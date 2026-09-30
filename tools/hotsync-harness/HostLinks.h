#ifndef HOST_LINKS_H
#define HOST_LINKS_H

#include "HotSyncLink.h"

#include <cstdio>
#include <string>

// termios serial port for driving a real cradle from macOS/Linux.
class SerialLink : public HotSyncLink
{
public:
    explicit SerialLink(const std::string &device);
    ~SerialLink() override;
    bool is_open() const { return _fd >= 0; }
    int read(uint8_t *buf, size_t len, uint32_t timeout_ms) override;
    int write(const uint8_t *buf, size_t len) override;
    void set_baud_rate(uint32_t baud) override;
    bool trace = false;

private:
    int _fd = -1;
};

// One accepted TCP connection (NetSync or serial-over-TCP from an emulator).
class SocketLink : public HotSyncLink
{
public:
    explicit SocketLink(int fd) : _fd(fd) {}
    ~SocketLink() override;
    int read(uint8_t *buf, size_t len, uint32_t timeout_ms) override;
    int write(const uint8_t *buf, size_t len) override;
    bool trace = false;

private:
    int _fd;
};

int listen_tcp(int port);

void hexdump(const char *dir, const uint8_t *buf, size_t len);

#endif
