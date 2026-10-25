/* FN Test: asks the FujiNet on the cradle for its network settings
 * over FujiBus, as a quick check that the link works. */
#include <PalmOS.h>

#include "fnlink.h"
#include "FujiNetTestRsc.h"

#define MAX_LINES 8
#define LINE_CHARS 40
#define TEXT_TOP 20
#define LINE_HEIGHT 12

static Char gLines[MAX_LINES][LINE_CHARS];
static UInt16 gLineCount;

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

static void PingFujiNet(void)
{
    FnAdapterConfig config;
    Char line[LINE_CHARS];
    Err err;

    gLineCount = 0;
    AddLine("Contacting FujiNet...");
    DrawLines();

    err = FnGetAdapterConfig(&config);
    gLineCount = 0;
    if (err == fnErrNoReply) {
        AddLine("No answer from FujiNet.");
        AddLine("Is the cradle cabled to it?");
    } else if (err != errNone) {
        StrPrintF(line, "Error %x", err);
        AddLine(line);
    } else {
        config.ssid[sizeof(config.ssid) - 1] = '\0';
        config.fn_version[sizeof(config.fn_version) - 1] = '\0';
        AddLine("FujiNet found!");
        StrPrintF(line, "Firmware %s", config.fn_version);
        AddLine(line);
        StrPrintF(line, "IP %d.%d.%d.%d", config.localIP[0], config.localIP[1],
                  config.localIP[2], config.localIP[3]);
        AddLine(line);
        StrPrintF(line, "WiFi %s", config.ssid);
        AddLine(line);
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
        AddLine("on the cradle serial port.");
        DrawLines();
        return true;
    case ctlSelectEvent:
        if (event->data.ctlSelect.controlID == MainPingButton) {
            PingFujiNet();
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
    FnClose();
    return 0;
}
