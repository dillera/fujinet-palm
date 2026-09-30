#include "HostLinks.h"

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

void hexdump(const char *dir, const uint8_t *buf, size_t len)
{
    fprintf(stderr, "%s %3zu:", dir, len);
    for (size_t i = 0; i < len; ++i)
        fprintf(stderr, " %02x", buf[i]);
    fprintf(stderr, "\n");
}

static speed_t to_speed(uint32_t baud)
{
    switch (baud)
    {
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
    default: return B9600;
    }
}

SerialLink::SerialLink(const std::string &device)
{
    _fd = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (_fd < 0)
    {
        perror(device.c_str());
        return;
    }
    termios tio{};
    tcgetattr(_fd, &tio);
    cfmakeraw(&tio);
    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cflag &= ~(CRTSCTS | CSTOPB | PARENB);
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;
    tcsetattr(_fd, TCSANOW, &tio);
    set_baud_rate(9600);
    tcflush(_fd, TCIOFLUSH);
}

SerialLink::~SerialLink()
{
    if (_fd >= 0)
        ::close(_fd);
}

static int poll_read(int fd, uint8_t *buf, size_t len, uint32_t timeout_ms, bool trace)
{
    pollfd p{fd, POLLIN, 0};
    int r = poll(&p, 1, static_cast<int>(timeout_ms));
    if (r == 0)
        return 0;
    if (r < 0)
        return errno == EINTR ? 0 : -1;
    ssize_t got = ::read(fd, buf, len);
    if (got == 0)
        return -1;
    if (got < 0)
        return (errno == EAGAIN || errno == EINTR) ? 0 : -1;
    if (trace)
        hexdump("<<<", buf, got);
    return static_cast<int>(got);
}

static int write_all(int fd, const uint8_t *buf, size_t len, bool trace)
{
    if (trace)
        hexdump(">>>", buf, len);
    size_t done = 0;
    while (done < len)
    {
        ssize_t n = ::write(fd, buf + done, len - done);
        if (n < 0)
        {
            if (errno == EAGAIN || errno == EINTR)
            {
                pollfd p{fd, POLLOUT, 0};
                poll(&p, 1, 100);
                continue;
            }
            return -1;
        }
        done += n;
    }
    return static_cast<int>(done);
}

int SerialLink::read(uint8_t *buf, size_t len, uint32_t timeout_ms)
{
    return poll_read(_fd, buf, len, timeout_ms, trace);
}

int SerialLink::write(const uint8_t *buf, size_t len)
{
    int n = write_all(_fd, buf, len, trace);
    tcdrain(_fd);
    return n;
}

void SerialLink::set_baud_rate(uint32_t baud)
{
    termios tio{};
    tcgetattr(_fd, &tio);
    cfsetispeed(&tio, to_speed(baud));
    cfsetospeed(&tio, to_speed(baud));
    tcsetattr(_fd, TCSADRAIN, &tio);
    // The device is still reprogramming its UART right after the CMP exchange.
    usleep(50 * 1000);
    if (trace)
        fprintf(stderr, "--- baud %u\n", baud);
}

SocketLink::~SocketLink()
{
    ::close(_fd);
}

int SocketLink::read(uint8_t *buf, size_t len, uint32_t timeout_ms)
{
    return poll_read(_fd, buf, len, timeout_ms, trace);
}

int SocketLink::write(const uint8_t *buf, size_t len)
{
    return write_all(_fd, buf, len, trace);
}

int listen_tcp(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0 || listen(fd, 1) < 0)
    {
        perror("listen");
        return -1;
    }
    return fd;
}
