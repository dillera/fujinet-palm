/* palm/suite.c on a desktop: the same tests, report and upload, with the
 * link replaced by plain HTTP over a socket. It checks the suite's logic and
 * the server end to end, not any Palm stack:
 *
 *   host-h2h <target> [reps] [--no-upload]
 *
 * Each socket call counts as one "exchange" in gLink; ping costs nothing. */
#include <PalmOS.h>

#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "h2h.h"

LinkCounters gLink;

/* ---------------------------------------------------------------- Palm OS bits */

int host_StrPrintF(char *out, const char *fmt, ...)
{
    char f[256];
    size_t i, j = 0;
    va_list ap;
    int n;

    /* %lu is a 32-bit UInt32 here, so it is passed as an unsigned int. */
    for (i = 0; fmt[i] && j < sizeof(f) - 1; i++) {
        f[j++] = fmt[i];
        if (fmt[i] == '%' && fmt[i + 1] == 'l')
            i++;
    }
    f[j] = '\0';
    va_start(ap, fmt);
    n = vsprintf(out, f, ap);
    va_end(ap);
    return n;
}

Err FtrGet(UInt32 creator, UInt16 feature, UInt32 *value)
{
    (void)creator;
    switch (feature) {
    case sysFtrNumROMVersion: *value = 0x03503000UL; return 0;
    case sysFtrNumOEMCompanyID: *value = 0x686F7374UL; return 0;   /* "host" */
    case sysFtrNumOEMDeviceID: *value = 0x74657374UL; return 0;    /* "test" */
    }
    return 1;
}

UInt32 TimGetTicks(void)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    return (UInt32)(now.tv_sec * 100 + now.tv_usec / 10000);
}

static struct timeval gStart;

void clk_start(void) { gettimeofday(&gStart, NULL); }

UInt32 clk_us(void)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    return (UInt32)((now.tv_sec - gStart.tv_sec) * 1000000L + (now.tv_usec - gStart.tv_usec));
}

UInt32 clk_resolution_ns(void) { return 1000; }
const Char *clk_describe(void) { return "host 1us"; }

void ui_progress(const Char *line) { fprintf(stderr, "| %s\n", line); }
Boolean ui_stop_requested(void) { return false; }

/* ---------------------------------------------------------------- the link */

#define CHUNK 1024

static int gSock = -1;
static Boolean gEof;

const Char *link_stack(void) { return "host"; }
const Char *link_title(void) { return "Host sockets"; }
UInt16 link_max_chunk(void) { return 8192; }
UInt16 link_default_chunk(void) { return CHUNK; }
UInt32 link_baud(void) { return 0; }

void link_url(const Char *url, Char *out, UInt16 outSize)
{
    snprintf(out, outSize, "%s", url);
}

Err link_start(Char *fw, UInt16 fwSize)
{
    snprintf(fw, fwSize, "host");
    return errNone;
}

void link_stop(void)
{
    if (gSock >= 0)
        close(gSock);
    gSock = -1;
}

/* H2H_FLAKY=n fails every nth ping and open with a note, as a link that
 * drops the odd request would. */
static unsigned gCalls;

static Boolean Flaky(const char *call)
{
    const char *n = getenv("H2H_FLAKY");
    char line[H2H_NOTE_LEN];

    if (!n || atoi(n) <= 0 || ++gCalls % (unsigned)atoi(n))
        return false;
    snprintf(line, sizeof(line), "%s err 5 #%u flaky", call, gCalls);
    note_add(line);
    return true;
}

Err link_ping(void)
{
    gLink.exchanges++;
    return Flaky("ping") ? (Err)(appErrorClass | 0x105) : errNone;
}

static void Count(UInt32 t0, size_t out, size_t in)
{
    gLink.exchanges++;
    gLink.bytesOut += out;
    gLink.bytesIn += in;
    gLink.usRecv += clk_us() - t0;
    gLink.usLink += clk_us() - t0;
}

Err link_get_open(const Char *url)
{
    char host[128], port[8] = "80", req[512];
    const char *p = url, *path;
    struct addrinfo hints, *ai;
    size_t n;
    UInt32 t0 = clk_us();

    if (strncmp(p, "http://", 7) != 0)
        return h2hErrHttp;
    if (Flaky("open"))
        return (Err)(appErrorClass | 0x105);
    p += 7;
    path = strchr(p, '/');
    n = path ? (size_t)(path - p) : strlen(p);
    if (n >= sizeof(host))
        return h2hErrHttp;
    memcpy(host, p, n);
    host[n] = '\0';
    if (strchr(host, ':')) {
        snprintf(port, sizeof(port), "%s", strchr(host, ':') + 1);
        *strchr(host, ':') = '\0';
    }
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &ai) != 0)
        return h2hErrHttp;
    gSock = socket(ai->ai_family, SOCK_STREAM, 0);
    if (gSock < 0 || connect(gSock, ai->ai_addr, ai->ai_addrlen) != 0) {
        freeaddrinfo(ai);
        return h2hErrHttp;
    }
    freeaddrinfo(ai);
    n = (size_t)snprintf(req, sizeof(req), "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
                         path ? path : "/", host);
    if (send(gSock, req, n, 0) != (ssize_t)n)
        return h2hErrHttp;
    gEof = false;
    Count(t0, n, 0);
    return errNone;
}

/* Reads the status line and headers a byte at a time, so the body is left. */
Err link_get_wait(UInt16 *httpStatus)
{
    char head[2048];
    size_t n = 0;
    UInt32 t0 = clk_us();

    *httpStatus = 0;
    while (n < sizeof(head) - 1 && recv(gSock, head + n, 1, 0) == 1) {
        n++;
        if (n >= 4 && memcmp(head + n - 4, "\r\n\r\n", 4) == 0)
            break;
    }
    head[n] = '\0';
    Count(t0, 0, n);
    if (sscanf(head, "HTTP/%*s %hu", httpStatus) != 1)
        return h2hErrHttp;
    return *httpStatus >= 400 ? h2hErrHttp : errNone;
}

Err link_get_read(UInt8 *buf, UInt16 max, UInt16 *got, Boolean *eof)
{
    ssize_t n;
    UInt32 t0 = clk_us();

    *got = 0;
    *eof = gEof;
    if (gEof)
        return errNone;
    n = recv(gSock, buf, max, 0);
    if (n < 0)
        return h2hErrHttp;
    if (n == 0)
        gEof = *eof = true;
    *got = (UInt16)n;
    Count(t0, 0, (size_t)n);
    return errNone;
}

Err link_get_close(void)
{
    link_stop();
    return errNone;
}

const Char *link_err_text(Err err)
{
    switch (err) {
    case errNone: return "ok";
    case h2hErrHttp: return "HTTP error";
    case h2hErrVerify: return "bad data";
    case h2hErrShort: return "short body";
    case h2hErrTimeout: return "timed out";
    }
    return "error";
}

/* ---------------------------------------------------------------- main */

int main(int argc, char **argv)
{
    Settings s;
    static char report[8000];
    UInt16 sent = 0, i;
    Err err;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <target> [reps] [--no-upload]\n", argv[0]);
        return 2;
    }
    memset(&s, 0, sizeof(s));
    snprintf(s.target, sizeof(s.target), "%s", argv[1]);
    s.reps = argc > 2 && argv[2][0] != '-' ? (UInt16)atoi(argv[2]) : 2;
    s.big = true;
    s.sweep = true;
    snprintf(s.name, sizeof(s.name), "%s", getenv("H2H_WHO") ? getenv("H2H_WHO") : "Host Tester");
    suite_run(&s, false);
    suite_report(report, sizeof(report));
    fputs(report, stdout);
    for (i = 0; i < gResults.count; i++)
        if (gResults.rows[i].fails)
            fprintf(stderr, "row %s: %u of %u failed (%s)\n", gResults.rows[i].test, gResults.rows[i].fails,
                    gResults.rows[i].reps, link_err_text(gResults.rows[i].lastErr));
    if (argc > 2 && strcmp(argv[argc - 1], "--no-upload") == 0)
        return 0;
    err = suite_upload(&s, &sent);
    printf("uploaded %u requests: %s\n", sent, link_err_text(err));
    return err == errNone ? 0 : 1;
}
