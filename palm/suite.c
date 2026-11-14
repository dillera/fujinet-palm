/* The tests, the same for every stack, and their report.
 *
 *   ping    the stack's smallest round trip, no network (32 of them, after one
 *           untimed-for-the-average first call that shows any wake-up cost)
 *   get16   an HTTP GET of 16 bytes: what one request costs
 *   get1k, get4k, get16k, get64k
 *           GETs of known bodies, read with the size the shipped apps use
 *   swNNN   16 KB read NNN bytes at a time, to find the fastest read size
 *   page    any web page (when the target is not an h2h server)
 *
 * Bodies come from server/h2h_server.py: byte i is (i ^ (i >> 8)) & 0xFF,
 * checked here as it arrives (the check is timed and reported apart). */
#include <PalmOS.h>

#include "h2h.h"

Results gResults;

#define PING_COUNT 32
#define SWEEP_BYTES 16384UL
#define SWEEP_REPS 2
#define STALL_US 20000000UL     /* 20 s without a byte ends a download */
#define UNKNOWN_SIZE 0xFFFFFFFFUL
#define URL_SIZE 256

static const UInt32 kSizes[] = {16, 1024, 4096, 16384, 65536};
static const Char *kSizeNames[] = {"get16", "get1k", "get4k", "get16k", "get64k"};
static const UInt16 kSweep[] = {64, 128, 256, 512, 1024, 2048, 4096, 8192};
#define N_SIZES (sizeof(kSizes) / sizeof(kSizes[0]))
#define N_SWEEP (sizeof(kSweep) / sizeof(kSweep[0]))

static UInt8 *gBuf;
static UInt16 gBufSize;
static Boolean gStopped;
static const Settings *gSettings;
static LinkCounters gSnap;

/* ---------------------------------------------------------------- formatting */

static UInt32 Div(UInt32 a, UInt32 b) { return b ? (a + b / 2) / b : 0; }

/* 850us, 9.8ms, 312ms, 4.62s, 17.9s, 214s */
static void FmtTime(Char *out, UInt32 us)
{
    if (us < 1000)
        StrPrintF(out, "%luus", us);
    else if (us < 10000)
        StrPrintF(out, "%lu.%lums", us / 1000, (us / 100) % 10);
    else if (us < 1000000UL)
        StrPrintF(out, "%lums", Div(us, 1000));
    else if (us < 10000000UL)
        StrPrintF(out, "%lu.%lu%lus", us / 1000000UL, (us / 100000) % 10, (us / 10000) % 10);
    else if (us < 100000000UL)
        StrPrintF(out, "%lu.%lus", us / 1000000UL, (us / 100000) % 10);
    else
        StrPrintF(out, "%lus", Div(us, 1000000UL));
}

void fmt_ms(Char *out, UInt32 us) { FmtTime(out, us); }

void fmt_secs(Char *out, UInt32 us)
{
    StrPrintF(out, "%lu.%lu%lus", us / 1000000UL, (us / 100000) % 10, (us / 10000) % 10);
}

/* s padded with spaces to width, on the right (left) or the left. */
static void Pad(Char *out, const Char *s, UInt16 width, Boolean left)
{
    UInt16 n = StrLen(s), i = 0;

    if (!left)
        for (; n + i < width; i++)
            out[i] = ' ';
    StrCopy(out + i, s);
    i += n;
    if (left)
        for (; i < width; i++)
            out[i] = ' ';
    out[i] = '\0';
}

/* Bytes per second without 64-bit arithmetic. */
static UInt32 Rate(UInt32 bytes, UInt32 us)
{
    if (us == 0 || bytes == 0)
        return 0;
    if (us >= 100000UL)
        return bytes * 1000UL / (us / 1000);    /* bytes < 4 MB */
    if (us < 100)
        us = 100;
    return bytes * 10000UL / (us / 100);        /* bytes < 400 KB */
}

void fmt_rate(Char *out, UInt32 bytes, UInt32 us)
{
    UInt32 k10 = Rate(bytes, us) * 10 / 1024;
    StrPrintF(out, "%lu.%luKB/s", k10 / 10, k10 % 10);
}

static UInt16 Pct(UInt32 part, UInt32 whole)
{
    if (!whole)
        return 0;
    if (whole > 1000000UL)
        return (UInt16)Div(part, whole / 100);
    return (UInt16)Div(part * 100, whole);
}

/* ---------------------------------------------------------------- counters */

static void Snap(void) { gSnap = gLink; }

void note_add(const Char *line)
{
    if (gResults.noteCount < H2H_NOTES) {
        StrNCopy(gResults.notes[gResults.noteCount], line, H2H_NOTE_LEN - 1);
        gResults.notes[gResults.noteCount][H2H_NOTE_LEN - 1] = '\0';
    }
    if (gResults.noteCount < 0xFFFF)
        gResults.noteCount++;
}

static void Since(LinkCounters *d)
{
    d->exchanges = gLink.exchanges - gSnap.exchanges;
    d->resends = gLink.resends - gSnap.resends;
    d->failures = gLink.failures - gSnap.failures;
    d->bytesOut = gLink.bytesOut - gSnap.bytesOut;
    d->bytesIn = gLink.bytesIn - gSnap.bytesIn;
    d->usSend = gLink.usSend - gSnap.usSend;
    d->usWait = gLink.usWait - gSnap.usWait;
    d->usRecv = gLink.usRecv - gSnap.usRecv;
    d->usLink = gLink.usLink - gSnap.usLink;
}

static Row *NewRow(const Char *name)
{
    Row *row;

    if (gResults.count >= H2H_MAX_ROWS)
        return NULL;
    row = &gResults.rows[gResults.count++];
    MemSet(row, sizeof(*row), 0);
    StrNCopy(row->test, name, H2H_TEST_LEN - 1);
    row->usMin = 0xFFFFFFFFUL;
    return row;
}

static Boolean Stop(void)
{
    if (!gStopped && ui_stop_requested())
        gStopped = true;
    return gStopped;
}

/* ---------------------------------------------------------------- one download */

typedef struct {
    UInt32 usOpen, usReady, usFirst, usBody, usClose, usTotal, usVerify;
    UInt32 bytes, reads, empty;
    LinkCounters link;
    UInt16 http;
} Download;

/* Byte i of a body is (i ^ (i >> 8)) & 0xFF: within each 256-byte block the
 * high byte is fixed, so the check runs on bytes, with no 32-bit shifts. */
static Boolean Check(const UInt8 *p, UInt32 pos, UInt16 n)
{
    UInt8 lo = (UInt8)pos, hi = (UInt8)(pos >> 8);
    UInt16 run;

    while (n) {
        run = 256 - lo;
        if (run > n)
            run = n;
        n -= run;
        while (run--) {
            if (*p++ != (UInt8)(lo ^ hi))
                return false;
            lo++;
        }
        hi++;
    }
    return true;
}

static Err Get(const Char *url, UInt32 expect, UInt16 chunk, Download *d)
{
    static Char full[URL_SIZE];
    UInt32 t0, t1, t2, t3, now, last;
    UInt16 got, iter = 0;
    Boolean eof = false, first = true;
    Err err, closeErr;

    MemSet(d, sizeof(*d), 0);
    link_url(url, full, sizeof(full));
    if (chunk > gBufSize)
        chunk = gBufSize;
    Snap();
    t0 = clk_us();
    err = link_get_open(full);
    t1 = clk_us();
    d->usOpen = t1 - t0;
    if (err == errNone)
        err = link_get_wait(&d->http);
    t2 = clk_us();
    d->usReady = t2 - t1;
    last = t2;
    while (err == errNone && !eof) {
        err = link_get_read(gBuf, chunk, &got, &eof);
        if (err != errNone)
            break;
        now = clk_us();
        if (got) {
            if (first) {
                d->usFirst = now - t0;
                first = false;
            }
            if (expect != UNKNOWN_SIZE) {
                if (!Check(gBuf, d->bytes, got))
                    err = h2hErrVerify;
                d->usVerify += clk_us() - now;
            }
            d->bytes += got;
            d->reads++;
            if (expect != UNKNOWN_SIZE && d->bytes > expect)
                err = h2hErrVerify;
            last = clk_us();
        } else if (!eof) {
            d->empty++;
            if (now - last > STALL_US)
                err = h2hErrTimeout;
        }
        if ((++iter & 15) == 0) {
            EvtResetAutoOffTimer();
            if (Stop())
                err = h2hErrCancelled;
        }
    }
    t3 = clk_us();
    d->usBody = t3 - t2;
    closeErr = link_get_close();
    d->usClose = clk_us() - t3;
    d->usTotal = clk_us() - t0;
    if (first)
        d->usFirst = d->usTotal;
    Since(&d->link);
    if (err == errNone && expect != UNKNOWN_SIZE && d->bytes != expect)
        err = h2hErrShort;
    if (err == errNone)
        err = closeErr;
    return err;
}

/* ---------------------------------------------------------------- tests */

/* s appended to out, which holds size bytes; whatever does not fit is cut. */
static void Append(Char *out, const Char *s, UInt16 size)
{
    UInt16 n = StrLen(out);

    while (*s && n + 1 < size)
        out[n++] = *s++;
    out[n] = '\0';
}

static void Line(const Char *a, const Char *b, const Char *c)
{
    Char line[48];

    line[0] = '\0';
    Append(line, a, sizeof(line));
    Append(line, " ", sizeof(line));
    Append(line, b, sizeof(line));
    Append(line, " ", sizeof(line));
    Append(line, c, sizeof(line));
    ui_progress(line);
}

static void Finish(Row *row, UInt16 good)
{
    if (!good)
        row->usMin = row->usMax = 0;
    if (!good)
        return;
#define AVG(f) row->f = Div(row->f, good)
    AVG(usTotal); AVG(usOpen); AVG(usReady); AVG(usFirst); AVG(usBody); AVG(usClose);
    AVG(usVerify); AVG(exchanges); AVG(empty); AVG(reads); AVG(bytesOut); AVG(bytesIn);
    AVG(usSend); AVG(usWait); AVG(usRecv); AVG(bytes);
#undef AVG
}

static void RunPing(void)
{
    Row *first = NewRow("ping1"), *row = NewRow("ping");
    LinkCounters d;
    UInt32 t0, dt;
    UInt16 i, good = 0;
    Char a[16];
    Err err;

    if (!first || !row)
        return;
    ui_progress("ping: link only, no network");
    /* The first call pays for any wake-up (NIO's GetInfo after 30 s idle,
     * classic's HotSync listener holding the port). */
    Snap();
    t0 = clk_us();
    err = link_ping();
    dt = clk_us() - t0;
    Since(&d);
    first->reps = 1;
    first->usTotal = first->usMin = first->usMax = dt;
    first->exchanges = d.exchanges;
    first->resends = d.resends;
    first->bytesOut = d.bytesOut;
    first->bytesIn = d.bytesIn;
    if (err != errNone) {
        first->fails = 1;
        first->lastErr = err;
    }
    Snap();
    for (i = 0; i < PING_COUNT && !Stop(); i++) {
        t0 = clk_us();
        err = link_ping();
        dt = clk_us() - t0;
        row->reps++;
        if (err != errNone) {
            row->fails++;
            row->lastErr = err;
            continue;
        }
        good++;
        row->usTotal += dt;
        if (dt < row->usMin) row->usMin = dt;
        if (dt > row->usMax) row->usMax = dt;
    }
    Since(&d);
    row->exchanges = d.exchanges;
    row->resends = d.resends;
    row->bytesOut = d.bytesOut;
    row->bytesIn = d.bytesIn;
    row->usSend = d.usSend;
    row->usWait = d.usWait;
    row->usRecv = d.usRecv;
    Finish(row, good);
    FmtTime(a, row->usTotal);
    Line("ping", a, good == row->reps ? "" : "(fails)");
}

static void TargetUrl(Char *out, UInt32 size, const Char *test, UInt16 rep)
{
    StrPrintF(out, "%s/bytes/%lu?r=%u&t=%s&k=%u&s=%s", gSettings->target, size,
              gResults.runId, test, rep, link_stack());
}

static void RunGet(const Char *name, UInt32 size, UInt16 chunk, UInt16 reps)
{
    static Char url[URL_SIZE];
    Row *row = NewRow(name);
    Char a[16], b[16];
    Download d;
    UInt16 k, good = 0;
    Err err;

    if (!row)
        return;
    row->chunk = chunk;
    for (k = 0; k < reps && !Stop(); k++) {
        StrPrintF(url, "%s %u/%u", name, k + 1, reps);
        ui_progress(url);
        if (size == UNKNOWN_SIZE)
            StrCopy(url, gSettings->target);
        else
            TargetUrl(url, size, name, k);
        err = Get(url, size, chunk, &d);
        row->reps++;
        row->resends += d.link.resends;
        if (err != errNone) {
            row->fails++;
            row->lastErr = err;
            Line(name, "failed:", link_err_text(err));
            if (err == h2hErrCancelled)
                break;
            continue;
        }
        good++;
        row->bytes += d.bytes;
        row->usTotal += d.usTotal;
        if (d.usTotal < row->usMin) row->usMin = d.usTotal;
        if (d.usTotal > row->usMax) row->usMax = d.usTotal;
        row->usOpen += d.usOpen;
        row->usReady += d.usReady;
        row->usFirst += d.usFirst;
        row->usBody += d.usBody;
        row->usClose += d.usClose;
        row->usVerify += d.usVerify;
        row->exchanges += d.link.exchanges;
        row->empty += d.empty;
        row->reads += d.reads;
        row->bytesOut += d.link.bytesOut;
        row->bytesIn += d.link.bytesIn;
        row->usSend += d.link.usSend;
        row->usWait += d.link.usWait;
        row->usRecv += d.link.usRecv;
    }
    Finish(row, good);
    if (good) {
        FmtTime(a, row->usTotal);
        fmt_rate(b, row->bytes, row->usBody);
        Line(name, a, b);
    }
}

/* The read sizes to try: the list, the shipped size and the largest this
 * build allows, each once, smallest first, none over the largest. */
static UInt16 SweepSizes(UInt16 *out)
{
    UInt16 max = link_max_chunk(), def = link_default_chunk(), n = 0, i, j, v;
    Boolean seen;

    for (i = 0; i < N_SWEEP + 2; i++) {
        v = i < N_SWEEP ? kSweep[i] : (i == N_SWEEP ? def : max);
        if (v > max || v > gBufSize)
            continue;
        seen = false;
        for (j = 0; j < n; j++)
            if (out[j] == v)
                seen = true;
        if (seen)
            continue;
        for (j = n; j > 0 && out[j - 1] > v; j--)
            out[j] = out[j - 1];
        out[j] = v;
        n++;
    }
    return n;
}

/* A feature's four characters, or nothing when the ROM has none. */
static void Put4cc(Char *out, UInt16 feature)
{
    UInt32 v = 0;
    UInt16 i;
    Char c;

    out[0] = '\0';
    if (FtrGet(sysFtrCreator, feature, &v) != errNone || v == 0)
        return;
    for (i = 0; i < 4; i++) {
        c = (Char)(v >> (24 - 8 * i));
        out[i] = (c >= 0x20 && c <= 0x7E) ? c : '?';
    }
    out[4] = '\0';
}

/* "hspr/Ecto VZ OS3.5": maker and model when the ROM says, CPU, Palm OS. */
static void DescribeDevice(Char *out)
{
    static const Char *cpus[] = {"?", "328", "EZ", "VZ", "SZ"};
    Char comp[6], dev[6];
    UInt32 rom = 0, cpu = 0, id;

    Put4cc(comp, sysFtrNumOEMCompanyID);
    Put4cc(dev, sysFtrNumOEMDeviceID);
    FtrGet(sysFtrCreator, sysFtrNumROMVersion, &rom);
    FtrGet(sysFtrCreator, sysFtrNumProcessorID, &cpu);
    id = (cpu >> 16) & 0xFFFF;
    out[0] = '\0';
    if (comp[0] || dev[0])
        StrPrintF(out, "%s/%s ", comp, dev);
    StrPrintF(out + StrLen(out), "%s OS%u.%u", id <= 4 ? cpus[id] : "ARM", (UInt16)((rom >> 24) & 0xFF),
              (UInt16)((rom >> 20) & 0x0F));
}

static Boolean IsPage(const Char *target)
{
    UInt16 len = StrLen(target);
    return len < 4 || StrCompare(target + len - 4, "/h2h") != 0;
}

void suite_run(const Settings *settings, Boolean pingOnly)
{
    UInt16 sizes[N_SWEEP + 2], n, i, reps;
    UInt32 t0;
    Char name[H2H_TEST_LEN], line[48];
    Err err;

    gSettings = settings;
    gStopped = false;
    MemSet(&gResults, sizeof(gResults), 0);
    MemSet(&gLink, sizeof(gLink), 0);
    gResults.runId = (UInt16)(SysRandom(TimGetSeconds() ^ TimGetTicks()) & 0x7FFF);
    gResults.started = TimGetSeconds();
    gResults.page = IsPage(settings->target);
    DescribeDevice(gResults.device);
    reps = settings->reps ? settings->reps : 1;

    clk_start();
    StrPrintF(line, "clock %s", clk_describe());
    ui_progress(line);
    gBufSize = link_max_chunk();
    if (!gBuf && !(gBuf = MemPtrNew(gBufSize))) {
        gResults.startErr = h2hErrNoMemory;
        ui_progress("not enough memory");
        return;
    }
    t0 = clk_us();
    ui_progress("opening the link...");
    err = link_start(gResults.firmware, sizeof(gResults.firmware));
    if (err != errNone) {
        gResults.startErr = err;
        Line("link:", link_err_text(err), "");
        link_stop();
        goto done;
    }
    Line("firmware", gResults.firmware, "");

    RunPing();
    if (pingOnly || Stop())
        goto stop;
    if (gResults.page) {
        RunGet("page", UNKNOWN_SIZE, link_default_chunk(), reps);
        goto stop;
    }
    for (i = 0; i < N_SIZES && !Stop(); i++) {
        if (kSizes[i] == 65536UL && !settings->big)
            continue;
        RunGet(kSizeNames[i], kSizes[i], link_default_chunk(), reps);
    }
    if (settings->sweep) {
        n = SweepSizes(sizes);
        for (i = 0; i < n && !Stop(); i++) {
            StrPrintF(name, "sw%u", sizes[i]);
            RunGet(name, SWEEP_BYTES, sizes[i], SWEEP_REPS);
        }
    }
stop:
    link_stop();
done:
    gResults.usRun = clk_us() - t0;
    gResults.stopped = gStopped;
    if (gStopped)
        ui_progress("stopped");
    else
        ui_progress("done");
    if (gBuf) {
        MemPtrFree(gBuf);
        gBuf = NULL;
    }
}

/* ---------------------------------------------------------------- report */

typedef struct {
    Char *buf;
    UInt16 size, len;
} Out;

static void Put(Out *o, const Char *s)
{
    UInt16 n = StrLen(s);
    if (o->len + n + 1 >= o->size)
        n = o->size - o->len - 1;
    MemMove(o->buf + o->len, s, n);
    o->len += n;
    o->buf[o->len] = '\0';
}

static void PutLine(Out *o, const Char *s)
{
    Put(o, s);
    Put(o, "\n");
}

static const Row *FindRow(const Char *test)
{
    UInt16 i;
    for (i = 0; i < gResults.count; i++)
        if (StrCompare(gResults.rows[i].test, test) == 0)
            return &gResults.rows[i];
    return NULL;
}

static void ReportWhere(Out *o, const Row *r)
{
    Char line[80], a[16], b[16], c[16];
    UInt32 link = r->usSend + r->usWait + r->usRecv, palm;
    UInt32 whole = r->usOpen + r->usReady + r->usBody + r->usClose;

    StrPrintF(line, "-- Where %s went --", r->test + 3);
    PutLine(o, line);
    FmtTime(a, r->usOpen);
    FmtTime(b, r->usReady);
    StrPrintF(line, "open %s  ready %s", a, b);
    PutLine(o, line);
    FmtTime(a, r->usBody);
    FmtTime(b, r->usClose);
    StrPrintF(line, "body %s  close %s", a, b);
    PutLine(o, line);
    if (!r->exchanges) {
        /* NioLib makes its exchanges inside its own code: no probe there. */
        PutLine(o, " link: not split (in the library)");
        FmtTime(a, r->usVerify);
        StrPrintF(line, " palm check %s", a);
        PutLine(o, line);
        StrPrintF(line, " reads %lu  empty %lu  avg %luB", r->reads, r->empty, Div(r->bytes, r->reads));
        PutLine(o, line);
        return;
    }
    palm = whole > link ? whole - link : 0;
    StrPrintF(line, " send %u%%  wait %u%%", Pct(r->usSend, whole), Pct(r->usWait, whole));
    PutLine(o, line);
    StrPrintF(line, " recv %u%%  palm %u%%", Pct(r->usRecv, whole), Pct(palm, whole));
    PutLine(o, line);
    FmtTime(a, r->usVerify);
    StrPrintF(line, " (palm incl. check %s)", a);
    PutLine(o, line);
    StrPrintF(line, " exch %lu  reads %lu  empty %lu", r->exchanges, r->reads, r->empty);
    PutLine(o, line);
    FmtTime(c, Div(link, r->exchanges));
    StrPrintF(line, " avg read %luB  %s/exch", Div(r->bytes, r->reads), c);
    PutLine(o, line);
    StrPrintF(line, " wire out %luB in %luB", r->bytesOut, r->bytesIn);
    PutLine(o, line);
    if (link_baud()) {
        /* What the line itself needs: 10 bits a byte. */
        UInt32 wire = (r->bytesOut + r->bytesIn) * 10 / (link_baud() / 1000);   /* ms */
        StrPrintF(line, " line time %lums of %lums", wire, Div(whole, 1000));
        PutLine(o, line);
    }
}

UInt16 suite_report(Char *buf, UInt16 size)
{
    Out o;
    Char line[96], a[16], b[16], c[16], name[12];
    const Row *r, *biggest = NULL, *best = NULL;
    UInt32 bestRate = 0, rate;
    UInt16 i, fails = 0;
    Boolean sweepHead = false, getHead = false;

    o.buf = buf;
    o.size = size;
    o.len = 0;
    buf[0] = '\0';
    StrPrintF(line, "%s  run %u", link_stack(), gResults.runId);
    PutLine(&o, line);
    PutLine(&o, link_title());
    Put(&o, "FW ");
    PutLine(&o, gResults.firmware[0] ? gResults.firmware : "?");
    StrPrintF(line, "%s  %s", gResults.device, clk_describe());
    PutLine(&o, line);
    if (link_baud())
        StrPrintF(line, "%lu baud, read %u (max %u)", link_baud(), link_default_chunk(), link_max_chunk());
    else
        StrPrintF(line, "read %u (max %u)", link_default_chunk(), link_max_chunk());
    PutLine(&o, line);
    if (gSettings)
        PutLine(&o, gSettings->target);
    FmtTime(a, gResults.usRun);
    StrPrintF(line, gResults.stopped ? "took %s, stopped early" : "took %s", a);
    PutLine(&o, line);
    if (gResults.startErr) {
        Put(&o, "Link failed: ");
        PutLine(&o, link_err_text(gResults.startErr));
        return o.len;
    }

    if ((r = FindRow("ping")) != NULL) {
        PutLine(&o, "-- Link only (no network) --");
        FmtTime(a, r->usTotal);
        FmtTime(b, r->usMin);
        FmtTime(c, r->usMax);
        StrPrintF(line, "ping %ux %s (%s-%s)", r->reps - r->fails, a, b, c);
        PutLine(&o, line);
        if (r->exchanges) {
            FmtTime(a, Div(r->usSend, r->exchanges));
            FmtTime(b, Div(r->usWait, r->exchanges));
            FmtTime(c, Div(r->usRecv, r->exchanges));
            StrPrintF(line, " send %s wait %s recv %s", a, b, c);
            PutLine(&o, line);
            StrPrintF(line, " wire %lu/%luB per ping", Div(r->bytesOut, r->exchanges),
                      Div(r->bytesIn, r->exchanges));
            PutLine(&o, line);
        }
        if ((r = FindRow("ping1")) != NULL) {
            FmtTime(a, r->usTotal);
            StrPrintF(line, " first call %s, %lu exch", a, r->exchanges);
            PutLine(&o, line);
        }
    }

    for (i = 0; i < gResults.count; i++) {
        r = &gResults.rows[i];
        if (r->fails)
            fails++;
        if (StrNCompare(r->test, "get", 3) != 0 && StrCompare(r->test, "page") != 0)
            continue;
        if (!getHead) {
            StrPrintF(line, "-- HTTP GET, read %u --", r->chunk);
            PutLine(&o, line);
            getHead = true;
        }
        if (r->reps == r->fails) {
            StrPrintF(line, "%s failed: %s", r->test + (r->test[0] == 'g' ? 3 : 0),
                      link_err_text(r->lastErr));
            PutLine(&o, line);
            continue;
        }
        FmtTime(a, r->usTotal);
        fmt_rate(b, r->bytes, r->usBody);
        Pad(name, r->test[0] == 'g' ? r->test + 3 : r->test, 4, true);
        if (r->bytes <= 64) {
            FmtTime(c, r->usFirst);
            StrPrintF(line, "%s %s  ttfb %s", name, a, c);
        } else {
            StrPrintF(line, "%s %s  %s", name, a, b);
        }
        PutLine(&o, line);
        if (r->fails) {
            StrPrintF(line, " %u of %u failed", r->fails, r->reps);
            PutLine(&o, line);
        }
        if (!biggest || r->bytes > biggest->bytes)
            biggest = r;
    }
    if (biggest)
        ReportWhere(&o, biggest);

    for (i = 0; i < gResults.count; i++) {
        r = &gResults.rows[i];
        if (r->test[0] != 's' || r->test[1] != 'w' || r->reps == r->fails)
            continue;
        rate = Rate(r->bytes, r->usBody);
        if (rate > bestRate) {
            bestRate = rate;
            best = r;
        }
    }
    for (i = 0; i < gResults.count; i++) {
        r = &gResults.rows[i];
        if (r->test[0] != 's' || r->test[1] != 'w')
            continue;
        if (!sweepHead) {
            PutLine(&o, "-- Read size, 16K body --");
            sweepHead = true;
        }
        StrPrintF(a, "%u", r->chunk);
        Pad(name, a, 5, false);
        if (r->reps == r->fails) {
            StrPrintF(line, "%s failed: %s", name, link_err_text(r->lastErr));
        } else {
            fmt_rate(b, r->bytes, r->usBody);
            c[0] = '\0';
            if (r->exchanges) {
                FmtTime(c, Div(r->usSend + r->usWait + r->usRecv, r->exchanges));
                StrCat(c, "/x");
            }
            /* No %c: Palm OS 3.5's StrPrintF does not take it. */
            StrPrintF(line, "%s%s %s %s%s", name, r->chunk == link_default_chunk() ? "*" : " ",
                      b, c, r == best ? " best" : "");
        }
        PutLine(&o, line);
    }
    if (sweepHead)
        PutLine(&o, " * = the shipped read size");
    StrPrintF(line, "Tests with failures: %u", fails);
    PutLine(&o, line);
    if (gResults.noteCount) {
        StrPrintF(line, "-- Link notes (%u) --", gResults.noteCount);
        PutLine(&o, line);
        for (i = 0; i < gResults.noteCount && i < H2H_NOTES; i++)
            PutLine(&o, gResults.notes[i]);
    }
    return o.len;
}

/* ---------------------------------------------------------------- upload */

static void UrlAppend(Char *out, const Char *s, UInt16 size)
{
    static const Char hex[] = "0123456789ABCDEF";
    UInt16 n = StrLen(out);
    UInt8 ch;

    for (; *s && n + 4 < size; s++) {
        ch = (UInt8)*s;
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '.' || ch == '_') {
            out[n++] = (Char)ch;
        } else {
            out[n++] = '%';
            out[n++] = hex[ch >> 4];
            out[n++] = hex[ch & 15];
        }
    }
    out[n] = '\0';
}

/* A GET whose reply is only read and dropped; tried up to three times, so
 * a link that drops the odd request still gets the results across (the
 * server takes a value twice the same). */
static Err Send(const Char *url)
{
    Download d;
    UInt16 tries;
    Err err = errNone;

    for (tries = 0; tries < 3; tries++) {
        err = Get(url, UNKNOWN_SIZE, 256, &d);
        if (err == errNone || err == h2hErrCancelled)
            break;
    }
    return err;
}

Err suite_upload(const Settings *settings, UInt16 *sent)
{
    static Char url[URL_SIZE];
    Char num[12], fw[48];
    UInt32 v[23];
    UInt16 i, j, start;
    const Row *r;
    Err err;

    *sent = 0;
    gSettings = settings;
    if (IsPage(settings->target))
        return h2hErrHttp;
    gBufSize = 256;
    if (!gBuf && !(gBuf = MemPtrNew(gBufSize)))
        return h2hErrNoMemory;
    clk_start();
    err = link_start(fw, sizeof(fw));
    if (err == errNone) {
        StrPrintF(url, "%s/h?id=%u&s=%s&n=%u&clkns=%lu&baud=%lu&def=%u&max=%u&run=%lu&fw=", settings->target,
                  gResults.runId, link_stack(), gResults.count, clk_resolution_ns(), link_baud(),
                  link_default_chunk(), link_max_chunk(), gResults.usRun);
        UrlAppend(url, gResults.firmware, sizeof(url));
        Append(url, "&dev=", sizeof(url));
        UrlAppend(url, gResults.device, sizeof(url));
        err = Send(url);
        if (err == errNone)
            (*sent)++;
    }
    for (i = 0; i < gResults.count && err == errNone; i++) {
        r = &gResults.rows[i];
        v[0] = r->reps; v[1] = r->fails; v[2] = r->chunk; v[3] = r->bytes;
        v[4] = r->usTotal; v[5] = r->usMin; v[6] = r->usMax; v[7] = r->usOpen;
        v[8] = r->usReady; v[9] = r->usFirst; v[10] = r->usBody; v[11] = r->usClose;
        v[12] = r->usVerify; v[13] = r->exchanges; v[14] = r->empty; v[15] = r->reads;
        v[16] = r->bytesOut; v[17] = r->bytesIn; v[18] = r->usSend; v[19] = r->usWait;
        v[20] = r->usRecv; v[21] = r->resends; v[22] = (UInt16)r->lastErr;
        /* As many values per GET as fit the URL (classic N: takes 256). */
        for (start = 0; start < 23 && err == errNone; start = j) {
            StrPrintF(url, "%s/r?id=%u&s=%s&t=%s&o=%u&v=", settings->target, gResults.runId,
                      link_stack(), r->test, start);
            for (j = start; j < 23; j++) {
                StrPrintF(num, j > start ? ",%lu" : "%lu", v[j]);
                if (j > start && StrLen(url) + StrLen(num) + 8 > 240)
                    break;
                Append(url, num, sizeof(url));
            }
            err = Send(url);
            if (err == errNone)
                (*sent)++;
        }
    }
    /* Notes made while sending count too, so the total goes with each. */
    for (i = 0; i < gResults.noteCount && i < H2H_NOTES && err == errNone; i++) {
        StrPrintF(url, "%s/n?id=%u&s=%s&i=%u&of=%u&m=", settings->target, gResults.runId,
                  link_stack(), i, gResults.noteCount);
        UrlAppend(url, gResults.notes[i], sizeof(url));
        err = Send(url);
        if (err == errNone)
            (*sent)++;
    }
    link_stop();
    MemPtrFree(gBuf);
    gBuf = NULL;
    return err;
}
