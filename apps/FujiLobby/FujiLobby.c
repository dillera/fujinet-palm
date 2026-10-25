/* FI Lobby for Palm OS.
 *
 * Lists the game servers registered with the FujiNet Game Lobby
 * (github.com/FujiNetWIFI/fujinet-lobby), so the games that already run
 * across the 8-bit machines can be found from a Palm.
 *
 * The 8-bit lobby clients ask GET /view?platform=<theirs>, which only lists
 * servers with a client for that platform. A Palm has none yet, so this asks
 * GET /viewFull instead: every server, with all its clients, as JSON. The
 * body comes through the FujiNet NIO on the cradle (fujinet-nio-lib) as a
 * plain HTTP GET and is read in 256-byte pieces straight into a streaming
 * JSON reader (lobbyjson.c), so the ~30 KB reply never has to fit in memory.
 * The fetch runs a step at a time from the event loop, so the app stays
 * usable while the lobby answers.
 */
#include <PalmOS.h>

#include "fn_types.h"
#include "fujinet-nio.h"
#include "lobbyjson.h"
#include "FujiLobbyRsc.h"

#define CREATOR APP_CREATOR
#define PREFS_ID 1
#define PREFS_VERSION 1

/* Plain HTTP: the lobby serves the same JSON on port 80, and it spares
 * FujiNet a TLS session. */
#define LOBBY_URL    "http://lobby.fujinet.online/viewFull"
#define QA_LOBBY_URL "http://qalobby.fujinet.online/viewFull"

#define MAX_SERVERS 48
#define POOL_SIZE 6144
#define STATUS_LEN 48

/* Largest READ; with its header it fits the library's 512-byte packet. */
#define NET_CHUNK 256
/* A lost reply is asked for again this many times in all. */
#define NET_TRIES 3
/* The lobby's headers come once FujiNet has made the HTTP request. */
#define FIRST_STATUS_TICKS (SysTicksPerSecond() * 15)
#define STALL_TICKS (SysTicksPerSecond() * 15)
#define POLL_TICKS (SysTicksPerSecond() / 20)

#define lbErrTimeout  (appErrorClass | 0x10)
#define lbErrBadReply (appErrorClass | 0x11)
#define lbErrMemory   (appErrorClass | 0x12)
/* A fujinet-nio-lib error, in the low byte. */
#define lbErrNio      (appErrorClass | 0x100)

/* Posted once the main form is drawn, to fetch the list at start. */
#define refreshEvent firstUserEvent

typedef struct {
    UInt8 useQA;
} LbPrefs;

/* Where the fetch is: waiting for the lobby's HTTP status, or reading. */
typedef enum { fetchIdle, fetchInfo, fetchRead } FetchState;

static LbPrefs gPrefs;
static LbParser gParser;
static LbServer *gServers;
static char *gPool;
static UInt16 gCount;
static Int16 gSelected = noListSelection;
static char gStatus[STATUS_LEN];
static uint8_t gBuf[NET_CHUNK];
static char gTitle[20];
/* Whether the main form is on screen and not under a menu, an alert or the
 * detail form, where drawing would be lost; and whether it changed while it
 * was covered. */
static Boolean gMainShown, gStale;

static FetchState gFetch = fetchIdle;
static fn_handle_t gHandle = FN_INVALID_HANDLE;
static UInt32 gReadAt, gShown;
/* Ticks spent waiting on NOT_READY since the last bytes, and when the last
 * step ran; time in an alert or a menu between steps does not count. */
static UInt32 gWaited, gStepAt;
static uint16_t gHttpStatus;
static uint8_t gInfoFlags;
static UInt8 gTries;

static void ShowError(const char *what, Err err, UInt16 httpStatus)
{
    char detail[96];

    if (err == (lbErrNio | FN_ERR_TIMEOUT))
        StrCopy(detail, "\nNo answer from FujiNet. Is the cradle cabled to it?");
    else if (err == (lbErrNio | FN_ERR_NOT_READY))
        StrCopy(detail, "\nFujiNet has no network. Is its WiFi up?");
    else if (err == (lbErrNio | FN_ERR_IO))
        StrCopy(detail, "\nFujiNet could not reach the lobby. Is its WiFi up?");
    else if ((err & 0xFF00) == lbErrNio)
        StrPrintF(detail, "\nFujiNet: %s.", fn_error_string((uint8_t)err));
    else if (err == lbErrTimeout)
        StrCopy(detail, "\nThe lobby stopped sending.");
    else if (err == lbErrBadReply)
        StrCopy(detail, "\nThe lobby's reply was cut short or is not the JSON expected.");
    else if (err == lbErrMemory)
        StrCopy(detail, "\nNot enough memory.");
    else if (err == errNone && httpStatus == 404)
        StrCopy(detail, "\nThe lobby answered 404 (no servers).");
    else if (err == errNone && httpStatus != 0)
        StrPrintF(detail, "\nThe lobby answered HTTP %u.", httpStatus);
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
    if (gMainShown)
        FldDrawField(field);
    else
        gStale = true;
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
    if (!gMainShown) {
        gStale = true;
        return;
    }
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

/* A library result as an Err, for ShowError. */
static Err NioErr(uint8_t result)
{
    return result == FN_OK ? errNone : (lbErrNio | result);
}

/* A reply lost on the cradle line, or cut short on it. */
static Boolean LostReply(uint8_t result)
{
    return result == FN_ERR_TIMEOUT || result == FN_ERR_TRANSPORT || result == FN_ERR_IO;
}

static void CloseFetch(void)
{
    if (gHandle != FN_INVALID_HANDLE)
        fn_close(gHandle);
    gHandle = FN_INVALID_HANDLE;
    gFetch = fetchIdle;
}

/* Shows how the fetch ended: the list, the lobby's own message, or why it
 * failed. */
static void EndFetch(Err err)
{
    char text[STATUS_LEN];
    UInt16 httpStatus = 0;

    CloseFetch();
    gCount = gParser.count;
    if ((gInfoFlags & FN_INFO_HAS_STATUS) && (gHttpStatus < 200 || gHttpStatus > 299))
        httpStatus = gHttpStatus;

    if (err == errNone && httpStatus == 0 && !lb_finish(&gParser))
        err = lbErrBadReply;
    if (err == errNone && httpStatus != 0 && gParser.message[0]) {
        /* The lobby's own explanation, such as "No servers available". */
        StrPrintF(text, "Lobby: %s", gParser.message);
        SetStatus(text);
    } else if (err != errNone || httpStatus != 0) {
        SetStatus(gCount ? "Partial list. Tap Refresh." : "Failed. Tap Refresh to retry.");
        ShowList();
        ShowError("Could not fetch the lobby.", err, httpStatus);
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

/* Opens a GET of the list; FetchStep does the rest. */
static void Refresh(void)
{
    uint8_t result;

    CloseFetch();
    gCount = 0;
    ShowList();
    if (gServers == NULL || gPool == NULL) {
        SetStatus("Not enough memory.");
        ShowError("Could not fetch the lobby.", lbErrMemory, 0);
        return;
    }
    SetStatus("Connecting...");
    lb_init(&gParser, gServers, MAX_SERVERS, gPool, POOL_SIZE);
    gReadAt = gShown = gWaited = 0;
    gHttpStatus = 0;
    gInfoFlags = 0;
    gTries = 0;
    gStepAt = TimGetTicks();
    result = fn_init();
    if (result == FN_OK)
        result = fn_open(&gHandle, FN_METHOD_GET, LobbyUrl(), 0);
    if (result != FN_OK) {
        gHandle = FN_INVALID_HANDLE;
        EndFetch(NioErr(result));
        return;
    }
    gFetch = fetchInfo;
    SetStatus("Asking the lobby...");
}

/* One exchange of the fetch: the lobby's HTTP status first, then a piece of
 * the body into the reader. INFO and READ are non-blocking: NOT_READY means
 * the lobby has not answered yet. A READ whose reply was lost is sent again
 * at the same offset and size; NIO answers a repeat with the same bytes.
 * Returns true when the next step should run at once. */
static Boolean FetchStep(void)
{
    UInt32 now = TimGetTicks(), gap = now - gStepAt;
    uint32_t length;
    uint16_t got = 0;
    uint8_t flags = 0, result;

    gStepAt = now;
    if (gap > 2 * POLL_TICKS)
        gap = 2 * POLL_TICKS;
    EvtResetAutoOffTimer();
    if (gFetch == fetchInfo)
        result = fn_info(gHandle, &gHttpStatus, &length, &gInfoFlags);
    else
        result = fn_read(gHandle, gReadAt, gBuf, sizeof(gBuf), &got, &flags);

    if (LostReply(result)) {
        if (++gTries >= NET_TRIES) {
            EndFetch(NioErr(result));
            return false;
        }
        return true;
    }
    gTries = 0;
    if (result == FN_ERR_NOT_READY || result == FN_ERR_BUSY) {
        gWaited += gap;
        if (gWaited > (gFetch == fetchInfo ? FIRST_STATUS_TICKS : STALL_TICKS))
            EndFetch(lbErrTimeout);
        return false;
    }
    if (result != FN_OK) {
        EndFetch(NioErr(result));
        return false;
    }
    gWaited = 0;
    if (gFetch == fetchInfo) {
        gFetch = fetchRead;
        SetStatus("Reading...");
        return true;
    }
    if (got > sizeof(gBuf)) {
        EndFetch(lbErrBadReply);
        return false;
    }
    lb_feed(&gParser, (const char *)gBuf, got);
    if (gParser.error) {
        EndFetch(lbErrBadReply);
        return false;
    }
    gReadAt += got;
    if (gReadAt - gShown >= 1024) {
        ShowProgress(gReadAt);
        gShown = gReadAt;
    }
    if ((flags & FN_READ_EOF) || got == 0) {
        EndFetch(errNone);
        return false;
    }
    return true;
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

/* Palm OS 3.1 draws a new title without erasing the old one, so a shorter
 * title would leave the end of a longer one showing. */
static void SetFormTitle(FormType *form, char *title)
{
    RectangleType r;

    if (FrmVisible(form)) {
        RctSetRectangle(&r, 0, 0, 160, 12);
        WinEraseRectangle(&r, 0);
    }
    FrmSetTitle(form, title);
}

static void SetTitle(FormType *form)
{
    StrCopy(gTitle, gPrefs.useQA ? "FI QA Lobby" : "FI Lobby");
    SetFormTitle(form, gTitle);
}

static void ToggleQA(void)
{
    FormType *form;

    gPrefs.useQA = !gPrefs.useQA;
    PrefSetAppPreferences(CREATOR, PREFS_ID, PREFS_VERSION, &gPrefs, sizeof(gPrefs), true);
    /* A shorter title leaves the end of the old one behind, so the form is
     * drawn afresh. */
    form = FrmGetFormPtr(MainForm);
    SetTitle(form);
    FrmEraseForm(form);
    FrmDrawForm(form);
    Refresh();
}

static void About(void)
{
    char where[64];

    StrPrintF(where, "Lobby: %s", LobbyUrl());
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
        gStale = false;
        MemSet(&refresh, sizeof(refresh), 0);
        refresh.eType = refreshEvent;
        EvtAddEventToQueue(&refresh);
        return true;
    case frmUpdateEvent:
        gStale = false;
        FrmDrawForm(form);
        ShowList();
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

/* Follows the main form being covered and uncovered, and redraws it if the
 * fetch changed it meanwhile. */
static void TrackMainWindow(const EventType *event)
{
    FormType *form = FrmGetFormPtr(MainForm);
    WinHandle window = form ? FrmGetWindowHandle(form) : NULL;

    if (window == NULL)
        return;
    if (event->eType == winEnterEvent && event->data.winEnter.enterWindow == window) {
        gMainShown = true;
        if (gStale)
            FrmUpdateForm(MainForm, frmRedrawUpdateCode);
    } else if (event->eType == winExitEvent && event->data.winExit.exitWindow == window) {
        gMainShown = false;
    }
}

/* While a fetch runs, each pass with no event waiting moves it on a step,
 * and waits a little when FujiNet has nothing yet. */
static void EventLoop(void)
{
    EventType event;
    Int32 timeout = POLL_TICKS;
    Err err;

    do {
        EvtGetEvent(&event, gFetch != fetchIdle ? timeout : evtWaitForever);
        if (event.eType == nilEvent && gFetch != fetchIdle) {
            timeout = FetchStep() ? 0 : POLL_TICKS;
            continue;
        }
        if (event.eType == winEnterEvent || event.eType == winExitEvent)
            TrackMainWindow(&event);
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
    UInt16 size = sizeof(gPrefs);

    if (PrefGetAppPreferences(CREATOR, PREFS_ID, &gPrefs, &size, true) != PREFS_VERSION ||
        size != sizeof(gPrefs))
        gPrefs.useQA = 0;
    gServers = MemPtrNew(MAX_SERVERS * sizeof(LbServer));
    gPool = MemPtrNew(POOL_SIZE);
    gStatus[0] = '\0';
    return errNone;
}

static void AppStop(void)
{
    CloseFetch();
    FrmCloseAllForms();
    fn_shutdown();
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
    if (!RomCompatible()) {
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
