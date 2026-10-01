/* FujiNet Lobby for Palm OS.
 *
 * Lists the game servers registered with the FujiNet Game Lobby
 * (github.com/FujiNetWIFI/fujinet-lobby), so the games that already run
 * across the 8-bit machines can be found from a Palm.
 *
 * The 8-bit lobby clients ask GET /view?platform=<theirs>, which only lists
 * servers with a client for that platform. A Palm has none yet, so this asks
 * GET /viewFull instead: every server, with all its clients, as JSON. The
 * body goes through FujiNet's N1: device as a plain HTTP GET and is read in
 * 256-byte pieces straight into a streaming JSON reader (lobbyjson.c), so the
 * ~30 KB reply never has to fit in memory.
 */
#include <PalmOS.h>

#include "fnnet.h"
#include "lobbyjson.h"
#include "FujiLobbyRsc.h"

#define CREATOR (((UInt32)'F' << 24) | ((UInt32)'n' << 16) | ((UInt32)'L' << 8) | 'b')
#define PREFS_ID 1
#define PREFS_VERSION 1

#define NET_UNIT 1
/* Plain HTTP: the lobby serves the same JSON on port 80, and it spares
 * FujiNet a TLS session. */
#define LOBBY_URL    "N1:HTTP://lobby.fujinet.online/viewFull"
#define QA_LOBBY_URL "N1:HTTP://qalobby.fujinet.online/viewFull"

#define MAX_SERVERS 48
#define POOL_SIZE 6144
#define STATUS_LEN 48

/* The first STATUS is when FujiNet makes the HTTP request. */
#define FIRST_STATUS_TICKS (SysTicksPerSecond() * 15)
#define STALL_TICKS (SysTicksPerSecond() * 15)
#define POLL_TICKS (SysTicksPerSecond() / 20)
#define FETCH_ATTEMPTS 2

#define lbErrTimeout  (appErrorClass | 0x10)
#define lbErrBadReply (appErrorClass | 0x11)
#define lbErrMemory   (appErrorClass | 0x12)

/* Posted once the main form is drawn, to fetch the list at start. */
#define refreshEvent firstUserEvent

typedef struct {
    UInt8 useQA;
} LbPrefs;

static LbPrefs gPrefs;
static LbParser gParser;
static LbServer *gServers;
static char *gPool;
static UInt16 gCount;
static Int16 gSelected = noListSelection;
static char gStatus[STATUS_LEN];
static char gBuf[FN_NET_CHUNK];
static char gTitle[20];
static Boolean gOpened;

static void ShowError(const char *what, Err err, UInt8 ndevErr)
{
    char detail[96];

    if (err == fnErrNoReply)
        StrCopy(detail, "\nNo answer from FujiNet. Is the cradle cabled to it?");
    else if (err == fnErrRefused)
        StrCopy(detail, "\nFujiNet refused the request.");
    else if (err == lbErrTimeout)
        StrCopy(detail, "\nThe lobby stopped sending.");
    else if (err == lbErrBadReply)
        StrCopy(detail, "\nThe lobby's reply was cut short or is not the JSON expected.");
    else if (err == lbErrMemory)
        StrCopy(detail, "\nNot enough memory.");
    else if (err == errNone && ndevErr == FN_NET_NOT_FOUND)
        StrCopy(detail, "\nThe lobby answered 404 (no servers).");
    else if (err == errNone && ndevErr != 0)
        StrPrintF(detail, "\nFujiNet network error %d. Is its WiFi up?", ndevErr);
    else
        StrPrintF(detail, "\nError %x.", err);
    FrmCustomAlert(MessageAlert, what, detail, "");
}

static const char *LobbyUrl(void)
{
    return gPrefs.useQA ? QA_LOBBY_URL : LOBBY_URL;
}

/* ---- Main form helpers ----------------------------------------------- */

static void *GetObject(FormType *form, UInt16 id)
{
    return FrmGetObjectPtr(form, FrmGetObjectIndex(form, id));
}

static void SetStatus(const char *text)
{
    FormType *form = FrmGetFormPtr(MainForm);
    FieldType *field;

    StrNCopy(gStatus, text, STATUS_LEN - 1);
    gStatus[STATUS_LEN - 1] = '\0';
    if (form == NULL)
        return;
    field = GetObject(form, MainStatusField);
    FldSetTextPtr(field, gStatus);
    FldRecalculateField(field, false);
    if (FrmGetActiveForm() == form && FrmVisible(form))
        FldDrawField(field);
}

/* Draws text cut to fit width, ending in "..." when it was cut. */
static void DrawFit(const char *text, Coord x, Coord y, Int16 width)
{
    Int16 w = width, len = StrLen(text), dots;
    Boolean fits;

    FntCharsInWidth(text, &w, &len, &fits);
    if (fits) {
        WinDrawChars(text, len, x, y);
        return;
    }
    dots = FntCharsWidth("...", 3);
    w = width - dots;
    len = StrLen(text);
    FntCharsInWidth(text, &w, &len, &fits);
    WinDrawChars(text, len, x, y);
    WinDrawChars("...", 3, x + w, y);
}

/* One row: game, server name, and players (or "off" when offline). */
static void DrawRow(Int16 item, RectangleType *bounds, Char **unused)
{
    const LbServer *s;
    char players[12];
    Coord x = bounds->topLeft.x + 1, y = bounds->topLeft.y;
    Int16 right = bounds->topLeft.x + bounds->extent.x - 10, pw;

    if (item < 0 || item >= gCount)
        return;
    s = &gServers[item];
    if (StrCompare(s->status, "online") == 0)
        StrPrintF(players, "%u/%u", s->curplayers, s->maxplayers);
    else
        StrCopy(players, "off");
    pw = FntCharsWidth(players, StrLen(players));
    DrawFit(s->game, x, y, 62);
    DrawFit(s->server, x + 64, y, right - pw - 4 - (x + 64));
    WinDrawChars(players, StrLen(players), right - pw, y);
}

static UInt16 CountGames(void)
{
    UInt16 games = 0, i, j;

    for (i = 0; i < gCount; i++) {
        for (j = 0; j < i; j++)
            if (StrCompare(gServers[i].game, gServers[j].game) == 0)
                break;
        if (j == i)
            games++;
    }
    return games;
}

static void ShowList(void)
{
    FormType *form = FrmGetFormPtr(MainForm);
    ListType *list;
    RectangleType r;

    if (form == NULL)
        return;
    list = GetObject(form, MainList);
    LstSetListChoices(list, NULL, gCount);
    LstSetDrawFunction(list, DrawRow);
    LstSetSelection(list, noListSelection);
    gSelected = noListSelection;
    if (FrmGetActiveForm() != form || !FrmVisible(form))
        return;
    FrmGetObjectBounds(form, FrmGetObjectIndex(form, MainList), &r);
    WinEraseRectangle(&r, 0);
    LstDrawList(list);
}

/* ---- Fetching -------------------------------------------------------- */

static void ShowProgress(UInt32 bytes)
{
    char text[STATUS_LEN];

    StrPrintF(text, "Reading... %ld KB, %d servers", (Int32)(bytes / 1024),
              gParser.total);
    SetStatus(text);
}

/* One GET of the list into gParser. *ndevErr is FujiNet's status code
 * when the request ended badly (an HTTP error or no network). */
static Err Fetch(UInt8 *ndevErr)
{
    FnNetStatus status;
    UInt32 last, bytes = 0, shown = 0;
    UInt16 want, got;
    Err err;

    *ndevErr = 0;
    gOpened = false;
    lb_init(&gParser, gServers, MAX_SERVERS, gPool, POOL_SIZE);
    err = FnNetOpen(NET_UNIT, LobbyUrl(), FN_NET_HTTP_GET, FN_NET_TRANS_NONE);
    if (err != errNone)
        return err;
    gOpened = true;
    SetStatus("Asking the lobby...");
    err = FnNetGetStatusWait(NET_UNIT, &status, FIRST_STATUS_TICKS);
    last = TimGetTicks();
    while (err == errNone) {
        EvtResetAutoOffTimer();
        if (status.avail > 0) {
            want = status.avail < sizeof(gBuf) ? status.avail : sizeof(gBuf);
            if ((err = FnNetRead(NET_UNIT, gBuf, want, &got)) != errNone)
                break;
            lb_feed(&gParser, gBuf, got);
            if (gParser.error) {
                err = lbErrBadReply;
                break;
            }
            bytes += got;
            last = TimGetTicks();
            if (bytes - shown >= 1024) {
                ShowProgress(bytes);
                shown = bytes;
            }
        } else if (!status.connected) {
            if (status.error != FN_NET_OK && status.error != FN_NET_EOF)
                *ndevErr = status.error;
            break;
        } else if (TimGetTicks() - last > STALL_TICKS) {
            err = lbErrTimeout;
            break;
        } else {
            SysTaskDelay(POLL_TICKS);
        }
        err = FnNetGetStatus(NET_UNIT, &status);
    }
    FnNetClose(NET_UNIT);
    return err;
}

static void Refresh(void)
{
    char text[STATUS_LEN];
    UInt8 ndevErr = 0;
    UInt16 attempt;
    Err err = errNone;

    gCount = 0;
    ShowList();
    if (gServers == NULL || gPool == NULL) {
        SetStatus("Not enough memory.");
        ShowError("Could not fetch the lobby.", lbErrMemory, 0);
        return;
    }
    SetStatus("Connecting...");
    /* A READ is sent once (see fnnet.h); if one goes missing the stream is
     * out of step, so the whole list is fetched again. */
    for (attempt = 0; attempt < FETCH_ATTEMPTS; attempt++) {
        err = Fetch(&ndevErr);
        if (err != fnErrNoReply || !gOpened)
            break;
    }
    gCount = gParser.count;

    if (err == errNone && ndevErr == 0 && !lb_finish(&gParser))
        err = lbErrBadReply;
    if (err == errNone && ndevErr != 0 && gParser.message[0]) {
        /* The lobby's own explanation, such as "No servers available". */
        StrPrintF(text, "Lobby: %s", gParser.message);
        SetStatus(text);
    } else if (err != errNone || ndevErr != 0) {
        SetStatus(gCount ? "Partial list. Tap Refresh." : "Failed. Tap Refresh to retry.");
        ShowList();
        ShowError("Could not fetch the lobby.", err, ndevErr);
        return;
    } else if (gParser.total > gCount) {
        StrPrintF(text, "%d of %d servers, %d games", gCount, gParser.total, CountGames());
        SetStatus(text);
    } else {
        StrPrintF(text, "%d servers, %d games%s", gCount, CountGames(),
                  gPrefs.useQA ? " (QA)" : "");
        SetStatus(text);
    }
    ShowList();
}

/* ---- Detail form ----------------------------------------------------- */

static UInt16 Append(char *out, UInt16 at, const char *text)
{
    UInt16 n = StrLen(text);
    MemMove(out + at, text, n);
    return at + n;
}

/* Everything the lobby said about one server. */
static MemHandle DetailText(const LbServer *s)
{
    UInt16 size = 320 + StrLen(s->url) + StrLen(s->platforms) + s->clientsLen * 2;
    MemHandle handle = MemHandleNew(size);
    char line[64], *out;
    UInt16 at = 0, i;
    Boolean lineStart = true;

    if (handle == NULL)
        return NULL;
    out = MemHandleLock(handle);
    at = Append(out, at, s->game);
    at = Append(out, at, "\n");
    at = Append(out, at, s->server);
    StrPrintF(line, "\n\nStatus: %s, region %s\n", s->status, s->region);
    at = Append(out, at, line);
    StrPrintF(line, "Players: %u of %u\nApp key: %u\n\nServer URL:\n", s->curplayers,
              s->maxplayers, s->appkey);
    at = Append(out, at, line);
    at = Append(out, at, s->url);
    StrPrintF(line, "\n\nPlatforms (%d):\n", s->clientCount);
    at = Append(out, at, line);
    at = Append(out, at, s->platforms);
    if (s->clientsLen > 0) {
        at = Append(out, at, "\n\nClients:\n");
        /* Pool lines are "platform url"; shown as "platform: url". */
        for (i = 0; i < s->clientsLen; i++) {
            char c = gPool[s->clientsAt + i];
            if (lineStart && c == ' ') {
                at = Append(out, at, ": ");
                lineStart = false;
                continue;
            }
            out[at++] = c;
            if (c == '\n')
                lineStart = true;
        }
    }
    if (s->clientsCut)
        at = Append(out, at, "\n(Some client URLs not shown.)");
    out[at] = '\0';
    MemHandleUnlock(handle);
    return handle;
}

static void UpdateScrollBar(FormType *form)
{
    FieldType *field = GetObject(form, DetailField);
    ScrollBarType *bar = GetObject(form, DetailScrollBar);
    UInt16 pos, textHeight, fieldHeight, max;

    FldGetScrollValues(field, &pos, &textHeight, &fieldHeight);
    if (textHeight > fieldHeight)
        max = textHeight - fieldHeight;
    else
        max = pos;
    SclSetScrollBar(bar, pos, 0, max, fieldHeight - 1);
}

static void ScrollDetail(FormType *form, Int16 lines)
{
    FieldType *field = GetObject(form, DetailField);

    if (lines < 0)
        FldScrollField(field, -lines, winUp);
    else if (lines > 0)
        FldScrollField(field, lines, winDown);
    UpdateScrollBar(form);
}

static void PageDetail(FormType *form, WinDirectionType direction)
{
    FieldType *field = GetObject(form, DetailField);
    UInt16 lines = FldGetVisibleLines(field) - 1;

    if (FldScrollable(field, direction))
        ScrollDetail(form, direction == winUp ? -(Int16)lines : (Int16)lines);
}

static Boolean DetailHandleEvent(EventType *event)
{
    FormType *form = FrmGetActiveForm();
    MemHandle text;

    switch (event->eType) {
    case frmOpenEvent:
        if (gSelected >= 0 && gSelected < gCount &&
            (text = DetailText(&gServers[gSelected])) != NULL)
            FldSetTextHandle(GetObject(form, DetailField), text);
        FrmDrawForm(form);
        UpdateScrollBar(form);
        return true;
    case ctlSelectEvent:
        if (event->data.ctlSelect.controlID == DetailDoneButton) {
            FrmReturnToForm(MainForm);
            return true;
        }
        break;
    case sclRepeatEvent:
        ScrollDetail(form, event->data.sclRepeat.newValue - event->data.sclRepeat.value);
        return false;
    case keyDownEvent:
        if (event->data.keyDown.chr == vchrPageUp) {
            PageDetail(form, winUp);
            return true;
        }
        if (event->data.keyDown.chr == vchrPageDown) {
            PageDetail(form, winDown);
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

/* ---- Main form ------------------------------------------------------- */

static void OpenDetail(Int16 item)
{
    if (item < 0 || item >= gCount)
        return;
    gSelected = item;
    FrmPopupForm(DetailForm);
}

static void SetTitle(FormType *form)
{
    StrCopy(gTitle, gPrefs.useQA ? "FujiNet QA Lobby" : "FujiNet Lobby");
    FrmSetTitle(form, gTitle);
}

static void ToggleQA(void)
{
    gPrefs.useQA = !gPrefs.useQA;
    PrefSetAppPreferences(CREATOR, PREFS_ID, PREFS_VERSION, &gPrefs, sizeof(gPrefs), true);
    SetTitle(FrmGetFormPtr(MainForm));
    Refresh();
}

static void About(void)
{
    char where[64];

    StrPrintF(where, "Lobby: %s", LobbyUrl() + 3);
    FrmCustomAlert(AboutAlert, where, "", "");
}

static Boolean MainHandleEvent(EventType *event)
{
    FormType *form = FrmGetActiveForm();
    EventType refresh;

    switch (event->eType) {
    case frmOpenEvent:
        SetTitle(form);
        ShowList();
        FldSetTextPtr(GetObject(form, MainStatusField), gStatus);
        FrmDrawForm(form);
        MemSet(&refresh, sizeof(refresh), 0);
        refresh.eType = refreshEvent;
        EvtAddEventToQueue(&refresh);
        return true;
    case frmUpdateEvent:
        FrmDrawForm(form);
        return true;
    case refreshEvent:
        Refresh();
        return true;
    case lstSelectEvent:
        if (event->data.lstSelect.listID == MainList) {
            OpenDetail(event->data.lstSelect.selection);
            return true;
        }
        break;
    case ctlSelectEvent:
        if (event->data.ctlSelect.controlID == MainRefreshButton) {
            Refresh();
            return true;
        }
        if (event->data.ctlSelect.controlID == MainInfoButton) {
            OpenDetail(LstGetSelection(GetObject(form, MainList)));
            return true;
        }
        break;
    case menuEvent:
        switch (event->data.menu.itemID) {
        case MainMenuRefresh: Refresh(); return true;
        case MainMenuQA: ToggleQA(); return true;
        case MainMenuAbout: About(); return true;
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
    if (event->data.frmLoad.formID == DetailForm)
        FrmSetEventHandler(form, DetailHandleEvent);
    else
        FrmSetEventHandler(form, MainHandleEvent);
    return true;
}

static void EventLoop(void)
{
    EventType event;
    Err err;

    do {
        EvtGetEvent(&event, evtWaitForever);
        if (SysHandleEvent(&event))
            continue;
        if (MenuHandleEvent(0, &event, &err))
            continue;
        if (AppHandleEvent(&event))
            continue;
        FrmDispatchEvent(&event);
    } while (event.eType != appStopEvent);
}

/* The Serial Manager with SrmOpen first shipped in Palm OS 3.3. */
static Boolean HasNewSerialManager(void)
{
    UInt32 value;
    return FtrGet(sysFileCSerialMgr, sysFtrNewSerialPresent, &value) == errNone && value != 0;
}

static Err AppStart(void)
{
    UInt16 size = sizeof(gPrefs);

    if (PrefGetAppPreferences(CREATOR, PREFS_ID, &gPrefs, &size, true) != PREFS_VERSION ||
        size != sizeof(gPrefs))
        gPrefs.useQA = 0;
    gServers = MemPtrNew(MAX_SERVERS * sizeof(LbServer));
    gPool = MemPtrNew(POOL_SIZE);
    gStatus[0] = '\0';
    return FnOpen();
}

static void AppStop(void)
{
    FrmCloseAllForms();
    FnClose();
    if (gServers)
        MemPtrFree(gServers);
    if (gPool)
        MemPtrFree(gPool);
    gServers = NULL;
    gPool = NULL;
}

UInt32 PilotMain(UInt16 cmd, MemPtr cmdPBP, UInt16 launchFlags)
{
    Err err;

    if (cmd != sysAppLaunchCmdNormalLaunch)
        return 0;
    if (!HasNewSerialManager()) {
        FrmAlert(RomIncompatibleAlert);
        return 0;
    }
    err = AppStart();
    if (err != errNone) {
        ShowError("Could not open the cradle port.", err, 0);
        AppStop();
        return 0;
    }
    FrmGotoForm(MainForm);
    EventLoop();
    AppStop();
    return 0;
}
