/* FI Texas Hold'em for Palm OS.
 *
 * A client for the FujiNet Texas Hold'em server, the same one the Apple II,
 * Atari, CoCo, MS-DOS and Intellivision clients of
 * github.com/dillera/fujinet-texasHoldEm play against. Like the Apple II
 * client, it asks for the server's packed binary state (bin=1, here with
 * be=1 for big-endian numbers) as a plain HTTP GET, through a FujiNet NIO on
 * the cradle (fujinet-nio-lib), polls it about once a second, and plays the
 * two-letter move codes the server offers; every poker rule is the server's.
 * See common/holdem.h.
 *
 * A GET runs alongside the event loop, one exchange with FujiNet at a time,
 * so the table keeps its clock and the pen while the server answers.
 *
 * The cards are the Apple II client's hi-res art, the small cards the Atari
 * Lynx client's sprites, both converted by tools/make_cards.py.
 */
#include <PalmOS.h>

#include "fujinet-nio.h"
#include "holdem.h"
#include "cards.h"
#include "HoldemRsc.h"

#define CREATOR APP_CREATOR
#define PREFS_ID 1
#define PREFS_VERSION 1

#define DEFAULT_SERVER "https://th.carr-designs.com/"
#define DEFAULT_TABLE  "ai2"
#define SERVER_LEN 64
#define URL_LEN 160
/* hs_build_url starts each URL with the classic "N1:", which NIO does not take. */
#define URL_SKIP 3

/* Largest READ; with its header it fits the library's 512-byte packet. */
#define NET_CHUNK 256
/* A lost reply is asked for again this many times in all. */
#define NET_TRIES 3
#define FIRST_STATUS_TICKS (SysTicksPerSecond() * 15)
#define STALL_TICKS (SysTicksPerSecond() * 10)
#define READ_POLL_TICKS (SysTicksPerSecond() / 20)
#define POLL_GAP_TICKS (SysTicksPerSecond())
#define TICK_TICKS (SysTicksPerSecond() / 4)
/* Polls in a row that may fail before the table gives up. */
#define MAX_FAILURES 4

#define heErrTimeout  (appErrorClass | 0x10)
#define heErrBadReply (appErrorClass | 0x11)
#define heErrTooLong  (appErrorClass | 0x12)
#define heErrNoTable  (appErrorClass | 0x13)
/* A fujinet-nio-lib error, in the low byte. */
#define heErrNio      (appErrorClass | 0x100)
/* An HTTP status other than 2xx, in the low 12 bits. */
#define heErrHttp     (appErrorClass | 0x1000)

/* Posted once a form is drawn, to start its first fetch. */
#define fetchEvent firstUserEvent

/* What the GET in flight is for; its reply goes to the matching ...Done. */
#define REQ_NONE   0
#define REQ_TABLES 1
#define REQ_STATE  2
#define REQ_MOVE   3
#define REQ_LEAVE  4

typedef struct {
    char name[HS_NAME_LEN];
    char table[HS_TABLE_ID_LEN];
    char server[SERVER_LEN];
} HePrefs;

/* Screen layout of the table form. */
#define SEAT_W 80
#define SEAT_H 20
#define SEATS_Y 0
#define SEATS_H 80
#define FELT_Y 81
#define FELT_H 31
#define BOARD_X 3
#define CARD_STEP 15
#define HOLE_X 82
#define PANEL_X 114
#define INFO_Y 113
#define STATUS_Y 124
#define BAR_Y 136
#define BAR_H 24

static HePrefs gPrefs;
static HsGame gGame;
static HsTables gTables;
static fn_u8 gReply[HS_STATE_MAX + 4];
static char gUrl[URL_LEN];
static char gStatus[48];
static WinHandle gOffscreen;

/* The GET in flight: its handle, the reply read so far, and when FujiNet
 * last gave it anything. */
static UInt8 gReq = REQ_NONE;
static fn_handle_t gHandle = FN_INVALID_HANDLE;
static Boolean gGotInfo;        /* its HTTP status is in */
static UInt16 gReplyLen;
static UInt32 gLastHeard, gNextStep;
static UInt8 gTries;

static Boolean gSeated;         /* at the table form, polling */
static Boolean gHaveGame;       /* gGame holds a state */
static Boolean gMyTurn;         /* waiting for this player's move */
static UInt32 gNextPoll;
static UInt32 gTurnEnd;         /* tick the server's move clock runs out */
static UInt16 gFailures;
static Int16 gCursor;           /* highlighted move, for the hard keys */
static char gNotice[48];        /* trouble shown on the status line */

/* A bitmap built in memory for the card art. */
typedef struct {
    BitmapType hdr;
    UInt16 bits[CARD_H];
} PicBitmap;
static PicBitmap gPic;

/* ---- Errors ---------------------------------------------------------- */

static void ErrorText(Err err, char *out)
{
    if (err == (heErrNio | FN_ERR_TIMEOUT))
        StrCopy(out, "No answer from FujiNet. Is the cradle cabled to it?");
    else if (err == (heErrNio | FN_ERR_IO))
        StrCopy(out, "FujiNet could not reach the server. Is its WiFi up, and the server "
                     "address right?");
    else if (err == (heErrNio | FN_ERR_URL_TOO_LONG))
        StrCopy(out, "The server address is too long.");
    else if ((err & 0xFF00) == heErrNio)
        StrPrintF(out, "FujiNet: %s.", fn_error_string((uint8_t)err));
    else if ((err & 0xF000) == heErrHttp)
        StrPrintF(out, "The server answered HTTP %u. Is the server address right?",
                  err & 0x0FFF);
    else if (err == heErrTimeout)
        StrCopy(out, "The server stopped sending.");
    else if (err == heErrBadReply || err == heErrTooLong)
        StrCopy(out, "The server's reply was not a Hold'em state. Check the server address.");
    else if (err == heErrNoTable)
        StrCopy(out, "The server has no such table.");
    else
        StrPrintF(out, "Error %x.", err);
}

static void ShowError(const char *what, Err err)
{
    char detail[96];

    ErrorText(err, detail);
    FrmCustomAlert(MessageAlert, what, "\n", detail);
}

/* ---- HTTP through FujiNet NIO ----------------------------------------- */

/* A library result as an Err, for ErrorText. */
static Err NioErr(uint8_t result)
{
    return result == FN_OK ? errNone : (heErrNio | result);
}

/* A reply lost on the cradle line, or cut short on it. */
static Boolean LostReply(uint8_t result)
{
    return result == FN_ERR_TIMEOUT || result == FN_ERR_TRANSPORT || result == FN_ERR_IO;
}

/* Drops the GET in flight, if any. */
static void CancelGet(void)
{
    if (gHandle != FN_INVALID_HANDLE)
        fn_close(gHandle);
    gHandle = FN_INVALID_HANDLE;
    gReq = REQ_NONE;
}

/* Opens a GET of the server's path for req; StepGet then reads its reply
 * into gReply. An OPEN whose reply was lost is sent again; a handle NIO
 * opened for the lost one is the least recently used, so ALLOW_EVICT gives
 * it back. */
static Err StartGet(UInt8 req, const char *path, const char *table, const char *player)
{
    uint8_t result;
    UInt8 tries = 0;

    CancelGet();
    if (!hs_build_url(gUrl, sizeof(gUrl), gPrefs.server, path, table, player))
        return heErrNio | FN_ERR_URL_TOO_LONG;
    result = fn_init();
    while (result == FN_OK) {
        result = fn_open(&gHandle, FN_METHOD_GET, gUrl + URL_SKIP, FN_OPEN_ALLOW_EVICT);
        if (!LostReply(result) || ++tries >= NET_TRIES)
            break;
    }
    if (result != FN_OK) {
        gHandle = FN_INVALID_HANDLE;
        return NioErr(result);
    }
    gReq = req;
    gGotInfo = false;
    gReplyLen = 0;
    gTries = 0;
    gLastHeard = gNextStep = TimGetTicks();
    return errNone;
}

/* One exchange of the GET in flight: first its HTTP status (INFO), then
 * the body (READ) until EOF. Both are non-blocking, answering NOT_READY
 * until the server has; a lost reply is asked for again at the same
 * offset, which NIO answers with the same bytes. Returns true once the
 * reply is whole or the GET failed (*err), having closed the handle. */
static Boolean StepGet(Err *err)
{
    uint16_t httpStatus, got, want;
    uint32_t length;
    uint8_t flags, result;
    Boolean done = false;

    *err = errNone;
    EvtResetAutoOffTimer();
    if (!gGotInfo) {
        result = fn_info(gHandle, &httpStatus, &length, &flags);
        if (result == FN_OK) {
            gGotInfo = true;
            gLastHeard = TimGetTicks();
            if ((flags & FN_INFO_HAS_STATUS) && (httpStatus < 200 || httpStatus > 299)) {
                *err = heErrHttp | (httpStatus & 0x0FFF);
                done = true;
            }
        }
    } else {
        want = sizeof(gReply) - gReplyLen;
        if (want > NET_CHUNK)
            want = NET_CHUNK;
        if (want == 0) {
            *err = heErrTooLong;
            done = true;
            result = FN_OK;
        } else {
            result = fn_read(gHandle, gReplyLen, gReply + gReplyLen, want, &got, &flags);
            if (result == FN_OK) {
                if (got > want) {
                    *err = heErrBadReply;
                    done = true;
                } else {
                    gReplyLen += got;
                    if (got > 0)
                        gLastHeard = TimGetTicks();
                    done = (flags & FN_READ_EOF) != 0;
                }
            }
        }
    }

    if (LostReply(result)) {
        if (++gTries >= NET_TRIES) {
            *err = NioErr(result);
            done = true;
        }
    } else if (result == FN_ERR_NOT_READY || result == FN_ERR_BUSY) {
        gTries = 0;
        gNextStep = TimGetTicks() + READ_POLL_TICKS;
    } else if (result != FN_OK) {
        *err = NioErr(result);
        done = true;
    } else {
        gTries = 0;
    }
    if (!done && TimGetTicks() - gLastHeard >
                     (gGotInfo || gReplyLen > 0 ? STALL_TICKS : FIRST_STATUS_TICKS)) {
        *err = heErrTimeout;
        done = true;
    }
    if (done) {
        fn_close(gHandle);
        gHandle = FN_INVALID_HANDLE;
        gReq = REQ_NONE;
    }
    return done;
}

/* Runs the GET in flight to its end, for when there is no event loop left
 * to run it (the app is closing). */
static Err FinishGet(void)
{
    Err err;

    while (!StepGet(&err))
        SysTaskDelay(READ_POLL_TICKS);
    return err;
}

/* Starts a game call (state, move/XX, leave) of this player at the table. */
static Err StartGameCall(UInt8 req, const char *path)
{
    return StartGet(req, path, gPrefs.table, gPrefs.name);
}

/* The reply of a game call, into gGame. */
static Err GameReply(Err err)
{
    if (err != errNone)
        return err;
    if (gReplyLen == 0)
        return heErrNoTable;
    if (!hs_parse_game(gReply, gReplyLen, &gGame))
        return heErrBadReply;
    gHaveGame = true;
    return errNone;
}

/* ---- Drawing helpers -------------------------------------------------- */

static void DrawPic(const UInt16 *rows, Int16 width, Int16 height, Coord x, Coord y)
{
    MemSet(&gPic.hdr, sizeof(gPic.hdr), 0);
    gPic.hdr.width = width;
    gPic.hdr.height = height;
    gPic.hdr.rowBytes = 2;
    gPic.hdr.pixelSize = 1;
    gPic.hdr.version = 1;
    MemMove(gPic.bits, rows, height * sizeof(UInt16));
    WinDrawBitmap(&gPic.hdr, x, y);
}

static void DrawTableCard(const char *code, Coord x, Coord y)
{
    UInt16 rows[CARD_H];
    UInt8 rank, suit;

    if (!hs_card(code, &rank, &suit))
        rank = suit = HS_UNKNOWN;
    card_table(rank, suit, rows);
    DrawPic(rows, 16, CARD_H, x, y);
}

/* An empty place on the board: a dotted outline of a card. */
static void DrawEmptySlot(Coord x, Coord y)
{
    UInt16 rows[CARD_H];
    UInt16 i;

    for (i = 0; i < CARD_H; i++)
        rows[i] = 0xFFFF;
    for (i = 1; i < CARD_W - 1; i += 2) {
        rows[1] &= ~(0x8000 >> i);
        rows[CARD_H - 2] &= ~(0x8000 >> i);
    }
    for (i = 3; i < CARD_H - 2; i += 2)
        rows[i] &= ~((0x8000 >> 1) | (0x8000 >> (CARD_W - 2)));
    DrawPic(rows, 16, CARD_H, x, y);
}

static void DrawMiniCard(const char *code, Coord x, Coord y)
{
    UInt16 rows[MINI_H];
    UInt8 rank, suit;

    if (!hs_card(code, &rank, &suit))
        rank = suit = HS_UNKNOWN;
    card_mini(rank, suit, rows);
    DrawPic(rows, MINI_W, MINI_H, x, y);
}

static void DrawMiniHalf(Coord x, Coord y)
{
    UInt16 rows[MINI_H];

    card_mini_half(rows);
    DrawPic(rows, MINI_HALF_W, MINI_H, x, y);
}

static void DrawChip(Coord x, Coord y, Boolean onFelt)
{
    UInt16 rows[8];
    UInt16 i;

    card_chip(rows);
    if (onFelt)
        for (i = 0; i < 8; i++)
            rows[i] = ~rows[i];
    DrawPic(rows, 7, 8, x, y);
}

/* Draws text cut to fit width, ending in "..." when it was cut. */
static void DrawFit(const char *text, Coord x, Coord y, Int16 width, Boolean inverted)
{
    Int16 w = width, len = StrLen(text);
    Boolean fits;
    char buf[96];

    FntCharsInWidth(text, &w, &len, &fits);
    if (!fits && len > 0) {
        w = width - FntCharsWidth("...", 3);
        len = StrLen(text);
        FntCharsInWidth(text, &w, &len, &fits);
        if (len > (Int16)sizeof(buf) - 4)
            len = sizeof(buf) - 4;
        MemMove(buf, text, len);
        StrCopy(buf + len, "...");
        text = buf;
        len += 3;
    }
    if (inverted)
        WinDrawInvertedChars(text, len, x, y);
    else
        WinDrawChars(text, len, x, y);
}

static void DrawRight(const char *text, Coord right, Coord y, Boolean inverted)
{
    Int16 len = StrLen(text), w = FntCharsWidth(text, len);

    if (inverted)
        WinDrawInvertedChars(text, len, right - w, y);
    else
        WinDrawChars(text, len, right - w, y);
}

/* "clyd bot" -> "Clyd Bot": the server lowercases everything. */
static void TitleCase(const char *in, char *out, UInt16 size)
{
    UInt16 i;
    Boolean start = true;

    for (i = 0; in[i] && i + 1 < size; i++) {
        char c = in[i];
        if (start && c >= 'a' && c <= 'z')
            c = c - 'a' + 'A';
        start = c == ' ' || c == '-';
        out[i] = c;
    }
    out[i] = '\0';
}

/* ---- The table -------------------------------------------------------- */

static Int16 SecondsLeft(void)
{
    Int32 left;

    if (!gMyTurn)
        return 0;
    left = (Int32)(gTurnEnd - TimGetTicks());
    if (left <= 0)
        return 0;
    return (Int16)((left + SysTicksPerSecond() - 1) / SysTicksPerSecond());
}

/* One opponent, in a 80 x 20 cell: name over chips and last move, with the
 * hand in mini cards on the right. */
static void DrawSeat(const HsPlayer *p, Boolean active, Coord x, Coord y)
{
    char text[24];
    Int16 textW = SEAT_W - 20, w;
    UInt8 cards = hs_card_count(p->hand);
    RectangleType r;

    if (cards >= 2 && p->hand[0] != '?') {
        DrawMiniCard(p->hand, x + SEAT_W - 23, y + 1);
        DrawMiniCard(p->hand + 2, x + SEAT_W - 12, y + 1);
        textW = SEAT_W - 25;
    } else if (cards >= 2) {
        /* Two backs: the Lynx half card peeking out from under a whole one. */
        DrawMiniHalf(x + SEAT_W - 18, y + 1);
        DrawMiniCard("??", x + SEAT_W - 12, y + 1);
    }

    FntSetFont(stdFont);
    /* The second line first: the name's cell then keeps its descenders. */
    StrPrintF(text, "%u", p->purse);
    w = FntCharsWidth(text, StrLen(text));
    WinDrawChars(text, StrLen(text), x + 2, y + 9);
    if (p->move[0])
        TitleCase(p->move, text, sizeof(text));
    else if (p->status == HS_WAITING)
        StrCopy(text, "Waits");
    else
        text[0] = '\0';
    if (text[0])
        DrawFit(text, x + w + 5, y + 9, textW - w - 6, false);

    TitleCase(p->name, text, sizeof(text));
    if (active) {
        FntSetFont(boldFont);
        r.topLeft.x = x;
        r.topLeft.y = y;
        r.extent.x = textW;
        r.extent.y = 10;
        WinDrawRectangle(&r, 2);
        DrawFit(text, x + 2, y - 1, textW - 3, true);
        FntSetFont(stdFont);
    } else {
        DrawFit(text, x + 2, y - 1, textW - 3, false);
    }
}

/* The others in two columns, spread down the space above the felt. */
static void DrawSeats(void)
{
    UInt16 i, k = 0, first = gGame.viewing ? 0 : 1, rows, rowH;

    rows = (gGame.playerCount - first + 1) / 2;
    if (gGame.playerCount <= first || rows == 0) {
        FntSetFont(stdFont);
        WinDrawChars("Waiting for other players...", 28, 4, 34);
        return;
    }
    if (rows > 4)
        rows = 4;
    rowH = SEATS_H / rows;
    if (rowH > 26)
        rowH = 26;
    for (i = first; i < gGame.playerCount && k < 8; i++, k++)
        DrawSeat(&gGame.players[i], gGame.activePlayer == (Int16)i, (k % 2) * SEAT_W,
                 SEATS_Y + (SEATS_H - rows * rowH) / 2 + (k / 2) * rowH + (rowH - SEAT_H) / 2);
}

static void DrawFelt(void)
{
    RectangleType r;
    char text[16];
    UInt16 i, n = hs_card_count(gGame.community);
    const HsPlayer *me = &gGame.players[0];

    r.topLeft.x = 0;
    r.topLeft.y = FELT_Y;
    r.extent.x = 160;
    r.extent.y = FELT_H;
    WinDrawRectangle(&r, 5);

    for (i = 0; i < 5; i++) {
        if (i < n)
            DrawTableCard(gGame.community + 2 * i, BOARD_X + i * CARD_STEP, FELT_Y + 2);
        else
            DrawEmptySlot(BOARD_X + i * CARD_STEP, FELT_Y + 2);
    }
    if (!gGame.viewing && hs_card_count(me->hand) >= 2) {
        DrawTableCard(me->hand, HOLE_X, FELT_Y + 2);
        DrawTableCard(me->hand + 2, HOLE_X + CARD_STEP, FELT_Y + 2);
    } else {
        DrawEmptySlot(HOLE_X, FELT_Y + 2);
        DrawEmptySlot(HOLE_X + CARD_STEP, FELT_Y + 2);
    }

    FntSetFont(stdFont);
    DrawFit(hs_street(gGame.round), PANEL_X + 1, FELT_Y + 1, 158 - PANEL_X, true);
    DrawChip(PANEL_X + 1, FELT_Y + 13, true);
    WinDrawInvertedChars("Pot", 3, PANEL_X + 10, FELT_Y + 11);
    FntSetFont(boldFont);
    StrPrintF(text, "%u", gGame.pot);
    WinDrawInvertedChars(text, StrLen(text), PANEL_X + 1, FELT_Y + 20);
    FntSetFont(stdFont);
}

static void DrawInfo(void)
{
    char text[48], name[HS_NAME_LEN];
    const HsPlayer *me = &gGame.players[0];
    Int16 x;

    FntSetFont(stdFont);
    if (gGame.viewing) {
        WinDrawChars("Watching: the table is full.", 28, 2, INFO_Y);
        return;
    }
    if (gGame.playerCount == 0)
        return;
    TitleCase(me->name, name, sizeof(name));
    FntSetFont(boldFont);
    WinDrawChars(name, StrLen(name), 2, INFO_Y);
    x = 6 + FntCharsWidth(name, StrLen(name));
    FntSetFont(stdFont);
    StrPrintF(text, "%u", me->purse);
    DrawChip(x, INFO_Y + 2, false);
    WinDrawChars(text, StrLen(text), x + 9, INFO_Y);
    x += 13 + FntCharsWidth(text, StrLen(text));
    if (me->bet > 0) {
        StrPrintF(text, "bet %u", me->bet);
        WinDrawChars(text, StrLen(text), x, INFO_Y);
    }
    if (me->status == HS_FOLDED)
        DrawRight("Folded", 158, INFO_Y, false);
    else if (me->status == HS_WAITING)
        DrawRight("Next hand", 158, INFO_Y, false);
    else if (me->move[0]) {
        TitleCase(me->move, text, sizeof(text));
        DrawRight(text, 158, INFO_Y, false);
    }
}

static void StatusText(char *out)
{
    char name[HS_NAME_LEN];
    Int16 left;

    if (gNotice[0]) {
        StrCopy(out, gNotice);
    } else if (gMyTurn) {
        left = SecondsLeft();
        StrPrintF(out, "Your move!  %d s", left);
    } else if (gGame.activePlayer > 0 && gGame.activePlayer < gGame.playerCount) {
        TitleCase(gGame.players[gGame.activePlayer].name, name, sizeof(name));
        StrPrintF(out, "Waiting on %s...", name);
    } else if (gGame.lastResult[0]) {
        StrNCopy(out, gGame.lastResult, 80);
        out[80] = '\0';
        if (out[0] >= 'a' && out[0] <= 'z')
            out[0] = out[0] - 'a' + 'A';
    } else if (gGame.round == 0) {
        StrCopy(out, "Waiting for the next hand...");
    } else {
        out[0] = '\0';
    }
}

static void DrawStatus(void)
{
    char text[84];

    StatusText(text);
    FntSetFont(gMyTurn && !gNotice[0] ? boldFont : stdFont);
    DrawFit(text, 2, STATUS_Y, 156, false);
    FntSetFont(stdFont);
}

/* The move buttons: one per move the server offers, word over amount. */
static void ButtonRect(Int16 i, Int16 n, RectangleType *r)
{
    Int16 w = (160 - (n - 1) * 2) / n;

    r->topLeft.x = i * (w + 2);
    r->topLeft.y = BAR_Y + 1;
    r->extent.x = (i == n - 1) ? 160 - r->topLeft.x : w;
    r->extent.y = BAR_H - 2;
}

static void DrawButton(Int16 i, Int16 n, const char *label, const char *amount, Boolean lit)
{
    RectangleType r;
    Int16 len;

    ButtonRect(i, n, &r);
    r.topLeft.x += 1;
    r.topLeft.y += 1;
    r.extent.x -= 2;
    r.extent.y -= 2;
    WinEraseRectangle(&r, 3);
    WinDrawRectangleFrame(roundFrame, &r);
    FntSetFont(boldFont);
    len = StrLen(label);
    if (amount[0] == '\0') {
        WinDrawChars(label, len, r.topLeft.x + (r.extent.x - FntCharsWidth(label, len)) / 2,
                     r.topLeft.y + 4);
    } else {
        WinDrawChars(label, len, r.topLeft.x + (r.extent.x - FntCharsWidth(label, len)) / 2,
                     r.topLeft.y - 1);
        FntSetFont(stdFont);
        len = StrLen(amount);
        WinDrawChars(amount, len, r.topLeft.x + (r.extent.x - FntCharsWidth(amount, len)) / 2,
                     r.topLeft.y + 9);
    }
    FntSetFont(stdFont);
    if (lit)
        WinInvertRectangle(&r, 3);
}

static void DrawButtons(void)
{
    char label[HS_MOVE_NAME_LEN], amount[HS_MOVE_NAME_LEN];
    Int16 i, n = gGame.moveCount;

    for (i = 0; i < n; i++) {
        hs_split_move(gGame.moves[i].name, label, sizeof(label), amount, sizeof(amount));
        DrawButton(i, n, label, amount, i == gCursor);
    }
}

/* The Leave button sits alone at the right of the bar between turns. */
static void LeaveRect(RectangleType *r)
{
    r->topLeft.x = 124;
    r->topLeft.y = BAR_Y + 2;
    r->extent.x = 35;
    r->extent.y = BAR_H - 4;
}

static void DrawLeave(void)
{
    RectangleType r;

    LeaveRect(&r);
    WinEraseRectangle(&r, 3);
    WinDrawRectangleFrame(roundFrame, &r);
    FntSetFont(boldFont);
    WinDrawChars("Leave", 5, r.topLeft.x + (r.extent.x - FntCharsWidth("Leave", 5)) / 2,
                 r.topLeft.y + 4);
    FntSetFont(stdFont);
}

static void DrawTable(void)
{
    RectangleType r;
    WinHandle old = NULL;

    r.topLeft.x = 0;
    r.topLeft.y = 0;
    r.extent.x = 160;
    r.extent.y = 160;
    if (gOffscreen)
        old = WinSetDrawWindow(gOffscreen);
    WinEraseRectangle(&r, 0);

    if (!gHaveGame) {
        FntSetFont(boldFont);
        WinDrawChars("FI Texas Hold'em", 16, 34, 40);
        FntSetFont(stdFont);
        DrawFit(gNotice[0] ? gNotice : "Taking a seat...", 4, 60, 152, false);
        DrawFelt();
    } else {
        DrawSeats();
        DrawFelt();
        DrawInfo();
        DrawStatus();
        WinDrawLine(0, BAR_Y - 1, 159, BAR_Y - 1);
        if (gMyTurn)
            DrawButtons();
        else {
            char text[24];
            FntSetFont(stdFont);
            StrPrintF(text, "Table: %s", gPrefs.table);
            DrawFit(text, 2, BAR_Y + 6, 118, false);
            DrawLeave();
        }
    }

    if (gOffscreen) {
        WinSetDrawWindow(old);
        WinCopyRectangle(gOffscreen, NULL, &r, 0, 0, winPaint);
    }
}

/* Just the status line, for the countdown. */
static void RedrawStatus(void)
{
    RectangleType r;

    r.topLeft.x = 0;
    r.topLeft.y = STATUS_Y;
    r.extent.x = 160;
    r.extent.y = 11;
    WinEraseRectangle(&r, 0);
    DrawStatus();
}

/* ---- Play ------------------------------------------------------------- */

static void AfterState(void)
{
    Boolean wasTurn = gMyTurn;

    gMyTurn = !gGame.viewing && gGame.activePlayer == 0 && gGame.moveCount > 0 &&
              gGame.moveTime > 0;
    if (gMyTurn) {
        gTurnEnd = TimGetTicks() + (UInt32)gGame.moveTime * SysTicksPerSecond();
        if (!wasTurn) {
            UInt16 i;
            /* Start on check, or else call. */
            gCursor = 0;
            for (i = 0; i < gGame.moveCount; i++)
                if (StrCompare(gGame.moves[i].code, "ch") == 0 ||
                    (StrCompare(gGame.moves[i].code, "ca") == 0 && gCursor == 0))
                    gCursor = i;
            SndPlaySystemSound(sndConfirmation);
        }
        /* No polling while the move is ours: nothing changes until it is
         * made, or the clock runs out and the server makes it. */
        gNextPoll = gTurnEnd;
    } else {
        gNextPoll = TimGetTicks() + POLL_GAP_TICKS;
    }
}

static void PollFailed(Err err)
{
    char detail[96];

    gFailures++;
    if (gFailures >= MAX_FAILURES) {
        /* Not worth a leave the server would not hear either. */
        gSeated = false;
        gMyTurn = false;
        ShowError("Lost the table.", err);
        FrmGotoForm(SetupForm);
        return;
    }
    ErrorText(err, detail);
    StrPrintF(gNotice, "Retrying (%u)... ", gFailures);
    StrNCat(gNotice, detail, sizeof(gNotice) - 1);
    gNextPoll = TimGetTicks() + POLL_GAP_TICKS * 2;
    DrawTable();
}

static void Poll(void)
{
    Err err = StartGameCall(REQ_STATE, "state");

    if (err != errNone)
        PollFailed(err);
}

static void PollDone(Err err)
{
    err = GameReply(err);
    if (err == heErrNoTable && !gHaveGame) {
        /* A table that does not exist is not worth retrying. */
        gSeated = false;
        ShowError("Could not sit down.", err);
        FrmGotoForm(SetupForm);
        return;
    }
    if (err != errNone) {
        PollFailed(err);
        return;
    }
    gFailures = 0;
    gNotice[0] = '\0';
    AfterState();
    DrawTable();
}

static void MoveFailed(void)
{
    StrCopy(gNotice, "Move not sent. Tap it again.");
    DrawTable();
}

static void PlayMove(Int16 i)
{
    char path[12];

    if (!gMyTurn || gReq != REQ_NONE || i < 0 || i >= gGame.moveCount)
        return;
    StrCopy(path, "move/");
    StrCat(path, gGame.moves[i].code);
    StrCopy(gNotice, "Sending your move...");
    RedrawStatus();
    if (StartGameCall(REQ_MOVE, path) != errNone)
        MoveFailed();
}

static void MoveDone(Err err)
{
    gNotice[0] = '\0';
    if (GameReply(err) != errNone) {
        MoveFailed();
        return;
    }
    gMyTurn = false;
    AfterState();
    DrawTable();
}

/* The Leave button: gives the seat back, then goes to the setup form
 * whether or not the server heard. */
static void BeginLeave(void)
{
    if (!gSeated || gReq == REQ_LEAVE)
        return;
    gMyTurn = false;
    StrCopy(gNotice, "Leaving the table...");
    DrawTable();
    if (gFailures >= MAX_FAILURES || StartGameCall(REQ_LEAVE, "leave") != errNone) {
        gSeated = false;
        FrmGotoForm(SetupForm);
    }
}

static void LeaveDone(void)
{
    gSeated = false;
    FrmGotoForm(SetupForm);
}

/* Gives the seat back to the server as the app closes, best effort. */
static void LeaveTable(void)
{
    if (!gSeated)
        return;
    gSeated = false;
    gMyTurn = false;
    if (gFailures < MAX_FAILURES && StartGameCall(REQ_LEAVE, "leave") == errNone)
        FinishGet();
}

static Boolean InRect(const RectangleType *r, Coord x, Coord y)
{
    return x >= r->topLeft.x && x < r->topLeft.x + r->extent.x &&
           y >= r->topLeft.y && y < r->topLeft.y + r->extent.y;
}

/* Tracks the pen in a button as a control would; true if let go inside. */
static Boolean TrackPen(const RectangleType *r)
{
    Coord x, y;
    Boolean down = true, inside = true, wasInside = true;

    WinInvertRectangle(r, 3);
    while (down) {
        EvtGetPen(&x, &y, &down);
        inside = InRect(r, x, y);
        if (inside != wasInside) {
            WinInvertRectangle(r, 3);
            wasInside = inside;
        }
    }
    if (wasInside)
        WinInvertRectangle(r, 3);
    return inside;
}

static void ShowResult(void)
{
    char text[HS_RESULT_LEN];

    if (!gHaveGame || gGame.lastResult[0] == '\0') {
        FrmCustomAlert(ResultAlert, "No hand has finished yet.", "", "");
        return;
    }
    StrCopy(text, gGame.lastResult);
    if (text[0] >= 'a' && text[0] <= 'z')
        text[0] = text[0] - 'a' + 'A';
    FrmCustomAlert(ResultAlert, text, "", "");
}

static Boolean TablePen(Coord x, Coord y)
{
    RectangleType r;
    Int16 i, n;

    if (!gHaveGame)
        return false;
    if (gMyTurn) {
        n = gGame.moveCount;
        for (i = 0; i < n; i++) {
            ButtonRect(i, n, &r);
            if (InRect(&r, x, y)) {
                r.topLeft.x += 1;
                r.topLeft.y += 1;
                r.extent.x -= 2;
                r.extent.y -= 2;
                if (i == gCursor)
                    WinInvertRectangle(&r, 3); /* unlit, so tracking shows */
                if (TrackPen(&r)) {
                    gCursor = i;
                    PlayMove(i);
                } else if (i == gCursor) {
                    WinInvertRectangle(&r, 3);
                }
                return true;
            }
        }
    } else {
        LeaveRect(&r);
        if (InRect(&r, x, y)) {
            if (TrackPen(&r))
                BeginLeave();
            return true;
        }
    }
    r.topLeft.x = 0;
    r.topLeft.y = STATUS_Y;
    r.extent.x = 160;
    r.extent.y = 11;
    if (InRect(&r, x, y)) {
        ShowResult();
        return true;
    }
    return false;
}

/* Scroll keys pick a move; an app button or Graffiti letter plays one. */
static Boolean TableKey(WChar c)
{
    Int16 i;

    if (c == vchrPageUp || c == vchrPageDown) {
        if (gMyTurn && gGame.moveCount > 0) {
            gCursor += c == vchrPageUp ? -1 : 1;
            if (gCursor < 0)
                gCursor = gGame.moveCount - 1;
            if (gCursor >= gGame.moveCount)
                gCursor = 0;
            DrawTable();
        }
        return true;
    }
    if (c >= vchrHard1 && c <= vchrHard4) {
        if (gMyTurn)
            PlayMove(gCursor);
        return true;
    }
    if (!gMyTurn)
        return false;
    for (i = 0; i < gGame.moveCount; i++) {
        char first = gGame.moves[i].name[0];
        WChar lower = (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
        /* f fold, c check/call, b bet, r raise, a all-in: the first of each. */
        if (first == (char)lower) {
            gCursor = i;
            PlayMove(i);
            return true;
        }
    }
    return false;
}

static Boolean TableHandleEvent(EventType *event)
{
    switch (event->eType) {
    case frmOpenEvent:
        FrmDrawForm(FrmGetActiveForm());
        gSeated = true;
        gHaveGame = false;
        MemSet(&gGame, sizeof(gGame), 0);
        gMyTurn = false;
        gFailures = 0;
        StrCopy(gNotice, "Taking a seat...");
        DrawTable();
        gNotice[0] = '\0';
        gNextPoll = TimGetTicks();
        return true;
    case frmUpdateEvent:
        DrawTable();
        return true;
    case frmCloseEvent:
        /* Closing mid-leave (another app was launched) lets it finish. */
        if (gReq == REQ_LEAVE) {
            FinishGet();
            gSeated = false;
        }
        CancelGet();
        LeaveTable();
        return false;
    case nilEvent:
        break;
    case penDownEvent:
        return TablePen(event->screenX, event->screenY);
    case keyDownEvent:
        return TableKey(event->data.keyDown.chr);
    case menuEvent:
        switch (event->data.menu.itemID) {
        case TableMenuLeave:
            BeginLeave();
            return true;
        case TableMenuResult:
            ShowResult();
            return true;
        case TableMenuAbout:
            FrmAlert(AboutAlert);
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

/* Called on every event while seated: polls, and ticks the move clock. */
static void TableIdle(void)
{
    static Int16 lastShown = -1;
    Int16 left;

    if (!gSeated || FrmGetActiveFormID() != TableForm)
        return;
    EvtResetAutoOffTimer();
    if (gMyTurn) {
        left = SecondsLeft();
        if (left != lastShown) {
            lastShown = left;
            RedrawStatus();
        }
        if (left > 0)
            return;
        gMyTurn = false;
    }
    if (gReq == REQ_NONE && (Int32)(TimGetTicks() - gNextPoll) >= 0)
        Poll();
}

/* ---- Setup form ------------------------------------------------------- */

static void *GetObject(FormType *form, UInt16 id)
{
    return FrmGetObjectPtr(form, FrmGetObjectIndex(form, id));
}

static void SetFieldText(FormType *form, UInt16 id, const char *text)
{
    FieldType *field = GetObject(form, id);
    MemHandle old = FldGetTextHandle(field);
    MemHandle handle = MemHandleNew(StrLen(text) + 1);

    if (handle == NULL)
        return;
    StrCopy(MemHandleLock(handle), text);
    MemHandleUnlock(handle);
    FldSetTextHandle(field, handle);
    if (old)
        MemHandleFree(old);
}

static void GetFieldText(FormType *form, UInt16 id, char *out, UInt16 size)
{
    const char *text = FldGetTextPtr(GetObject(form, id));

    if (text == NULL)
        text = "";
    StrNCopy(out, text, size - 1);
    out[size - 1] = '\0';
}

static void SetStatus(const char *text)
{
    FormType *form = FrmGetFormPtr(SetupForm);
    FieldType *field;

    StrNCopy(gStatus, text, sizeof(gStatus) - 1);
    gStatus[sizeof(gStatus) - 1] = '\0';
    if (form == NULL)
        return;
    field = GetObject(form, SetupStatusField);
    FldSetTextPtr(field, gStatus);
    FldRecalculateField(field, false);
    if (FrmGetActiveForm() == form && FrmVisible(form))
        FldDrawField(field);
}

static void ReadSetup(FormType *form)
{
    char server[SERVER_LEN];

    GetFieldText(form, SetupNameField, gPrefs.name, sizeof(gPrefs.name));
    hs_clean_name(gPrefs.name);
    GetFieldText(form, SetupTableField, gPrefs.table, sizeof(gPrefs.table));
    GetFieldText(form, SetupServerField, server, sizeof(server));
    if (server[0] == '\0')
        StrCopy(server, DEFAULT_SERVER);
    /* A lobby address carries its table: take it, and show both split. */
    if (hs_split_server(server, gPrefs.table, sizeof(gPrefs.table))) {
        SetFieldText(form, SetupServerField, server);
        SetFieldText(form, SetupTableField, gPrefs.table);
        if (FrmVisible(form)) {
            FldDrawField(GetObject(form, SetupServerField));
            FldDrawField(GetObject(form, SetupTableField));
        }
    }
    StrCopy(gPrefs.server, server);
    PrefSetAppPreferences(CREATOR, PREFS_ID, PREFS_VERSION, &gPrefs, sizeof(gPrefs), true);
}

static void DrawTableRow(Int16 item, RectangleType *bounds, Char **unused)
{
    const HsTable *t;
    char name[HS_TABLE_NAME_LEN];
    Coord x = bounds->topLeft.x + 1, y = bounds->topLeft.y;
    Int16 right = bounds->topLeft.x + bounds->extent.x - 2, w;

    if (item < 0 || item >= gTables.count)
        return;
    t = &gTables.tables[item];
    TitleCase(t->name, name, sizeof(name));
    w = FntCharsWidth(t->seats, StrLen(t->seats));
    DrawFit(name, x, y, right - w - 6 - x, false);
    WinDrawChars(t->seats, StrLen(t->seats), right - w, y);
}

static void ShowTables(FormType *form)
{
    ListType *list = GetObject(form, SetupList);
    RectangleType r;
    Int16 i, sel = noListSelection;

    for (i = 0; i < gTables.count; i++)
        if (StrCaselessCompare(gTables.tables[i].id, gPrefs.table) == 0)
            sel = i;
    LstSetListChoices(list, NULL, gTables.count);
    LstSetDrawFunction(list, DrawTableRow);
    LstSetSelection(list, sel);
    if (FrmGetActiveForm() != form || !FrmVisible(form))
        return;
    FrmGetObjectBounds(form, FrmGetObjectIndex(form, SetupList), &r);
    WinEraseRectangle(&r, 0);
    LstDrawList(list);
}

static void TablesDone(Err err)
{
    FormType *form = FrmGetFormPtr(SetupForm);
    char text[48];

    if (err == errNone && !hs_parse_tables(gReply, gReplyLen, &gTables))
        err = heErrBadReply;
    if (err != errNone)
        gTables.count = 0;
    if (form)
        ShowTables(form);
    if (err != errNone) {
        SetStatus("No table list. Tap Refresh.");
        ShowError("Could not list the tables.", err);
        return;
    }
    StrPrintF(text, "%u tables. Pick one, Sit Down.", gTables.count);
    SetStatus(text);
}

/* Asks for the table list; TablesDone shows it when it comes. */
static void RefreshTables(FormType *form)
{
    Err err;

    ReadSetup(form);
    gTables.count = 0;
    ShowTables(form);
    SetStatus("Asking the server...");
    err = StartGet(REQ_TABLES, "tables", NULL, NULL);
    if (err != errNone)
        TablesDone(err);
}

static void SitDown(FormType *form)
{
    ReadSetup(form);
    SetFieldText(form, SetupNameField, gPrefs.name);
    FldDrawField(GetObject(form, SetupNameField));
    if (gPrefs.name[0] == '\0') {
        FrmCustomAlert(MessageAlert, "Enter a name to play under: ",
                       "letters and digits, up to 8.", "");
        FrmSetFocus(form, FrmGetObjectIndex(form, SetupNameField));
        return;
    }
    if (gPrefs.table[0] == '\0') {
        FrmCustomAlert(MessageAlert, "Pick a table from the list, ",
                       "or type its name.", "");
        return;
    }
    FrmGotoForm(TableForm);
}

static Boolean SetupHandleEvent(EventType *event)
{
    FormType *form = FrmGetActiveForm();
    EventType fetch;

    switch (event->eType) {
    case frmOpenEvent:
        SetFieldText(form, SetupNameField, gPrefs.name);
        SetFieldText(form, SetupServerField, gPrefs.server);
        SetFieldText(form, SetupTableField, gPrefs.table);
        FldSetTextPtr(GetObject(form, SetupStatusField), gStatus);
        ShowTables(form);
        FrmDrawForm(form);
        FrmSetFocus(form, FrmGetObjectIndex(form, SetupNameField));
        if (gTables.count == 0) {
            MemSet(&fetch, sizeof(fetch), 0);
            fetch.eType = fetchEvent;
            EvtAddEventToQueue(&fetch);
        }
        return true;
    case frmUpdateEvent:
        FrmDrawForm(form);
        return true;
    case frmCloseEvent:
        if (gReq == REQ_TABLES)
            CancelGet();
        ReadSetup(form);
        return false;
    case fetchEvent:
        RefreshTables(form);
        return true;
    case lstSelectEvent:
        if (event->data.lstSelect.listID == SetupList) {
            Int16 i = event->data.lstSelect.selection;
            if (i >= 0 && i < gTables.count) {
                SetFieldText(form, SetupTableField, gTables.tables[i].id);
                FldDrawField(GetObject(form, SetupTableField));
            }
            return true;
        }
        break;
    case ctlSelectEvent:
        switch (event->data.ctlSelect.controlID) {
        case SetupRefreshButton:
            RefreshTables(form);
            return true;
        case SetupSitButton:
            SitDown(form);
            return true;
        }
        break;
    case menuEvent:
        switch (event->data.menu.itemID) {
        case SetupMenuRefresh:
            RefreshTables(form);
            return true;
        case SetupMenuDefault:
            SetFieldText(form, SetupServerField, DEFAULT_SERVER);
            FldDrawField(GetObject(form, SetupServerField));
            return true;
        case SetupMenuAbout:
            FrmAlert(AboutAlert);
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

/* ---- Application ------------------------------------------------------ */

static Boolean AppHandleEvent(EventType *event)
{
    FormType *form;

    if (event->eType != frmLoadEvent)
        return false;
    form = FrmInitForm(event->data.frmLoad.formID);
    FrmSetActiveForm(form);
    if (event->data.frmLoad.formID == TableForm)
        FrmSetEventHandler(form, TableHandleEvent);
    else
        FrmSetEventHandler(form, SetupHandleEvent);
    return true;
}

/* While seated, the app buttons play moves instead of launching apps. */
static Boolean IsTableHardKey(const EventType *event)
{
    WChar c = event->data.keyDown.chr;

    return gSeated && event->eType == keyDownEvent &&
           (event->data.keyDown.modifiers & commandKeyMask) &&
           ((c >= vchrHard1 && c <= vchrHard4) || c == vchrPageUp || c == vchrPageDown) &&
           FrmGetActiveFormID() == TableForm;
}

/* Moves the GET in flight on by one exchange when it is due, and hands a
 * finished reply to its form. */
static void NetIdle(void)
{
    UInt8 req = gReq;
    Err err;

    if (req == REQ_NONE || (Int32)(TimGetTicks() - gNextStep) < 0 || !StepGet(&err))
        return;
    switch (req) {
    case REQ_TABLES:
        TablesDone(err);
        break;
    case REQ_STATE:
        PollDone(err);
        break;
    case REQ_MOVE:
        MoveDone(err);
        break;
    case REQ_LEAVE:
        LeaveDone();
        break;
    }
}

static void EventLoop(void)
{
    EventType event;
    Err err;

    do {
        EvtGetEvent(&event, gReq != REQ_NONE ? READ_POLL_TICKS :
                            gSeated ? TICK_TICKS : evtWaitForever);
        if (IsTableHardKey(&event)) {
            TableKey(event.data.keyDown.chr);
            NetIdle();
            TableIdle();
            continue;
        }
        if (SysHandleEvent(&event))
            continue;
        if (MenuHandleEvent(0, &event, &err))
            continue;
        if (!AppHandleEvent(&event))
            FrmDispatchEvent(&event);
        if (event.eType != appStopEvent) {
            NetIdle();
            TableIdle();
        }
    } while (event.eType != appStopEvent);
}

/* Palm OS 3.1, the oldest release the app is tested on. */
static Boolean RomCompatible(void)
{
    UInt32 rom = 0;

    FtrGet(sysFtrCreator, sysFtrNumROMVersion, &rom);
    return rom >= sysMakeROMVersion(3, 1, 0, sysROMStageRelease, 0);
}

static void LoadPrefs(void)
{
    UInt16 size = sizeof(gPrefs);

    if (PrefGetAppPreferences(CREATOR, PREFS_ID, &gPrefs, &size, true) != PREFS_VERSION ||
        size != sizeof(gPrefs)) {
        MemSet(&gPrefs, sizeof(gPrefs), 0);
        StrCopy(gPrefs.server, DEFAULT_SERVER);
        StrCopy(gPrefs.table, DEFAULT_TABLE);
    }
    gPrefs.name[HS_NAME_LEN - 1] = '\0';
    gPrefs.table[HS_TABLE_ID_LEN - 1] = '\0';
    gPrefs.server[SERVER_LEN - 1] = '\0';
}

static Err AppStart(void)
{
    Err err;

    LoadPrefs();
    gStatus[0] = '\0';
    gOffscreen = WinCreateOffscreenWindow(160, 160, screenFormat, &err);
    return errNone;
}

static void AppStop(void)
{
    FrmCloseAllForms(); /* the table form's close gives up the seat */
    LeaveTable();
    CancelGet();
    fn_shutdown();
    if (gOffscreen)
        WinDeleteWindow(gOffscreen, false);
    gOffscreen = NULL;
}

UInt32 PilotMain(UInt16 cmd, MemPtr cmdPBP, UInt16 launchFlags)
{
    Err err;

    if (cmd != sysAppLaunchCmdNormalLaunch)
        return 0;
    if (!RomCompatible()) {
        FrmAlert(RomIncompatibleAlert);
        return 0;
    }
    err = AppStart();
    if (err != errNone) {
        ShowError("Could not open the cradle port.", err);
        AppStop();
        return 0;
    }
    FrmGotoForm(SetupForm);
    EventLoop();
    AppStop();
    return 0;
}
