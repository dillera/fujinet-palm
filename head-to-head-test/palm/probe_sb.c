/* H2H SB's probe on fujinet-nio-lib's palmos-springboard transport.
 *
 * The library's one exchange over CS1 is fn_palmos_exchange
 * (src/platform/palmos/fn_palmos_exchange.c). The linker sends the library's
 * calls to it here instead (--wrap=fn_palmos_exchange, palm/Makefile), to
 * a copy of it that reads the clock between its steps:
 *   send  BEGIN, the request two bytes per DATA write, CHECK, DOORBELL
 *   wait  polling until the firmware publishes the reply's sequence: the
 *         RP2350, USB, NIO on the Mac and the network
 *   recv  reading the reply words back and checking them
 * The bus accesses are the library's own macros (fn_palmos_internal.h), the
 * constants, the retry logic and the stable register reads are the library's
 * (2733ba1 plus read_stable, which reads RESP_SEQ, RESULT, RESP_LEN and
 * RESP_CHECK until two reads agree, and read_reply_careful, which reads every
 * reply word that way on the passes after a failed check). Keep it in step if
 * that file changes.
 *
 * It also looks at what it lets through, without changing what the library
 * gets. A reply that passed the link's check word but is not a whole FujiBus
 * packet answering the request (length, device and command; the library
 * checks the checksum itself) is read a second time, and both reads are
 * noted; so are register reads that did not agree, results other than OK,
 * resends and failed checks. Notes go to the report (note_add) and to the
 * firmware console (probe_log), where each request and reply is logged too. */
#include <PalmOS.h>

#include "fujinet-nio.h"
#include "fn_palmos_internal.h"
#include "h2h.h"


#define REPLY_WAIT_SECONDS 35
#define SEND_TRIES 3
#define READ_RETRIES 3
#define STABLE_TRIES 4

#ifndef FN_PALMOS_EXPIRED
#define FN_PALMOS_EXPIRED(start, wait) (TimGetTicks() - (start) > (wait))
#endif

LinkCounters gLink;

/* The last exchange, for probe_last. */
static struct {
    uint8_t seq;
    uint16_t result, len, tries;
    uint8_t head[6];
} gLast;

static const Char kHex[] = "0123456789abcdef";

static Char *Hex(Char *p, const uint8_t *b, UInt16 n)
{
    while (n--) {
        *p++ = kHex[*b >> 4];
        *p++ = kHex[*b & 15];
        b++;
    }
    *p = '\0';
    return p;
}

/* Two characters per write to the LOG register; the firmware prints the
 * line as "palm: ..." at the newline. */
void probe_log(const Char *line)
{
    const UInt8 *s = (const UInt8 *)line;

    if (!_fn_palmos_cs1)
        return;
    while (s[0]) {
        FN_PALMOS_WR(SBN_W_LOG, (uint16_t)((uint16_t)s[0] << 8 | s[1]));
        if (!s[1])
            break;
        s += 2;
    }
    FN_PALMOS_WR(SBN_W_LOG, (uint16_t)((uint16_t)'\n' << 8));
}

static void Note(const Char *line)
{
    note_add(line);
    probe_log(line);
}

/* "#37 r0 n18 t1 450112000300": sequence, result, reply length, sends, and
 * the reply's first bytes. */
void probe_last(Char *out, UInt16 size)
{
    Char buf[48];

    buf[0] = '\0';
    if (gLast.seq) {
        StrPrintF(buf, "#%u r%u n%u t%u ", (UInt16)gLast.seq, gLast.result, gLast.len, gLast.tries);
        Hex(buf + StrLen(buf), gLast.head, gLast.len < 6 ? gLast.len : 6);
    }
    StrNCopy(out, buf, size - 1);
    out[size - 1] = '\0';
}

/* What is wrong with resp as the answer to req, or NULL. */
static const Char *PacketFault(const uint8_t *req, const uint8_t *resp, uint16_t n)
{
    if (n < 6)
        return "short";
    if ((uint16_t)(resp[2] | (uint16_t)resp[3] << 8) != n)
        return "length";
    if (resp[0] != req[0] || resp[1] != req[1])
        return "not ours";
    return NULL;
}

static uint8_t ReadReply(uint8_t *resp, uint16_t cap, uint16_t *resp_len, uint16_t *got, uint16_t *want);

/* The library's read_stable, noting when the reads disagreed. */
static uint16_t ReadStable(uint16_t offset, const Char *name)
{
    uint16_t a = FN_PALMOS_RD(offset), b, tries;
    Char line[H2H_NOTE_LEN];

    for (tries = 0; tries < STABLE_TRIES; tries++) {
        b = FN_PALMOS_RD(offset);
        if (b == a)
            break;
        if (!tries) {
            StrPrintF(line, "#%u %s read %u, then %u", (UInt16)_fn_palmos_seq, name, a, b);
            Note(line);
        }
        a = b;
    }
    return a;
}

/* A reply that passed its check word but does not parse: note it, read it
 * again (it stays in place), note how the second read compares, and give
 * the library the first read, as it would have had. */
static void CheckReply(const uint8_t *req, uint8_t *resp, uint16_t cap, uint16_t *resp_len)
{
    Char line[H2H_NOTE_LEN], *p;
    const Char *fault = PacketFault(req, resp, *resp_len), *again;
    uint16_t n = *resp_len, n2 = 0, i, got, want;
    uint8_t *first, ok2;
    UInt32 bytesIn = gLink.bytesIn;

    if (!fault)
        return;
    StrPrintF(line, "#%u %s n%u ", (UInt16)_fn_palmos_seq, fault, n);
    p = Hex(line + StrLen(line), req, 2);
    *p++ = '>';
    Hex(p, resp, n < 8 ? n : 8);
    Note(line);
    first = MemPtrNew(n ? n : 1);
    if (!first)
        return;
    MemMove(first, resp, n);
    ok2 = ReadReply(resp, cap, &n2, &got, &want);
    gLink.bytesIn = bytesIn;   /* the second read is not on the books */
    for (i = 0; i < n && i < n2 && first[i] == resp[i]; i++)
        ;
    again = ok2 ? PacketFault(req, resp, n2) : "bad check";
    if (ok2 && n2 == n && i == n)
        StrPrintF(line, "#%u reread same, fw #%u n%u", (UInt16)_fn_palmos_seq,
                  FN_PALMOS_RD(SBN_R_RESP_SEQ), FN_PALMOS_RD(SBN_R_RESP_LEN));
    else
        StrPrintF(line, "#%u reread %s n%u, @%u %u/%u, fw #%u", (UInt16)_fn_palmos_seq,
                  again ? again : "good", n2, i, i < n ? first[i] : 0, i < n2 ? resp[i] : 0,
                  FN_PALMOS_RD(SBN_R_RESP_SEQ));
    Note(line);
    MemMove(resp, first, n);
    *resp_len = n;
    MemPtrFree(first);
}

static void SendRequest(const uint8_t *req, uint16_t len)
{
    fn_palmos_port_t port = FN_PALMOS_PORT(_fn_palmos_cs1 + SBN_W(SBN_W_DATA));
    uint16_t pairs = len >> 1, w, check = 0;

    FN_PALMOS_WR(SBN_W_BEGIN, len);
    while (pairs--) {
        w = (uint16_t)((uint16_t)req[0] << 8 | req[1]);
        req += 2;
        check = SBN_CHECK_STEP(check, w);
        FN_PALMOS_PUT(port, w);
    }
    if (len & 1) {
        w = (uint16_t)((uint16_t)req[0] << 8);
        check = SBN_CHECK_STEP(check, w);
        FN_PALMOS_PUT(port, w);
    }
    FN_PALMOS_WR(SBN_W_CHECK, check);
}

static uint8_t ReadReply(uint8_t *resp, uint16_t cap, uint16_t *resp_len, uint16_t *got, uint16_t *want_out)
{
    fn_palmos_port_t port = FN_PALMOS_PORT(_fn_palmos_cs1 + SBN_R_RESP);
    uint16_t n = ReadStable(SBN_R_RESP_LEN, "len"), want = ReadStable(SBN_R_RESP_CHECK, "check");
    uint16_t keep, pairs, words, w, check = 0;

    *got = 0;
    *want_out = want;
    if (n > SBN_RESP_MAX)
        return 0;
    gLink.bytesIn += 4 + ((n + 1) & ~1);
    keep = n < cap ? n : cap;
    pairs = keep >> 1;
    words = ((n + 1) >> 1) - pairs;
    while (pairs--) {
        w = FN_PALMOS_GET(port);
        check = SBN_CHECK_STEP(check, w);
        resp[0] = (uint8_t)(w >> 8);
        resp[1] = (uint8_t)w;
        resp += 2;
    }
    if (words) {
        w = FN_PALMOS_GET(port);
        check = SBN_CHECK_STEP(check, w);
        if (keep & 1)
            *resp = (uint8_t)(w >> 8);
        words--;
    }
    while (words--) {
        w = FN_PALMOS_GET(port);
        check = SBN_CHECK_STEP(check, w);
    }
    *resp_len = keep;
    *got = check;
    *want_out = want;
    return check == want;
}

/* One reply word, read until two reads agree; counts the words that did not
 * agree at first. */
static uint16_t WordStable(uint16_t offset, uint16_t *misreads)
{
    uint16_t a = FN_PALMOS_RD(offset), b, tries;

    for (tries = 0; tries < STABLE_TRIES; tries++) {
        b = FN_PALMOS_RD(offset);
        if (b == a)
            break;
        if (!tries)
            (*misreads)++;
        a = b;
    }
    return a;
}

/* The library's read_reply_careful: ReadReply with every word read until two
 * reads agree, for the passes after a failed check. */
static uint8_t ReadReplyCareful(uint8_t *resp, uint16_t cap, uint16_t *resp_len, uint16_t *got,
                                uint16_t *want_out)
{
    uint16_t offset = SBN_R_RESP, misreads = 0;
    uint16_t n = ReadStable(SBN_R_RESP_LEN, "len"), want = ReadStable(SBN_R_RESP_CHECK, "check");
    uint16_t keep, pairs, words, w, check = 0;
    Char line[H2H_NOTE_LEN];

    *got = 0;
    *want_out = want;
    if (n > SBN_RESP_MAX)
        return 0;
    gLink.bytesIn += 4 + ((n + 1) & ~1);
    keep = n < cap ? n : cap;
    pairs = keep >> 1;
    words = ((n + 1) >> 1) - pairs;
    while (pairs--) {
        w = WordStable(offset, &misreads);
        offset += 2;
        check = SBN_CHECK_STEP(check, w);
        resp[0] = (uint8_t)(w >> 8);
        resp[1] = (uint8_t)w;
        resp += 2;
    }
    if (words) {
        w = WordStable(offset, &misreads);
        offset += 2;
        check = SBN_CHECK_STEP(check, w);
        if (keep & 1)
            *resp = (uint8_t)(w >> 8);
        words--;
    }
    while (words--) {
        w = WordStable(offset, &misreads);
        offset += 2;
        check = SBN_CHECK_STEP(check, w);
    }
    if (misreads) {
        StrPrintF(line, "#%u careful read: %u words read twice", (UInt16)_fn_palmos_seq, misreads);
        Note(line);
    }
    *resp_len = keep;
    *got = check;
    *want_out = want;
    return check == want;
}

uint16_t __wrap_fn_palmos_exchange(const uint8_t *req, uint16_t len, uint8_t *resp, uint16_t cap,
                                   uint16_t *resp_len)
{
    uint16_t tries, result = SBN_RES_BAD_CHECK, got, want;
    uint32_t start, wait = (uint32_t)REPLY_WAIT_SECONDS * SysTicksPerSecond();
    UInt32 t0, ts, tw, tr;
    Char line[H2H_NOTE_LEN];

    *resp_len = 0;
    if (!fn_palmos_open())
        return FN_PALMOS_NO_ANSWER;
    if (!len || len > SBN_REQ_MAX)
        return SBN_RES_BAD_LEN;
    gLink.exchanges++;
    gLast.len = 0;
    t0 = clk_us();
    for (tries = 0; tries < SEND_TRIES && result == SBN_RES_BAD_CHECK; tries++) {
        if (tries) {
            gLink.resends++;
            StrPrintF(line, "#%u bad check: resend", (UInt16)_fn_palmos_seq);
            Note(line);
        }
        if (++_fn_palmos_seq == 0)
            _fn_palmos_seq = 1;
        ts = clk_us();
        SendRequest(req, len);
        FN_PALMOS_WR(SBN_W_DOORBELL, _fn_palmos_seq);
        tw = clk_us();
        gLink.usSend += tw - ts;
        gLink.bytesOut += 6 + ((len + 1) & ~1);   /* BEGIN, CHECK, DOORBELL and the data */
        start = TimGetTicks();
        /* A match counts once a second read agrees (the library's loop). */
        for (;;) {
            if (FN_PALMOS_RD(SBN_R_RESP_SEQ) == _fn_palmos_seq) {
                if (FN_PALMOS_RD(SBN_R_RESP_SEQ) == _fn_palmos_seq)
                    break;
                StrPrintF(line, "#%u seq read early", (UInt16)_fn_palmos_seq);
                Note(line);
            }
            if (FN_PALMOS_EXPIRED(start, wait)) {
                gLast.seq = _fn_palmos_seq;
                gLast.result = FN_PALMOS_NO_ANSWER;
                gLast.tries = tries + 1;
                StrPrintF(line, "#%u no answer, fw #%u", (UInt16)_fn_palmos_seq, FN_PALMOS_RD(SBN_R_RESP_SEQ));
                Note(line);
                gLink.usWait += clk_us() - tw;
                gLink.failures++;
                gLink.usLink += clk_us() - t0;
                return FN_PALMOS_NO_ANSWER;
            }
        }
        result = ReadStable(SBN_R_RESULT, "result");
        gLink.usWait += clk_us() - tw;
    }
    gLast.seq = _fn_palmos_seq;
    gLast.result = result;
    gLast.tries = tries;
    if (result != SBN_RES_OK && result != SBN_RES_BAD_CHECK) {
        StrPrintF(line, "#%u result %u, again %u, st %u", (UInt16)_fn_palmos_seq, result,
                  FN_PALMOS_RD(SBN_R_RESULT), FN_PALMOS_RD(SBN_R_STATUS));
        Note(line);
    }
    if (result != SBN_RES_OK) {
        gLink.failures++;
        gLink.usLink += clk_us() - t0;
        return result;
    }
    tr = clk_us();
    for (tries = 0; tries <= READ_RETRIES; tries++) {
        if (tries)
            gLink.resends++;
        if (tries ? ReadReplyCareful(resp, cap, resp_len, &got, &want)
                  : ReadReply(resp, cap, resp_len, &got, &want)) {
            gLink.usRecv += clk_us() - tr;
            gLink.usLink += clk_us() - t0;
            CheckReply(req, resp, cap, resp_len);
            gLast.len = *resp_len;
            MemMove(gLast.head, resp, *resp_len < 6 ? *resp_len : 6);
            return result;
        }
        StrPrintF(line, "#%u reply check %u, want %u, n%u", (UInt16)_fn_palmos_seq, got, want,
                  FN_PALMOS_RD(SBN_R_RESP_LEN));
        Note(line);
    }
    *resp_len = 0;
    gLink.usRecv += clk_us() - tr;
    gLink.failures++;
    gLink.usLink += clk_us() - t0;
    return FN_PALMOS_BAD_READ;
}
