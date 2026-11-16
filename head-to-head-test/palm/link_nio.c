/* H2H's link: fujinet-nio through fujinet-nio-lib, as fujinet-palm's FI apps use it.
 * The same file serves both NIO builds; the transport underneath (cradle
 * serial or Springboard CS1) is chosen when linking, and its probe
 * (probe_serial.c or probe_sb.c) does the counting in gLink.
 *
 * HTTP on NIO is fn_open, then fn_info until the status line is in, then
 * fn_read until FN_READ_EOF: one exchange per piece. An upload opens with
 * FN_OPEN_BODY_UNKNOWN, fn_writes the body in order and commits it with a
 * write of nothing before the fn_info; FN_OPEN_FOLLOW_REDIR asks NIO to
 * follow redirects. */
#include <PalmOS.h>

#include "fujinet-nio.h"
#include "h2h.h"
#ifdef H2H_NIOLIB
#include "NioLib.h"
#endif

#ifndef H2H_DEFAULT_CHUNK
#error "H2H_DEFAULT_CHUNK: the read size the FI apps get from this transport"
#endif

/* One read exchange carries at most this much body (fn_read.c). */
#define MAX_CHUNK (FN_MAX_PACKET_SIZE - 22)
#define READY_TICKS (SysTicksPerSecond() * 35)

static fn_handle_t gHandle;
static Boolean gHandleOpen;
static UInt32 gOffset;      /* the next read's offset */
static UInt32 gWritten;     /* the next write's */

#if defined(H2H_HOST_STACK)
/* Built into another app (NIO Config's H2H page), which names the stack from
 * the NioLib transport it runs through. */
const Char *link_stack(void) { return h2h_host_stack(); }
const Char *link_title(void) { return h2h_host_title(); }
UInt32 link_baud(void) { return 0; }
#elif defined(H2H_SPRINGBOARD) || defined(H2H_NIOLIB)
/* Whether this copy of the app runs from the module's ROM (card 1 on), where
 * the Palm fetches every instruction over the Springboard bus, or from RAM. */
static Boolean InModuleRom(void)
{
    UInt16 card = 0;
    LocalID id;

    return SysCurAppDatabase(&card, &id) == errNone && card != 0;
}

#ifdef H2H_NIOLIB
/* The transport the installed NioLib is set to (nioTransport..., from its saved
 * settings, as NIO Config shows them), read once; 0 if NioLib can't say. The
 * library is loaded, opened and released the way libniolib-client.a does. */
static UInt16 gTransport;
static UInt32 gSerialBaud;

static UInt16 NioTransport(void)
{
    UInt16 ref, flags = 0, remaining = 0;
    Boolean loaded = false;
    NioLibVersion version;
    NioLibConfig config;

    if (gTransport)
        return gTransport;
    if (SysLibFind(nioLibName, &ref) != errNone) {
        if (SysLibLoad(nioLibType, nioLibCreator, &ref) != errNone)
            return 0;
        loaded = true;
    }
    version.size = sizeof(version);
    if (NioLibGetVersion(ref, &version) == errNone && version.abiMajor == 1 &&
        version.abiMinor >= 2 && NioLibOpen(ref) == errNone) {
        MemSet(&config, sizeof(config), 0);
        config.size = sizeof(config);
        if (NioLibGetConfig(ref, &config, &flags) == errNone) {
            gTransport = config.transport;
            gSerialBaud = config.serialBaud;
        }
        if (NioLibClose(ref, &remaining) != errNone)
            remaining = 1;
    }
    if (loaded && !remaining)
        SysLibRemove(ref);
    return gTransport;
}

/* Named by NioLib's transport, as NIO Config's H2H page names it, so a run
 * through the cradle is not filed as a Springboard one. */
const Char *link_stack(void)
{
    switch (NioTransport()) {
    case nioTransportSerial: return "niolib-serial";
    case nioTransportUSB: return "niolib-usb";
    case nioTransportNetwork: return "niolib-network";
    default: return InModuleRom() ? "niolib-springboard" : "niolib-springboard-ram";
    }
}

const Char *link_title(void)
{
    switch (NioTransport()) {
    case nioTransportSerial: return "NioLib serial";
    case nioTransportUSB: return "NioLib USB";
    case nioTransportNetwork: return "NioLib Network, NIO on the Palm";
    default: return InModuleRom() ? "NioLib SB, app in module" : "NioLib SB, app in RAM";
    }
}

UInt32 link_baud(void) { return NioTransport() == nioTransportSerial ? gSerialBaud : 0; }
#else
const Char *link_stack(void) { return InModuleRom() ? "nio-springboard" : "nio-springboard-ram"; }
const Char *link_title(void) { return InModuleRom() ? "NIO SB, app in module" : "NIO SB, app in RAM"; }
UInt32 link_baud(void) { return 0; }
#endif
#else
const Char *link_stack(void) { return "nio-serial"; }
const Char *link_title(void) { return "NIO FujiBus, cradle"; }
UInt32 link_baud(void) { return 115200L; }
#endif

UInt16 link_max_chunk(void) { return MAX_CHUNK; }
UInt16 link_default_chunk(void) { return H2H_DEFAULT_CHUNK < MAX_CHUNK ? H2H_DEFAULT_CHUNK : MAX_CHUNK; }

void link_url(const Char *url, Char *out, UInt16 outSize)
{
    StrNCopy(out, url, outSize - 1);
    out[outSize - 1] = '\0';
}

/* A library error as a note: the call, NIO's code, and how the last
 * exchange went (probe_last). */
static void NoteErr(const Char *call, uint8_t r)
{
    Char line[H2H_NOTE_LEN], last[40];

    probe_last(last, sizeof(last));
    StrPrintF(line, "%s err %u %s", call, (UInt16)r, last);
    note_add(line);
    probe_log(line);
}

/* NIO's codes, kept clear of the Palm OS ones. */
static Err NioErr(uint8_t r)
{
    return r == FN_OK ? errNone : (Err)(appErrorClass | 0x100 | r);
}

/* NioErr, with a note when it is an error. */
static Err Check(const Char *call, uint8_t r)
{
    if (r != FN_OK)
        NoteErr(call, r);
    return NioErr(r);
}

Err link_start(Char *fw, UInt16 fwSize)
{
    fn_fuji_info_t info;
    uint8_t r;

    fw[0] = '\0';
    if ((r = fn_init()) != FN_OK)
        return Check("init", r);
    MemSet(&info, sizeof(info), 0);
    r = fn_fuji_get_info(&info);
    if (r == FN_ERR_UNSUPPORTED) {
        StrCopy(fw, "NIO (no GetInfo)");
        return errNone;
    }
    if (r != FN_OK)
        return Check("getinfo", r);
    StrNCopy(fw, info.firmware, fwSize - 1);
    fw[fwSize - 1] = '\0';
    if (info.profile[0] && StrLen(fw) + 2 < fwSize) {
        StrCat(fw, " ");
        StrNCat(fw, info.profile, fwSize);
    }
    return errNone;
}

void link_stop(void)
{
    if (gHandleOpen)
        fn_close(gHandle);
    gHandleOpen = false;
    fn_shutdown();
}

/* Clock.GetTime: a dozen bytes each way, no network. */
Err link_ping(void)
{
    FN_TIME_T t;
#if defined(H2H_HOST_STACK) || defined(H2H_NIOLIB)
    /* NioLib's Network transport is a NIO on the Palm, with no clock device:
     * its smallest round trip is the Fuji device's GetInfo. */
    if (StrCompare(link_stack(), "niolib-network") == 0) {
        fn_fuji_info_t info;
        return Check("ping", fn_fuji_get_info(&info));
    }
#endif
    return Check("ping", fn_clock_get(&t));
}

/* H2H_GET, _POST and _PUT are NIO's own method numbers. */
Err link_open(UInt8 method, const Char *url, UInt8 flags)
{
    uint8_t f = 0, r;

    if (flags & H2H_FOLLOW)
        f |= FN_OPEN_FOLLOW_REDIR;
    if (flags & H2H_BODY)
        f |= FN_OPEN_BODY_UNKNOWN;
    r = fn_open(&gHandle, method, url, f);
    gOffset = gWritten = 0;
    gHandleOpen = r == FN_OK;
    return Check("open", r);
}

Err link_get_open(const Char *url)
{
    return link_open(H2H_GET, url, 0);
}

/* The body's next piece, at the offset the last one ended at; fn_write
 * splits what one packet cannot carry. */
Err link_write(const UInt8 *buf, UInt16 len)
{
    Char call[20];
    uint16_t n;
    uint8_t r = FN_OK;

    while (len && r == FN_OK) {
        n = 0;
        r = fn_write(gHandle, gWritten, buf, len, &n);
        if (r == FN_OK && n == 0)
            r = FN_ERR_IO;
        gWritten += n;
        buf += n;
        len -= n;
    }
    if (r == FN_OK)
        return errNone;
    StrPrintF(call, "write@%lu", gWritten);
    return Check(call, r);
}

/* The end of a body of unknown length: a write of nothing. */
Err link_commit(void)
{
    static UInt8 none;
    uint16_t n = 0;

    return Check("commit", fn_write(gHandle, gWritten, &none, 0, &n));
}

Err link_get_wait(UInt16 *httpStatus)
{
    uint16_t status = 0;
    uint32_t length = 0;
    uint8_t flags = 0, r;
    UInt32 start = TimGetTicks();

    *httpStatus = 0;
    do {
        r = fn_info(gHandle, &status, &length, &flags);
        if (r != FN_ERR_NOT_READY && r != FN_ERR_BUSY)
            break;
        if (ui_stop_requested())
            return h2hErrCancelled;
    } while (TimGetTicks() - start < READY_TICKS);
    if (r == FN_ERR_NOT_READY || r == FN_ERR_BUSY)
        return h2hErrTimeout;
    if (r != FN_OK)
        return Check("status", r);
    if (flags & FN_INFO_HAS_STATUS)
        *httpStatus = status;
    if (*httpStatus >= 400)
        return h2hErrHttp;
    return errNone;
}

Err link_get_read(UInt8 *buf, UInt16 max, UInt16 *got, Boolean *eof)
{
    uint16_t n = 0;
    uint8_t flags = 0, r;

    *got = 0;
    *eof = false;
    r = fn_read(gHandle, gOffset, buf, max, &n, &flags);
    if (r == FN_ERR_NOT_READY || r == FN_ERR_BUSY)
        return errNone;
    if (r != FN_OK)
        return Check("read", r);
    *got = n;
    gOffset += n;
    *eof = (flags & FN_READ_EOF) != 0;
    return errNone;
}

Err link_get_close(void)
{
    uint8_t r = FN_OK;

    if (gHandleOpen)
        r = fn_close(gHandle);
    gHandleOpen = false;
    return Check("close", r);
}

const Char *link_err_text(Err err)
{
    switch (err) {
    case errNone: return "ok";
    case h2hErrTimeout: return "timed out";
    case h2hErrCancelled: return "stopped";
    case h2hErrHttp: return "HTTP error";
    case h2hErrVerify: return "bad data";
    case h2hErrShort: return "short body";
    case h2hErrNoMemory: return "no memory";
    }
    if ((err & 0xFF00) == (appErrorClass | 0x100))
        return fn_error_string((uint8_t)(err & 0xFF));
    return "error";
}
