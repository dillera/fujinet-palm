/* FN Test: checks the link to the FujiNet NIO on the cradle. Ping asks the
 * FujiNet for its firmware and network; Fetch gets a web page through it. */
#include <PalmOS.h>

#include "fujinet-nio.h"
#include "FujiNetTestRsc.h"

#define MAX_LINES 8
#define LINE_CHARS 40
#define TEXT_TOP 20
#define LINE_HEIGHT 12

#define FETCH_URL "http://example.com/"
/* The fetch happens while FujiNet answers, so allow the network its time. */
#define FETCH_TICKS (SysTicksPerSecond() * 20)
#define FETCH_CHUNK 256
#define KEEP_BYTES 512

static Char gLines[MAX_LINES][LINE_CHARS];
static UInt16 gLineCount;
static uint8_t gPage[KEEP_BYTES + 1];

static void AddLine(const Char *text)
{
    if (gLineCount >= MAX_LINES)
        return;
    StrNCopy(gLines[gLineCount], text, LINE_CHARS - 1);
    gLines[gLineCount][LINE_CHARS - 1] = '\0';
    gLineCount++;
}

static void DrawLines(void)
{
    RectangleType area;
    UInt16 i;

    RctSetRectangle(&area, 0, TEXT_TOP, 160, MAX_LINES * LINE_HEIGHT);
    WinEraseRectangle(&area, 0);
    for (i = 0; i < gLineCount; i++)
        WinDrawChars(gLines[i], StrLen(gLines[i]), 4, TEXT_TOP + i * LINE_HEIGHT);
}

static void ShowStart(const Char *text)
{
    gLineCount = 0;
    AddLine(text);
    DrawLines();
    gLineCount = 0;
}

static void AddError(const Char *what, uint8_t err)
{
    Char line[LINE_CHARS];

    if (err == FN_ERR_TIMEOUT) {
        AddLine("No answer from FujiNet.");
        AddLine("Is the cradle cabled to it?");
        return;
    }
    StrPrintF(line, "%s: %s", what, fn_error_string(err));
    AddLine(line);
}

static void PingFujiNet(void)
{
    fn_fuji_info_t info;
    fn_wifi_status_t status;
    fn_wifi_config_t config;
    Char line[LINE_CHARS];
    uint8_t err;

    ShowStart("Contacting FujiNet...");
    err = fn_init();
    if (err == FN_OK)
        err = fn_fuji_get_info(&info);
    if (err != FN_OK) {
        AddError("FujiNet", err);
        DrawLines();
        return;
    }
    AddLine("FujiNet found!");
    StrPrintF(line, "Firmware %s", info.firmware);
    AddLine(line);
    StrPrintF(line, "Board %s", info.profile);
    AddLine(line);

    err = fn_wifi_get_status(&status);
    if (err != FN_OK) {
        AddError("WiFi status", err);
    } else {
        StrPrintF(line, "IP %s", status.ip[0] ? status.ip : "none");
        AddLine(line);
    }
    err = fn_wifi_get_config(&config);
    if (err != FN_OK) {
        AddError("WiFi config", err);
    } else {
        StrPrintF(line, "WiFi %s", config.ssid);
        AddLine(line);
    }
    DrawLines();
}

static Boolean TimeLeft(UInt32 deadline)
{
    return TimGetTicks() < deadline;
}

/* The page's <title>, or an empty string. */
static void FindTitle(const Char *page, Char *title, UInt16 cap)
{
    const Char *start = StrStr(page, "<title>");
    const Char *end;
    UInt16 len;

    title[0] = '\0';
    if (start == NULL)
        return;
    start += 7;
    end = StrStr(start, "</title>");
    if (end == NULL)
        return;
    len = (UInt16)(end - start);
    if (len >= cap)
        len = cap - 1;
    MemMove(title, start, len);
    title[len] = '\0';
}

static void FetchPage(void)
{
    fn_handle_t handle;
    uint16_t httpStatus = 0, got;
    uint32_t length, total = 0;
    uint8_t infoFlags = 0, readFlags = 0, err;
    uint8_t chunk[FETCH_CHUNK];
    Char line[LINE_CHARS], title[LINE_CHARS];
    UInt32 deadline = TimGetTicks() + FETCH_TICKS;

    ShowStart("Fetching " FETCH_URL);
    err = fn_init();
    if (err == FN_OK)
        err = fn_open(&handle, FN_METHOD_GET, FETCH_URL, 0);
    if (err != FN_OK) {
        AddError("Open", err);
        DrawLines();
        return;
    }

    do {
        err = fn_info(handle, &httpStatus, &length, &infoFlags);
    } while ((err == FN_ERR_NOT_READY || err == FN_ERR_BUSY) && TimeLeft(deadline));

    while (err == FN_OK && TimeLeft(deadline)) {
        err = fn_read(handle, total, chunk, sizeof(chunk), &got, &readFlags);
        if (err == FN_ERR_NOT_READY || err == FN_ERR_BUSY) {
            err = FN_OK;
            continue;
        }
        if (err != FN_OK)
            break;
        if (total < KEEP_BYTES) {
            UInt16 keep = got;
            if (total + keep > KEEP_BYTES)
                keep = (UInt16)(KEEP_BYTES - total);
            MemMove(gPage + total, chunk, keep);
        }
        total += got;
        if ((readFlags & FN_READ_EOF) || got == 0)
            break;
    }
    fn_close(handle);

    gPage[total < KEEP_BYTES ? total : KEEP_BYTES] = '\0';
    if (err != FN_OK) {
        AddError("Fetch", err);
    } else if (!(readFlags & FN_READ_EOF) && !TimeLeft(deadline)) {
        AddLine("Fetch timed out.");
    }
    if (infoFlags & FN_INFO_HAS_STATUS) {
        StrPrintF(line, "HTTP %u, %lu bytes", httpStatus, total);
        AddLine(line);
    }
    FindTitle((const Char *)gPage, title, sizeof(title));
    if (title[0]) {
        AddLine("Title:");
        AddLine(title);
    }
    DrawLines();
}

static Boolean MainFormHandleEvent(EventType *event)
{
    switch (event->eType) {
    case frmOpenEvent:
        FrmDrawForm(FrmGetActiveForm());
        gLineCount = 0;
        AddLine("Tap Ping to find a FujiNet");
        AddLine("on the cradle serial port,");
        AddLine("or Fetch to get a web page");
        AddLine("through it.");
        DrawLines();
        return true;
    case ctlSelectEvent:
        if (event->data.ctlSelect.controlID == MainPingButton) {
            PingFujiNet();
            return true;
        }
        if (event->data.ctlSelect.controlID == MainFetchButton) {
            FetchPage();
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

static Boolean AppHandleEvent(EventType *event)
{
    FormType *form;

    if (event->eType != frmLoadEvent)
        return false;
    form = FrmInitForm(event->data.frmLoad.formID);
    FrmSetActiveForm(form);
    FrmSetEventHandler(form, MainFormHandleEvent);
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

/* Palm OS 3.1, the oldest release the app is tested on. */
static Boolean RomCompatible(void)
{
    UInt32 rom = 0;

    FtrGet(sysFtrCreator, sysFtrNumROMVersion, &rom);
    return rom >= sysMakeROMVersion(3, 1, 0, sysROMStageRelease, 0);
}

UInt32 PilotMain(UInt16 cmd, MemPtr cmdPBP, UInt16 launchFlags)
{
    if (cmd != sysAppLaunchCmdNormalLaunch)
        return 0;
    if (!RomCompatible()) {
        FrmAlert(RomIncompatibleAlert);
        return 0;
    }
    FrmGotoForm(MainForm);
    EventLoop();
    FrmCloseAllForms();
    fn_shutdown();
    return 0;
}
