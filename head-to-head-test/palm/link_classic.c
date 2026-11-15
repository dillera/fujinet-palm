/* FN H2H's link: classic fujinet-firmware over the HotSync cradle, FujiBus
 * (SLIP) at 115200 baud, the network through N1:.
 *
 * It does what fujinet-palm's FN apps do (common/fnlink.c and fnnet.c on the
 * classic branch), with the same codec (common/fujibus.c), the same retries
 * and the same one-byte-at-a-time receive, and times each exchange:
 *   send  the request written out and drained (SrmSendWait), i.e. its wire time
 *   wait  until the first reply byte arrives: FujiNet's turnaround and network
 *   recv  the rest of the reply frame, then the checksum
 * Only the read size differs: the FN apps read at most 256 bytes (their
 * packet buffer); this one can ask for up to MAX_CHUNK, for the sweep.
 *
 * HTTP on classic N: is OPEN, then STATUS (the first STATUS makes the
 * request), then STATUS + READ for every piece until STATUS shows the end. */
#include <PalmOS.h>
#include <SerialMgrOld.h>

#include "fnlink.h"
#include "h2h.h"

#define BAUD 115200L
/* The FN apps give the Serial Manager 1024 bytes; replies here can be four
 * times that, so the buffer holds a whole one. */
#define RX_BUFFER 8192
#define MAX_CHUNK 4096
#define DEFAULT_CHUNK 256       /* FN_NET_CHUNK in the FN apps */
#define FRAME_CAP (2 * (MAX_CHUNK + 32) + 2)

#define NET_DEVICE 0x71         /* N1: */
#define NETCMD_OPEN   0x4F
#define NETCMD_CLOSE  0x43
#define NETCMD_READ   0x52
#define NETCMD_STATUS 0x53
#define CMD_GET_ADAPTERCONFIG 0xE8

#define MODE_GET 12
#define NDEV_OK 1
#define NDEV_EOF 136

/* fnlink.c's and fnnet.c's timings. */
#define ATTEMPTS 8
#define WAIT_TICKS (SysTicksPerSecond() * 7 / 10)
#define OPEN_ATTEMPTS 2
#define OPEN_WAIT_TICKS (SysTicksPerSecond() * 10)
#define FIRST_STATUS_TICKS (SysTicksPerSecond() * 30)
#define IDLE_TICKS (SysTicksPerSecond() * 5)

LinkCounters gLink;

static UInt16 gPort;
static Boolean gOpen, gOldSerial;
static UInt8 gRxBuffer[RX_BUFFER];
static UInt8 *gFrame;           /* FRAME_CAP bytes from the heap */
static UInt32 gLastTicks;

typedef struct {
    UInt16 avail;
    UInt8 connected;
    UInt8 error;
} NetStatus;

static NetStatus gStatus;
static Boolean gHaveStatus;

const Char *link_stack(void) { return "classic-serial"; }
const Char *link_title(void) { return "Classic FujiBus, cradle"; }
UInt16 link_max_chunk(void) { return MAX_CHUNK; }
UInt16 link_default_chunk(void) { return DEFAULT_CHUNK; }
UInt32 link_baud(void) { return BAUD; }

void link_url(const Char *url, Char *out, UInt16 outSize)
{
    StrCopy(out, "N1:");
    StrNCat(out, url, outSize);
}

/* ---------------------------------------------------------------- the port */

static UInt32 PortReceive(UInt8 *buf, UInt32 count, Int32 timeout, Err *err)
{
    if (gOldSerial)
        return SerReceive(gPort, buf, count, timeout, err);
    return SrmReceive(gPort, buf, count, timeout, err);
}

static void PortSend(const UInt8 *buf, UInt32 count)
{
    Err err;

    if (gOldSerial) {
        SerSend(gPort, (void *)buf, count, &err);
        SerSendWait(gPort, -1);
    } else {
        SrmSend(gPort, (void *)buf, count, &err);
        SrmSendWait(gPort);
    }
}

static void PortFlush(void)
{
    if (gOldSerial)
        SerReceiveFlush(gPort, 0);
    else
        SrmReceiveFlush(gPort, 0);
}

static void PortClearErr(void)
{
    if (gOldSerial)
        SerClearErr(gPort);
    else
        SrmClearErr(gPort);
}

static Err PortOpen(void)
{
    UInt32 value;
    Err err;

    if (gOpen)
        return errNone;
    if (!gFrame && !(gFrame = MemPtrNew(FRAME_CAP)))
        return h2hErrNoMemory;
    gOldSerial = FtrGet(sysFileCSerialMgr, sysFtrNewSerialPresent, &value) != errNone || value == 0;
    if (!gOldSerial) {
        if ((err = SrmOpen(serPortCradlePort, BAUD, &gPort)) != errNone)
            return err;
        SrmSetReceiveBuffer(gPort, gRxBuffer, sizeof(gRxBuffer));
    } else {
        err = SysLibFind("Serial Library", &gPort);
        if (err == errNone)
            err = SerOpen(gPort, 0, BAUD);
        if (err == serErrAlreadyOpen)
            SerClose(gPort);
        if (err != errNone)
            return err;
        SerSetReceiveBuffer(gPort, gRxBuffer, sizeof(gRxBuffer));
    }
    gOpen = true;
    return errNone;
}

static void PortClose(void)
{
    if (gOpen) {
        if (gOldSerial) {
            SerSetReceiveBuffer(gPort, NULL, 0);
            SerClose(gPort);
        } else {
            SrmSetReceiveBuffer(gPort, NULL, 0);
            SrmClose(gPort);
        }
        gOpen = false;
    }
    if (gFrame) {
        MemPtrFree(gFrame);
        gFrame = NULL;
    }
}

/* One SLIP frame (END ... END), byte by byte as fnlink.c reads it; *first
 * is when its first byte came. */
static UInt16 ReceiveFrame(UInt32 deadline, UInt32 *first)
{
    UInt16 len = 0;
    UInt8 byte;
    Boolean any = false;
    Err err;

    while (TimGetTicks() < deadline) {
        if (PortReceive(&byte, 1, SysTicksPerSecond() / 20, &err) != 1) {
            if (err != errNone && err != serErrTimeOut)
                PortClearErr();
            continue;
        }
        if (!any) {
            *first = clk_us();
            any = true;
        }
        gLink.bytesIn++;
        if (len == 0 && byte != 0xC0)
            continue;
        if (byte == 0xC0 && len > 1) {
            gFrame[len++] = byte;
            return len;
        }
        if (byte == 0xC0)
            len = 0;
        if (len >= FRAME_CAP)
            len = 0;
        gFrame[len++] = byte;
    }
    return 0;
}

/* One FujiBus exchange, resent up to attempts times. */
static Err Call(UInt8 device, UInt8 command, const FbParam *params, UInt16 nparams,
                const void *payload, UInt16 payloadLen, void *reply, UInt16 replyMax,
                UInt16 *replyLen, UInt16 attempts, UInt32 waitTicks)
{
    static UInt8 request[FB_MAX_FRAME];   /* off the stack: Palm OS 3 gives apps little */
    UInt16 requestLen, frameLen, i;
    UInt32 t0, ts, tw, tf, te;
    FbReply parsed;
    Err err;

    if ((err = PortOpen()) != errNone)
        return err;
    requestLen = fb_build_request(device, command, params, nparams, (const UInt8 *)payload,
                                  payloadLen, request, sizeof(request));
    if (requestLen == 0)
        return fnErrTooBig;
    gLink.exchanges++;
    t0 = clk_us();
    err = fnErrNoReply;
    for (i = 0; i < attempts; i++) {
        if (i)
            gLink.resends++;
        PortFlush();
        ts = clk_us();
        PortSend(request, requestLen);
        tw = clk_us();
        gLink.usSend += tw - ts;
        gLink.bytesOut += requestLen;
        tf = 0;
        frameLen = ReceiveFrame(TimGetTicks() + waitTicks, &tf);
        te = clk_us();
        if (tf) {
            gLink.usWait += tf - tw;
            gLink.usRecv += te - tf;
        } else {
            gLink.usWait += te - tw;
        }
        if (frameLen == 0 || !fb_parse_reply(gFrame, frameLen, &parsed) || parsed.device != device)
            continue;
        if (parsed.command != FB_CMD_ACK) {
            err = fnErrRefused;
            break;
        }
        if (replyLen)
            *replyLen = parsed.data_len < replyMax ? parsed.data_len : replyMax;
        if (reply && replyLen)
            MemMove(reply, parsed.data, *replyLen);
        err = errNone;
        gLastTicks = TimGetTicks();
        break;
    }
    if (err != errNone)
        gLink.failures++;
    gLink.usLink += clk_us() - t0;
    return err;
}

/* ---------------------------------------------------------------- N1: */

static Err Status(NetStatus *status, UInt16 attempts, UInt32 waitTicks)
{
    UInt8 raw[4];
    UInt16 got = 0;
    Err err;

    MemSet(status, sizeof(*status), 0);
    err = Call(NET_DEVICE, NETCMD_STATUS, NULL, 0, NULL, 0, raw, sizeof(raw), &got, attempts,
               waitTicks);
    if (err != errNone)
        return err;
    if (got < sizeof(raw))
        return fnErrRefused;
    status->avail = raw[0] | ((UInt16)raw[1] << 8);
    status->connected = raw[2];
    status->error = raw[3];
    return errNone;
}

/* As fnnet.c: before a request that is sent only once, make sure FujiNet is
 * listening (its HotSync listener takes the port after a quiet spell). */
static Err Reclaim(void)
{
    NetStatus status;

    if (gLastTicks != 0 && TimGetTicks() - gLastTicks < IDLE_TICKS)
        return errNone;
    return Status(&status, ATTEMPTS, WAIT_TICKS);
}

Err link_start(Char *fw, UInt16 fwSize)
{
    FnAdapterConfig config;
    UInt16 got = 0;
    Err err;

    fw[0] = '\0';
    MemSet(&config, sizeof(config), 0);
    err = Call(FB_DEVICE_FUJINET, CMD_GET_ADAPTERCONFIG, NULL, 0, NULL, 0, &config,
               sizeof(config), &got, ATTEMPTS, WAIT_TICKS);
    if (err != errNone)
        return err;
    config.fn_version[sizeof(config.fn_version) - 1] = '\0';
    StrNCopy(fw, config.fn_version, fwSize - 1);
    fw[fwSize - 1] = '\0';
    return errNone;
}

void link_stop(void)
{
    PortClose();
}

/* STATUS of N1: with nothing open: four bytes back, no network. */
Err link_ping(void)
{
    NetStatus status;
    return Status(&status, ATTEMPTS, WAIT_TICKS);
}

Err link_get_open(const Char *url)
{
    FbParam p[2];
    Err err;

    if ((err = Reclaim()) != errNone)
        return err;
    gHaveStatus = false;
    p[0].value = MODE_GET;
    p[0].size = 1;
    p[1].value = 0;     /* no translation */
    p[1].size = 1;
    return Call(NET_DEVICE, NETCMD_OPEN, p, 2, url, StrLen(url) + 1, NULL, 0, NULL, OPEN_ATTEMPTS,
                OPEN_WAIT_TICKS);
}

/* The first STATUS is when FujiNet makes the request; sent at most twice. */
Err link_get_wait(UInt16 *httpStatus)
{
    Err err = Status(&gStatus, 2, FIRST_STATUS_TICKS);

    *httpStatus = 0;    /* classic N: does not report it */
    if (err != errNone)
        return err;
    gHaveStatus = true;
    if (gStatus.error != NDEV_OK && gStatus.error != NDEV_EOF)
        return h2hErrHttp;
    return errNone;
}

/* STATUS (unless the last call left one), then READ what it says is there. */
Err link_get_read(UInt8 *buf, UInt16 max, UInt16 *got, Boolean *eof)
{
    FbParam p;
    UInt16 want;
    Err err;

    *got = 0;
    *eof = false;
    if (!gHaveStatus && (err = Status(&gStatus, ATTEMPTS, WAIT_TICKS)) != errNone)
        return err;
    gHaveStatus = false;
    if (gStatus.avail == 0) {
        if (!gStatus.connected || gStatus.error == NDEV_EOF)
            *eof = true;
        else if (gStatus.error != NDEV_OK)
            return h2hErrHttp;
        return errNone;
    }
    want = gStatus.avail < max ? gStatus.avail : max;
    if (want > MAX_CHUNK)
        want = MAX_CHUNK;
    p.value = want;
    p.size = 2;
    /* Sent once: a resent READ would lose the bytes of the lost reply. */
    return Call(NET_DEVICE, NETCMD_READ, &p, 1, NULL, 0, buf, want, got, 1, WAIT_TICKS * 3);
}

Err link_get_close(void)
{
    gHaveStatus = false;
    return Call(NET_DEVICE, NETCMD_CLOSE, NULL, 0, NULL, 0, NULL, 0, NULL, ATTEMPTS, WAIT_TICKS);
}

const Char *link_err_text(Err err)
{
    switch (err) {
    case errNone: return "ok";
    case fnErrNoReply: return "no reply";
    case fnErrRefused: return "refused (NAK)";
    case fnErrTooBig: return "too big";
    case h2hErrTimeout: return "timed out";
    case h2hErrCancelled: return "stopped";
    case h2hErrHttp: return "network error";
    case h2hErrVerify: return "bad data";
    case h2hErrShort: return "short body";
    case h2hErrNoMemory: return "no memory";
    case serErrAlreadyOpen: return "port in use";
    }
    return "error";
}
