/* H2H's link: fujinet-nio through fujinet-nio-lib, as fujinet-palm's FI apps use it.
 * The same file serves both NIO builds; the transport underneath (cradle
 * serial or Springboard CS1) is chosen when linking, and its probe
 * (probe_serial.c or probe_sb.c) does the counting in gLink.
 *
 * HTTP on NIO is fn_open, then fn_info until the status line is in, then
 * fn_read until FN_READ_EOF: one exchange per piece. */
#include <PalmOS.h>

#include "fujinet-nio.h"
#include "h2h.h"

#ifndef H2H_DEFAULT_CHUNK
#error "H2H_DEFAULT_CHUNK: the read size the FI apps get from this transport"
#endif

/* One read exchange carries at most this much body (fn_read.c). */
#define MAX_CHUNK (FN_MAX_PACKET_SIZE - 22)
#define READY_TICKS (SysTicksPerSecond() * 35)

static fn_handle_t gHandle;
static Boolean gHandleOpen;
static UInt32 gOffset;

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
/* The installed NioLib (in RAM) and its default Springboard transport. */
const Char *link_stack(void) { return InModuleRom() ? "niolib-springboard" : "niolib-springboard-ram"; }
const Char *link_title(void) { return InModuleRom() ? "NioLib SB, app in module" : "NioLib SB, app in RAM"; }
#else
const Char *link_stack(void) { return InModuleRom() ? "nio-springboard" : "nio-springboard-ram"; }
const Char *link_title(void) { return InModuleRom() ? "NIO SB, app in module" : "NIO SB, app in RAM"; }
#endif
UInt32 link_baud(void) { return 0; }
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
#ifdef H2H_HOST_STACK
    /* NioLib's Network transport is a NIO on the Palm, with no clock device:
     * its smallest round trip is the Fuji device's GetInfo. */
    if (StrCompare(link_stack(), "niolib-network") == 0) {
        fn_fuji_info_t info;
        return Check("ping", fn_fuji_get_info(&info));
    }
#endif
    return Check("ping", fn_clock_get(&t));
}

Err link_get_open(const Char *url)
{
    uint8_t r = fn_open(&gHandle, FN_METHOD_GET, url, 0);

    gOffset = 0;
    gHandleOpen = r == FN_OK;
    return Check("open", r);
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
