/* Head-to-head speed test: what every part of the app shares.
 *
 * One source, three builds (palm/Makefile):
 *   FN H2H  classic fujinet-firmware, FujiBus N1: over the cradle (link_classic.c)
 *   H2H     fujinet-nio over the cradle, fujinet-nio-lib palmos-serial (link_nio.c + probe_serial.c)
 *   H2H SB  fujinet-nio over a Springboard module, palmos-springboard (link_nio.c + probe_sb.c)
 * Every build runs the same tests (suite.c) against the same target server
 * (server/h2h_server.py) and reports the same numbers, so the three can be
 * compared line for line. */
#ifndef H2H_H
#define H2H_H

#include <PalmOS.h>

#ifndef H2H_COMMIT
#define H2H_COMMIT "unknown"
#endif
#ifndef H2H_BUILT
#define H2H_BUILT "unknown"
#endif

/* ---------------------------------------------------------------- clock.c */

/* Microseconds since clk_start(), from the Dragonball's system timer when it
 * can be read (resolution 1-31 us), else from the tick count (10 ms). Wraps
 * after 71 minutes, so a run calls clk_start() first. Never steps back. */
void clk_start(void);
UInt32 clk_us(void);
UInt32 clk_resolution_ns(void);
const Char *clk_describe(void);    /* "TMR1 30us", "ticks 10ms" */

/* ---------------------------------------------------------------- link counters */

/* Every request/reply round trip on the link, whichever stack makes it, is
 * counted here by the link code (classic) or by the probes wrapped around
 * fujinet-nio-lib (NIO). Times are microseconds.
 *   send: from the start of the exchange until the request is out (serial:
 *         the bytes handed to the Serial Manager; Springboard: the CS1 writes)
 *   wait: from then until the first byte of the reply (serial) or until the
 *         firmware publishes it (Springboard): the device and the network
 *   recv: from then until the reply has been taken in and checked */
typedef struct {
    UInt32 exchanges;
    UInt32 resends;     /* requests sent again inside one exchange */
    UInt32 failures;    /* exchanges that ended without a good reply */
    UInt32 bytesOut;    /* bytes on the wire: SLIP frames, or 2 x CS1 words */
    UInt32 bytesIn;
    UInt32 usSend;
    UInt32 usWait;
    UInt32 usRecv;
    UInt32 usLink;      /* all of the time inside exchanges */
} LinkCounters;

extern LinkCounters gLink;

/* The NIO probes' side of a note (link_nio.c): what the last exchange looked
 * like, and the note on the device's own log where it has one (the
 * Springboard firmware console). */
void probe_last(Char *out, UInt16 size);
void probe_log(const Char *line);

/* ---------------------------------------------------------------- link_*.c */

/* Each stack provides these. Errors are Palm OS Err values; link_err_text
 * names them. */
#define h2hErrTimeout   (appErrorClass | 0x40)
#define h2hErrCancelled (appErrorClass | 0x41)
#define h2hErrHttp      (appErrorClass | 0x42)
#define h2hErrVerify    (appErrorClass | 0x43)
#define h2hErrShort     (appErrorClass | 0x44)
#define h2hErrNoMemory  (appErrorClass | 0x45)
/* The redirect and upload tests' own (suite.c names them; so does the
 * server's report, from a row's last error). */
#define h2hErrNoFollow  (appErrorClass | 0x46)  /* a redirect came back though following was asked */
#define h2hErrFollowed  (appErrorClass | 0x47)  /* a redirect was followed though it was not asked */
#define h2hErrNoTest    (appErrorClass | 0x48)  /* 404: the server has no such test (an older h2h server) */
#define h2hErrNoStack   (appErrorClass | 0x49)  /* this stack has no such request */
#define h2hErrBadUpload (appErrorClass | 0x4A)  /* 422: the server found the body it got wrong */

const Char *link_stack(void);     /* short id in results: "classic-serial" */
#ifdef H2H_HOST_STACK
/* Built into another app, that app provides the stack and its title. */
const Char *h2h_host_stack(void);
const Char *h2h_host_title(void);
#endif
const Char *link_title(void);     /* "Classic FujiBus, cradle" */
/* Opens the link and asks the device who it is; fw gets a version line. */
Err link_start(Char *fw, UInt16 fwSize);
void link_stop(void);
/* The smallest round trip the stack has that touches no network. */
Err link_ping(void);
/* Largest body bytes one read exchange can carry in this build, and what the
 * shipped apps ask for in one read. */
UInt16 link_max_chunk(void);
UInt16 link_default_chunk(void);
UInt32 link_baud(void);           /* serial line rate, 0 when there is none */
/* The URL the stack wants for a plain http:// URL ("N1:" on classic). */
void link_url(const Char *url, Char *out, UInt16 outSize);

/* One HTTP GET: open, wait until the reply has started, read until the end,
 * close. A read can return no bytes (nothing has come yet). */
Err link_get_open(const Char *url);
/* Any other request: method is H2H_GET, H2H_POST or H2H_PUT (NIO's own
 * numbers), with H2H_FOLLOW (follow redirects) and H2H_BODY (a body of
 * unknown length follows: link_write it in order, then link_commit). Its
 * reply goes through link_get_wait, _read and _close as a GET's does. A
 * stack without such requests answers h2hErrNoStack. */
#define H2H_GET 1
#define H2H_POST 2
#define H2H_PUT 3
#define H2H_FOLLOW 0x01
#define H2H_BODY 0x02
Err link_open(UInt8 method, const Char *url, UInt8 flags);
Err link_write(const UInt8 *buf, UInt16 len);
Err link_commit(void);
Err link_get_wait(UInt16 *httpStatus);
Err link_get_read(UInt8 *buf, UInt16 max, UInt16 *got, Boolean *eof);
Err link_get_close(void);
const Char *link_err_text(Err err);

/* ---------------------------------------------------------------- suite.c */

#define H2H_MAX_ROWS 32
#define H2H_TEST_LEN 8
#define H2H_NOTES 12        /* diagnostic notes kept in full; the rest are counted */
#define H2H_NOTE_LEN 56

/* One line of results: a test run reps times; times are averages over the
 * good repetitions unless named min or max. */
typedef struct {
    Char test[H2H_TEST_LEN];  /* "ping", "get1k", "sw512", "page", "post16k" */
    UInt16 reps;
    UInt16 fails;
    UInt16 chunk;             /* read (and write) size asked for */
    UInt32 bytes;             /* body bytes per repetition: received, or for an upload sent */
    UInt32 usTotal, usMin, usMax;
    UInt32 usOpen;            /* the open call */
    UInt32 usWrite;           /* an upload's writes, its commit included */
    UInt32 writes;            /* per repetition: write calls, the commit one of them */
    UInt32 usReady;           /* from the open (or the commit) until the reply has started */
    UInt32 usFirst;           /* from the start until the first body byte */
    UInt32 usBody;            /* from the reply starting until the last byte */
    UInt32 usClose;
    UInt32 usVerify;          /* Palm time spent checking the body */
    UInt32 exchanges, empty, reads;   /* per repetition */
    UInt32 bytesOut, bytesIn;         /* on the wire, per repetition */
    UInt32 usSend, usWait, usRecv;    /* in the link, per repetition */
    UInt32 resends;
    Err lastErr;
} Row;

typedef struct {
    Char target[128];   /* base URL of an h2h server, or any web page */
    UInt16 reps;
    Boolean big;        /* include the 64 KB download */
    Boolean sweep;      /* the read size sweep */
    Char name[24];      /* the tester: sent with the results, so testers' runs stay apart */
} Settings;

typedef struct {
    UInt16 runId;
    UInt16 count;
    Row rows[H2H_MAX_ROWS];
    Char firmware[48];
    Char device[24];
    UInt32 started;     /* TimGetSeconds() */
    UInt32 usRun;       /* how long the whole run took */
    Boolean page;       /* the target was a plain page, not an h2h server */
    Boolean stopped;    /* a tap ended the run early */
    Err startErr;
    UInt16 noteCount;   /* every note made, kept or not */
    Char notes[H2H_NOTES][H2H_NOTE_LEN];
} Results;

extern Results gResults;

/* The app's side of a run: progress lines and the stop check. */
void ui_progress(const Char *line);
Boolean ui_stop_requested(void);

/* Runs everything the settings ask for, or just the ping test. */
void suite_run(const Settings *settings, Boolean pingOnly);
/* The report as text into buf (NUL-terminated); returns its length. */
UInt16 suite_report(Char *buf, UInt16 size);
/* This app's build, "a1b2c3d 11-16 15:04Z" (H2H_COMMIT and H2H_BUILT, which palm/Makefile passes in). */
void suite_app_build(Char *out, UInt16 size);
/* The installed NioLib's build from its tver resource: "NioLib c1969bc 2026-11-16 15:02Z", or "NioLib: not
 * installed", or "NioLib: older build, no build info". out needs 64 bytes. */
void suite_niolib_build(Char *out, UInt16 size);
/* Sends the results to the target server, one GET per row. */
Err suite_upload(const Settings *settings, UInt16 *sent);
/* An error as text: the suite's own (h2hErrNoFollow...), else link_err_text's.
 * An app that has the suite built in names errors with this: the suite is
 * within reach of its calls where the link may not be (NIO Config's page). */
const Char *suite_err_text(Err err);

/* A short line on anything odd a link saw (a reply that does not parse, an
 * error from the library), for the report and the upload. The first
 * H2H_NOTES are kept; a run starts with none. */
void note_add(const Char *line);

/* Formatting helpers (StrPrintF has no floats). */
void fmt_ms(Char *out, UInt32 us);          /* "12.3" ms, "1234" ms */
void fmt_secs(Char *out, UInt32 us);        /* "1.23" s */
void fmt_rate(Char *out, UInt32 bytes, UInt32 us);   /* "6.8" KB/s */

#endif /* H2H_H */
