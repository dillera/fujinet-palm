/* Bounce World for Palm OS.
 *
 * A client for Mark Fisher's bounce-world server, ported from
 * bounce-world-client. It reaches the server's framed TCP port through
 * FujiNet's N1: device, registers as a 40x36 character screen, and polls for
 * the shapes in its part of the world, drawing each character as a 4x4 cell.
 *
 * Every request is a line ending in LF; every reply is a 2-byte
 * little-endian total size followed by the payload.
 */
#include <PalmOS.h>

#include "fnnet.h"
#include "BounceWorldRsc.h"

#define CREATOR APP_CREATOR
#define PREFS_ID 1
#define PREFS_VERSION 1

#define NET_UNIT 1
#define CLIENT_VERSION "2"

#define COLS 40
#define ROWS 36
#define CELL 4
#define WORLD_W (COLS * CELL)
#define WORLD_H (ROWS * CELL)

#define HOST_LEN 48
#define PORT_LEN 6
#define NAME_LEN 8
#define VIEW_SIZE 768   /* step, status, count and 3 bytes per shape */
#define REPLY_SIZE 640  /* the largest other reply is the shape data */
#define SHAPE_BYTES 512
#define MAX_SHAPES 50
#define MAX_WHO 12
#define MESSAGE_LEN 120
#define LINE_LEN 64

#define POLL_TICKS (SysTicksPerSecond() / 20)
#define REPLY_TICKS (SysTicksPerSecond() * 3)
#define DRAIN_QUIET_TICKS (SysTicksPerSecond() / 2)
#define DRAIN_MAX_TICKS (SysTicksPerSecond() * 3)
#define MAX_FAILURES 5

/* Status bits in each world reply. */
#define CLIENT_CHANGE 1
#define OBJECT_CHANGE 2
#define FROZEN_TOGGLE 4
#define CLIENT_CMD    8

#define bwErrClosed  (appErrorClass | 0x10)
#define bwErrTimeout (appErrorClass | 0x11)
#define bwErrReply   (appErrorClass | 0x12)
#define bwErrDropped (appErrorClass | 0x13)

typedef struct {
    char host[HOST_LEN];
    char port[PORT_LEN];
    char name[NAME_LEN + 1];
} BwPrefs;

typedef struct {
    UInt16 width;
    UInt16 height;
    UInt8 bodies[5];
    UInt8 clients;
    UInt8 frozen;
} BwWorld;

static BwPrefs gPrefs;
static BwWorld gWorld;
static UInt8 gView[VIEW_SIZE];
static UInt16 gViewLen;
static UInt8 gReply[REPLY_SIZE];
static UInt16 gReplyLen;

static UInt8 gShapeBytes[SHAPE_BYTES];
static UInt16 gShapeAt[MAX_SHAPES];
static UInt8 gShapeWidth[MAX_SHAPES];
static UInt8 gShapeCount;

static char gWho[MAX_WHO][NAME_LEN];
static UInt8 gWhoCount;
static char gMessage[MESSAGE_LEN + 1];

static UInt8 gClientId;
static Boolean gConnected;
static UInt8 gStep;
static UInt8 gFailures;
static Boolean gShowInfo, gShowWho, gShowMessage, gDark;
static WinHandle gOffscreen;
static char gTitle[32];

static void ShowError(const char *what, Err err)
{
    char detail[80];

    if (err == fnErrNoReply)
        StrCopy(detail, "\nNo answer from FujiNet. Is the cradle cabled to it?");
    else if (err == fnErrRefused)
        StrCopy(detail, "\nFujiNet refused; check the server name and port.");
    else if (err == bwErrClosed)
        StrCopy(detail, "\nThe server closed the connection.");
    else if (err == bwErrTimeout)
        StrCopy(detail, "\nThe server did not answer.");
    else if (err == bwErrReply)
        StrCopy(detail, "\nThe server sent an unexpected reply.");
    else if (err == bwErrDropped)
        StrCopy(detail, "\nThe server no longer knows this client.");
    else
        StrPrintF(detail, "\nError %x.", err);
    FrmCustomAlert(MessageAlert, what, detail, "");
}

/* ---- Server protocol ------------------------------------------------- */

static Err Send(const char *line)
{
    char buf[LINE_LEN];
    UInt16 len = StrLen(line);

    if (len + 1 > sizeof(buf))
        return fnErrTooBig;
    StrCopy(buf, line);
    buf[len] = 0x0A;
    return FnNetWrite(NET_UNIT, buf, len + 1);
}

/* Reads exactly len bytes, sizing each READ by what FujiNet has waiting. */
static Err ReadExact(UInt8 *buf, UInt16 len, UInt32 deadline)
{
    FnNetStatus status;
    UInt16 total = 0, want, got;
    Err err;

    while (total < len) {
        if ((err = FnNetGetStatus(NET_UNIT, &status)) != errNone)
            return err;
        if (status.avail == 0) {
            if (!status.connected)
                return bwErrClosed;
            if (TimGetTicks() > deadline)
                return bwErrTimeout;
            SysTaskDelay(1);
            continue;
        }
        want = len - total;
        if (want > status.avail)
            want = status.avail;
        if ((err = FnNetRead(NET_UNIT, buf + total, want, &got)) != errNone)
            return err;
        total += got;
    }
    return errNone;
}

/* Reads one framed reply into buf, dropping whatever does not fit. */
static Err ReadReply(UInt8 *buf, UInt16 max, UInt16 *len)
{
    UInt32 deadline = TimGetTicks() + REPLY_TICKS;
    UInt8 header[2], skip[32];
    UInt16 size, keep, n;
    Err err;

    *len = 0;
    if ((err = ReadExact(header, 2, deadline)) != errNone)
        return err;
    size = header[0] | ((UInt16)header[1] << 8);
    if (size < 2)
        return bwErrReply;
    size -= 2;
    keep = size < max ? size : max;
    if ((err = ReadExact(buf, keep, deadline)) != errNone)
        return err;
    for (size -= keep; size > 0; size -= n) {
        n = size < sizeof(skip) ? size : sizeof(skip);
        if ((err = ReadExact(skip, n, deadline)) != errNone)
            return err;
    }
    *len = keep;
    return errNone;
}

/* After a lost READ or WRITE the stream may hold part of a reply; reading
 * until it stays empty puts the next request back in step. */
static void Drain(void)
{
    UInt32 start = TimGetTicks(), quiet = start + DRAIN_QUIET_TICKS;
    FnNetStatus status;
    UInt8 skip[64];
    UInt16 got;

    while (TimGetTicks() < quiet && TimGetTicks() - start < DRAIN_MAX_TICKS) {
        if (FnNetGetStatus(NET_UNIT, &status) != errNone || !status.connected)
            return;
        if (status.avail == 0) {
            SysTaskDelay(POLL_TICKS);
            continue;
        }
        FnNetRead(NET_UNIT, skip, status.avail < sizeof(skip) ? status.avail : sizeof(skip),
                  &got);
        quiet = TimGetTicks() + DRAIN_QUIET_TICKS;
    }
}

/* Sends a command and reads its reply. READ and WRITE go out once (see
 * fnnet.h), so a lost one fails the request instead of repeating it; the
 * world poll simply runs again, and a lost command is left for the user to
 * repeat rather than risk doing it twice. */
static Err RequestInto(const char *line, UInt8 *buf, UInt16 max, UInt16 *len)
{
    Err err = Send(line);

    if (err == errNone)
        err = ReadReply(buf, max, len);
    if (err != errNone && err != bwErrClosed)
        Drain();
    return err;
}

static Err Request(const char *line)
{
    return RequestInto(line, gReply, sizeof(gReply), &gReplyLen);
}

static Err RequestForClient(const char *verb)
{
    char line[LINE_LEN];

    StrPrintF(line, "%s %d", verb, gClientId);
    return Request(line);
}

/* Shape records are id, width, then width x width characters; like the
 * upstream client, shapes are addressed by their position in the list. */
static Err LoadShapes(void)
{
    UInt16 at = 0, used = 0, size, i;
    UInt8 count, width;
    Err err;

    if ((err = Request("x-shape-count")) != errNone)
        return err;
    if (gReplyLen < 1)
        return bwErrReply;
    count = gReply[0] < MAX_SHAPES ? gReply[0] : MAX_SHAPES;
    if ((err = Request("x-shape-data")) != errNone)
        return err;
    gShapeCount = 0;
    for (i = 0; i < count && at + 2 <= gReplyLen; i++) {
        width = gReply[at + 1];
        size = (UInt16)width * width;
        if (at + 2 + size > gReplyLen || used + size > SHAPE_BYTES)
            break;
        MemMove(gShapeBytes + used, gReply + at + 2, size);
        gShapeAt[i] = used;
        gShapeWidth[i] = width;
        used += size;
        at += 2 + size;
        gShapeCount++;
    }
    return errNone;
}

static Err Register(void)
{
    char line[LINE_LEN];
    Err err;

    StrPrintF(line, "x-add-client %s,%s,%d,%d", gPrefs.name, CLIENT_VERSION, COLS, ROWS);
    if ((err = Request(line)) != errNone)
        return err;
    if (gReplyLen < 1 || gReply[0] == 0)
        return bwErrReply;
    gClientId = gReply[0];
    return errNone;
}

/* Width, height and body count (16-bit little-endian), the five body
 * counts by size, clients, frozen and wrapped. */
static Err LoadWorld(void)
{
    Err err = Request("x-ws");

    if (err != errNone)
        return err;
    if (gReplyLen < 14)
        return bwErrReply;
    gWorld.width = gReply[0] | ((UInt16)gReply[1] << 8);
    gWorld.height = gReply[2] | ((UInt16)gReply[3] << 8);
    MemMove(gWorld.bodies, gReply + 6, 5);
    gWorld.clients = gReply[11];
    gWorld.frozen = gReply[12];
    return errNone;
}

/* Client names, each padded to 8 characters. */
static Err LoadWho(void)
{
    Err err = Request("x-who");
    UInt16 n;

    if (err != errNone)
        return err;
    n = gReplyLen / NAME_LEN;
    gWhoCount = n < MAX_WHO ? n : MAX_WHO;
    MemMove(gWho, gReply, gWhoCount * NAME_LEN);
    return errNone;
}

static Err LoadMessage(void)
{
    Err err = Request("x-msg");
    UInt16 n;

    if (err != errNone)
        return err;
    n = gReplyLen < MESSAGE_LEN ? gReplyLen : MESSAGE_LEN;
    MemMove(gMessage, gReply, n);
    gMessage[n] = '\0';
    return errNone;
}

/* Commands queued for this client by the server's operator. */
static Err RunCommands(void)
{
    UInt8 commands[16];
    UInt16 n, i;
    Err err = RequestForClient("x-cmd-get");

    if (err != errNone)
        return err;
    n = gReplyLen < sizeof(commands) ? gReplyLen : sizeof(commands);
    MemMove(commands, gReply, n);
    for (i = 0; i < n && err == errNone; i++) {
        switch (commands[i]) {
        case 1: gDark = true; break;
        case 2: gDark = false; break;
        case 3: gShowWho = true; break;
        case 4: gShowWho = false; break;
        case 5: err = LoadMessage(); gShowMessage = true; break;
        case 6: gShowMessage = false; break;
        case 7: gShowInfo = true; break;
        case 8: gShowInfo = false; break;
        }
    }
    return err;
}

static void Disconnect(void)
{
    char line[LINE_LEN];

    if (!gConnected)
        return;
    if (gClientId != 0) {
        StrPrintF(line, "close %d", gClientId);
        Send(line);
    }
    FnNetClose(NET_UNIT);
    gConnected = false;
    gClientId = 0;
}

static Err Connect(void)
{
    char url[HOST_LEN + PORT_LEN + 16];
    Err err;

    StrPrintF(url, "N1:TCP://%s:%s/", gPrefs.host, gPrefs.port);
    if ((err = FnNetOpen(NET_UNIT, url, FN_NET_READ_WRITE, FN_NET_TRANS_NONE)) != errNone)
        return err;
    gConnected = true;
    gClientId = 0;
    if ((err = LoadShapes()) == errNone && (err = Register()) == errNone &&
        (err = LoadWorld()) == errNone)
        err = LoadWho();
    if (err != errNone) {
        Disconnect();
        return err;
    }
    gViewLen = 0;
    gStep = 0;
    gFailures = 0;
    gShowInfo = gShowWho = gShowMessage = gDark = false;
    return errNone;
}

/* ---- Drawing --------------------------------------------------------- */

/* Block characters as quadrants: 1 top left, 2 top right, 4 bottom left,
 * 8 bottom right. */
static UInt8 Quadrants(char c)
{
    static const UInt8 quads[] = {
        5, 10, 12, 3, 4, 8, 1, 2, 11, 7, 14, 13, 15, 9, 0, 6 /* a..p */
    };
    return c >= 'a' && c <= 'p' ? quads[c - 'a'] : 0;
}

/* Box-drawing characters as arms from the centre: 1 up, 2 down, 4 left,
 * 8 right. */
static UInt8 Arms(char c)
{
    switch (c) {
    case 'r': return 2 | 8;
    case ')': return 2 | 4;
    case 'L': return 1 | 8;
    case '!': return 1 | 4;
    case 'J': return 1 | 2 | 4;
    case 't': return 1 | 2 | 8;
    case 'T': return 2 | 4 | 8;
    case '2': return 1 | 4 | 8;
    case '|': return 1 | 2;
    case '-': return 4 | 8;
    case '+': return 15;
    }
    return 0;
}

static void FillCell(Coord x, Coord y, Coord w, Coord h)
{
    RectangleType r;

    RctSetRectangle(&r, x, y, w, h);
    WinDrawRectangle(&r, 0);
}

static void DrawCell(Coord x, Coord y, char c)
{
    UInt8 quads = Quadrants(c), arms = Arms(c);

    if (c == ' ')
        return;
    if (quads) {
        if (quads & 1) FillCell(x, y, 2, 2);
        if (quads & 2) FillCell(x + 2, y, 2, 2);
        if (quads & 4) FillCell(x, y + 2, 2, 2);
        if (quads & 8) FillCell(x + 2, y + 2, 2, 2);
    } else if (arms) {
        if (arms & 1) WinDrawLine(x + 1, y, x + 1, y + 1);
        if (arms & 2) WinDrawLine(x + 1, y + 1, x + 1, y + CELL - 1);
        if (arms & 4) WinDrawLine(x, y + 1, x + 1, y + 1);
        if (arms & 8) WinDrawLine(x + 1, y + 1, x + CELL - 1, y + 1);
    } else if (c == '/') {
        WinDrawLine(x, y + CELL - 1, x + CELL - 1, y);
    } else if (c == '\\') {
        WinDrawLine(x, y, x + CELL - 1, y + CELL - 1);
    } else {
        FillCell(x + 1, y + 1, 2, 2);
    }
}

/* Positions are the shape's centre cell, placed as the upstream client does. */
static void DrawShape(UInt8 id, Int8 cx, Int8 cy)
{
    const UInt8 *data;
    Int16 left, top, x, y;
    UInt8 width, i, j;

    if (id >= gShapeCount)
        return;
    width = gShapeWidth[id];
    data = gShapeBytes + gShapeAt[id];
    left = cx - (width >> 1) - 1;
    top = cy - (width >> 1) - 1;
    if ((width & 1) == 0) {
        left++;
        top++;
    }
    for (i = 0; i < width; i++) {
        y = top + i;
        if (y < 0 || y >= ROWS)
            continue;
        for (j = 0; j < width; j++) {
            x = left + j;
            if (x >= 0 && x < COLS)
                DrawCell(x * CELL, y * CELL, data[i * width + j]);
        }
    }
}

static void DrawBox(Coord x, Coord y, Coord w, Coord h)
{
    RectangleType r;

    RctSetRectangle(&r, x, y, w, h);
    WinEraseRectangle(&r, 0);
    WinDrawRectangleFrame(simpleFrame, &r);
}

static void DrawInfo(void)
{
    char line[48];
    Int16 lineH = FntLineHeight();

    DrawBox(1, 1, WORLD_W - 2, 2 * lineH + 2);
    StrPrintF(line, "%s  C:%d  %ux%u%s", gPrefs.name, gWorld.clients, gWorld.width,
              gWorld.height, gWorld.frozen ? "  frozen" : "");
    WinDrawChars(line, StrLen(line), 3, 2);
    StrPrintF(line, "Bodies 1:%d 2:%d 3:%d 4:%d 5:%d", gWorld.bodies[0], gWorld.bodies[1],
              gWorld.bodies[2], gWorld.bodies[3], gWorld.bodies[4]);
    WinDrawChars(line, StrLen(line), 3, 2 + lineH);
}

static void DrawWho(void)
{
    Int16 lineH = FntLineHeight(), top = gShowInfo ? 2 * lineH + 6 : 2;
    UInt8 i, rows = gWhoCount;

    if ((Int16)rows * lineH + top + 2 > WORLD_H)
        rows = (WORLD_H - top - 2) / lineH;
    DrawBox(WORLD_W - 52, top, 50, rows * lineH + 2);
    for (i = 0; i < rows; i++)
        WinDrawChars(gWho[i], NAME_LEN, WORLD_W - 50, top + 1 + i * lineH);
}

static void DrawMessage(void)
{
    Int16 lineH = FntLineHeight(), width = WORLD_W - 24, rows = 0, len;
    const char *text = gMessage;
    Coord top = 40;

    while (text[0] && rows < 6) {
        len = FntWordWrap(text, width);
        if (len == 0)
            break;
        text += len;
        rows++;
    }
    DrawBox(10, top, width + 4, rows * lineH + 2);
    for (text = gMessage; rows > 0; rows--, top += lineH) {
        len = FntWordWrap(text, width);
        WinDrawChars(text, len, 12, top + 1);
        text += len;
    }
}

/* Redraws the world from the last poll, off screen when possible. */
static void DrawWorld(void)
{
    RectangleType r;
    WinHandle old = NULL;
    FontID font;
    UInt16 count, i, at;

    RctSetRectangle(&r, 0, 0, WORLD_W, WORLD_H);
    if (gOffscreen)
        old = WinSetDrawWindow(gOffscreen);
    WinEraseRectangle(&r, 0);
    count = gViewLen >= 3 ? gView[2] : 0;
    if (count > (gViewLen - 3) / 3)
        count = (gViewLen - 3) / 3;
    for (i = 0, at = 3; i < count; i++, at += 3)
        DrawShape(gView[at], (Int8)gView[at + 1], (Int8)gView[at + 2]);
    font = FntSetFont(stdFont);
    if (gShowInfo)
        DrawInfo();
    if (gShowWho && gWhoCount > 0)
        DrawWho();
    if (gShowMessage && gMessage[0])
        DrawMessage();
    FntSetFont(font);
    if (gDark)
        WinInvertRectangle(&r, 0);
    if (gOffscreen) {
        WinSetDrawWindow(old);
        WinCopyRectangle(gOffscreen, NULL, &r, 0, 0, winPaint);
    }
}

/* ---- World form ------------------------------------------------------ */

static void LoseConnection(Err err)
{
    Disconnect();
    ShowError("Lost the connection to the server.", err);
    FrmGotoForm(ConnectForm);
}

/* One failed request is often a lost packet; several in a row are not. */
static void Check(Err err)
{
    if (err == errNone)
        gFailures = 0;
    else if (err == bwErrClosed || err == bwErrDropped || ++gFailures >= MAX_FAILURES)
        LoseConnection(err);
}

static Err HandleStatus(UInt8 status)
{
    Err err = errNone;

    if (status & (CLIENT_CHANGE | OBJECT_CHANGE | FROZEN_TOGGLE))
        err = LoadWorld();
    if (err == errNone && (status & CLIENT_CHANGE))
        err = LoadWho();
    if (err == errNone && (status & CLIENT_CMD))
        err = RunCommands();
    return err;
}

/* Fetches this client's view; redraws when the world has stepped. */
static Err Poll(void)
{
    char line[LINE_LEN];
    Boolean first = gViewLen == 0;
    UInt16 len;
    UInt8 status;
    Err err;

    StrPrintF(line, "x-w %d", gClientId);
    if ((err = RequestInto(line, gView, sizeof(gView), &len)) != errNone)
        return err;
    if (len == 1 && gView[0] == 0)
        return bwErrDropped;
    if (len < 3)
        return errNone;
    gViewLen = len;
    status = gView[1];
    if (status != 0) {
        err = HandleStatus(status);
        DrawWorld();
    } else if (first || gView[0] != gStep) {
        DrawWorld();
    }
    gStep = gView[0];
    return err;
}

/* Commands answer with one byte, which only keeps the stream in step. */
static void Command(const char *line)
{
    Err err = Request(line);

    if (err == errNone)
        err = LoadWorld();
    Check(err);
    if (gConnected)
        DrawWorld();
}

static void AddBody(Int16 size)
{
    char line[LINE_LEN];

    StrPrintF(line, "x-add-body %d", size);
    Command(line);
}

static void Toggle(Boolean *flag)
{
    *flag = !*flag;
    DrawWorld();
}

static void LeaveWorld(void)
{
    Disconnect();
    FrmGotoForm(ConnectForm);
}

static Boolean WorldKey(WChar c)
{
    switch (c) {
    case '+': case '=': case vchrPageUp: Command("x-inc"); break;
    case '-': case vchrPageDown: Command("x-dec"); break;
    case 'f': case 'F': case vchrHard1: Command("x-freeze"); break;
    case 'r': case 'R': Command("x-reset"); break;
    case '1': case '2': case '3': case '4': case '5': AddBody(c - '0'); break;
    case vchrHard2: AddBody(3); break;
    case 'w': case 'W': case vchrHard3: Toggle(&gShowWho); break;
    case 'i': case 'I': case vchrHard4: Toggle(&gShowInfo); break;
    case 'q': case 'Q': LeaveWorld(); break;
    default: return false;
    }
    return true;
}

static Boolean WorldMenuCommand(UInt16 id)
{
    switch (id) {
    case WorldMenuFreeze: Command("x-freeze"); break;
    case WorldMenuReset: Command("x-reset"); break;
    case WorldMenuWho: Toggle(&gShowWho); break;
    case WorldMenuInfo: Toggle(&gShowInfo); break;
    case WorldMenuDisconnect: LeaveWorld(); break;
    default: return false;
    }
    return true;
}

static Boolean WorldButton(UInt16 id)
{
    switch (id) {
    case WorldFreezeButton: Command("x-freeze"); break;
    case WorldResetButton: Command("x-reset"); break;
    case WorldFasterButton: Command("x-inc"); break;
    case WorldSlowerButton: Command("x-dec"); break;
    case WorldWhoButton: Toggle(&gShowWho); break;
    case WorldInfoButton: Toggle(&gShowInfo); break;
    default: return false;
    }
    return true;
}

static Boolean WorldHandleEvent(EventType *event)
{
    switch (event->eType) {
    case frmOpenEvent:
    case frmUpdateEvent:
        FrmDrawForm(FrmGetActiveForm());
        DrawWorld();
        return true;
    case nilEvent:
        if (gConnected)
            Check(Poll());
        return true;
    case keyDownEvent:
        return gConnected && WorldKey(event->data.keyDown.chr);
    case ctlSelectEvent:
        return gConnected && WorldButton(event->data.ctlSelect.controlID);
    case popSelectEvent:
        if (gConnected)
            AddBody(event->data.popSelect.selection + 1);
        return true; /* keeps the trigger's "Add" label */
    case menuEvent:
        return gConnected && WorldMenuCommand(event->data.menu.itemID);
    default:
        break;
    }
    return false;
}

/* ---- Connect form ---------------------------------------------------- */

static FieldType *GetField(FormType *form, UInt16 id)
{
    return FrmGetObjectPtr(form, FrmGetObjectIndex(form, id));
}

static void SetFieldText(FormType *form, UInt16 id, const char *text)
{
    MemHandle handle = MemHandleNew(StrLen(text) + 1);

    if (handle == NULL)
        return;
    StrCopy(MemHandleLock(handle), text);
    MemHandleUnlock(handle);
    FldSetTextHandle(GetField(form, id), handle);
}

static void GetFieldText(FormType *form, UInt16 id, char *out, UInt16 size, const char *empty)
{
    const char *text = FldGetTextPtr(GetField(form, id));

    if (text == NULL || text[0] == '\0')
        text = empty;
    StrNCopy(out, text, size - 1);
    out[size - 1] = '\0';
}

static void SetDefaults(void)
{
    StrCopy(gPrefs.host, "localhost");
    StrCopy(gPrefs.port, "9003");
    StrCopy(gPrefs.name, "palm");
}

static void LoadPrefs(void)
{
    UInt16 size = sizeof(gPrefs);

    if (PrefGetAppPreferences(CREATOR, PREFS_ID, &gPrefs, &size, true) != PREFS_VERSION ||
        size != sizeof(gPrefs))
        SetDefaults();
    gPrefs.host[HOST_LEN - 1] = gPrefs.port[PORT_LEN - 1] = gPrefs.name[NAME_LEN] = '\0';
}

static void SavePrefs(FormType *form)
{
    GetFieldText(form, ConnectHostField, gPrefs.host, HOST_LEN, "localhost");
    GetFieldText(form, ConnectPortField, gPrefs.port, PORT_LEN, "9003");
    GetFieldText(form, ConnectNameField, gPrefs.name, NAME_LEN + 1, "palm");
    PrefSetAppPreferences(CREATOR, PREFS_ID, PREFS_VERSION, &gPrefs, sizeof(gPrefs), true);
}

static void ConnectShow(FormType *form)
{
    SetFieldText(form, ConnectHostField, gPrefs.host);
    SetFieldText(form, ConnectPortField, gPrefs.port);
    SetFieldText(form, ConnectNameField, gPrefs.name);
    FrmDrawForm(form);
    FrmSetFocus(form, FrmGetObjectIndex(form, ConnectHostField));
}

static void ConnectStart(FormType *form)
{
    char what[HOST_LEN + PORT_LEN + 20];
    Err err;

    SavePrefs(form);
    StrCopy(gTitle, "Connecting...");
    FrmSetTitle(form, gTitle);
    err = Connect();
    StrCopy(gTitle, "Bounce World");
    FrmSetTitle(form, gTitle);
    if (err != errNone) {
        StrPrintF(what, "Could not join %s:%s.", gPrefs.host, gPrefs.port);
        ShowError(what, err);
        return;
    }
    FrmGotoForm(WorldForm);
}

static Boolean ConnectHandleEvent(EventType *event)
{
    FormType *form = FrmGetActiveForm();

    switch (event->eType) {
    case frmOpenEvent:
        ConnectShow(form);
        return true;
    case ctlSelectEvent:
        if (event->data.ctlSelect.controlID == ConnectButton) {
            ConnectStart(form);
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

/* ---- Application ----------------------------------------------------- */

static Boolean AppHandleEvent(EventType *event)
{
    FormType *form;

    if (event->eType != frmLoadEvent)
        return false;
    form = FrmInitForm(event->data.frmLoad.formID);
    FrmSetActiveForm(form);
    if (event->data.frmLoad.formID == WorldForm)
        FrmSetEventHandler(form, WorldHandleEvent);
    else
        FrmSetEventHandler(form, ConnectHandleEvent);
    return true;
}

/* The hard keys play the game instead of launching apps while connected. */
static Boolean IsWorldHardKey(const EventType *event)
{
    WChar c = event->data.keyDown.chr;

    return gConnected && event->eType == keyDownEvent &&
           (event->data.keyDown.modifiers & commandKeyMask) &&
           c >= vchrHard1 && c <= vchrHard4 && FrmGetActiveFormID() == WorldForm;
}

static void EventLoop(void)
{
    EventType event;
    Err err;

    do {
        EvtGetEvent(&event, gConnected ? POLL_TICKS : evtWaitForever);
        if (IsWorldHardKey(&event)) {
            WorldKey(event.data.keyDown.chr);
            continue;
        }
        if (SysHandleEvent(&event))
            continue;
        if (MenuHandleEvent(0, &event, &err))
            continue;
        if (AppHandleEvent(&event))
            continue;
        FrmDispatchEvent(&event);
    } while (event.eType != appStopEvent);
}

/* Palm OS 3.1, the oldest release the app is tested on. */
static Boolean RomCompatible(void)
{
    UInt32 rom = 0;

    FtrGet(sysFtrCreator, sysFtrNumROMVersion, &rom);
    return rom >= sysMakeROMVersion(3, 1, 0, sysROMStageRelease, 0);
}

static Err AppStart(void)
{
    Err err;

    LoadPrefs();
    gOffscreen = WinCreateOffscreenWindow(WORLD_W, WORLD_H, screenFormat, &err);
    return FnOpen();
}

static void AppStop(void)
{
    Disconnect();
    FrmCloseAllForms();
    FnClose();
    if (gOffscreen)
        WinDeleteWindow(gOffscreen, false);
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
    FrmGotoForm(ConnectForm);
    EventLoop();
    AppStop();
    return 0;
}
