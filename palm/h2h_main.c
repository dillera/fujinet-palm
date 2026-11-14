/* The app: a settings form that runs the suite and shows its progress, and a
 * results form with the report, which can be sent to the h2h server (where
 * the runs of every stack are compared) or saved to Memo Pad. */
#include <PalmOS.h>
#include <DLServer.h>  /* DlkGetSyncInfo: the HotSync user name */

#include "h2h.h"
#include "h2h_rsc.h"

#ifndef H2H_NAME
#define H2H_NAME "H2H"
#endif
#ifndef H2H_DEFAULT_TARGET
#define H2H_DEFAULT_TARGET "http://fujinet.online:8199/h2h"
#endif

#define FOURCC(s) (((UInt32)(UInt8)(s)[0] << 24) | ((UInt32)(UInt8)(s)[1] << 16) | \
                   ((UInt32)(UInt8)(s)[2] << 8) | (UInt32)(UInt8)(s)[3])
#define APP_CREATOR FOURCC(APP_CREATOR_STR)
/* Copy to RAM's name and creator for this app's RAM copy (FI H2H SB: FI H2H
 * RAM, ANHR), so the two sit side by side in the Launcher. */
#ifndef H2H_RAM_NAME
#define H2H_RAM_NAME H2H_NAME " RAM"
#endif
#ifndef H2H_RAM_CREATOR_STR
#define H2H_RAM_CREATOR_STR APP_CREATOR_STR
#endif
#define PREFS_ID 0
/* 2: the default target moved to fujinet.online, and the tester's name
 * came in, so settings saved by earlier builds (pointing at a Mac on the
 * LAN) start afresh. */
#define PREFS_VERSION 2
#define REPORT_SIZE 6000
#define MEMO_MAX 4000

/* The progress lines, between the Name field (y 82-93) and the buttons (y 147). */
#define LOG_LINES 4
#define LOG_CHARS 40
#define LOG_TOP 96
#define LOG_HEIGHT 12

static Settings gSettings;
static Char gTitle[dmDBNameLength];   /* this copy's own name */
static MemHandle gReport;
static Boolean gHaveResults, gLogVisible;
static Char gLog[LOG_LINES][LOG_CHARS];
static UInt16 gLogCount;

/* ---------------------------------------------------------------- progress */

static void DrawLog(void)
{
    RectangleType r;
    UInt16 i;

    if (!gLogVisible)
        return;
    RctSetRectangle(&r, 0, LOG_TOP, 160, LOG_LINES * LOG_HEIGHT);
    WinEraseRectangle(&r, 0);
    for (i = 0; i < gLogCount; i++)
        WinDrawChars(gLog[i], StrLen(gLog[i]), 2, LOG_TOP + i * LOG_HEIGHT);
}

static void DrawHeader(void)
{
    FontID old = FntSetFont(boldFont);
    const Char *t = link_title();
    RectangleType r;

    RctSetRectangle(&r, 0, 15, 160, 13);
    WinEraseRectangle(&r, 0);
    WinDrawChars(t, StrLen(t), 2, 16);
    FntSetFont(old);
}

void ui_progress(const Char *line)
{
    UInt16 i;

    if (gLogCount == LOG_LINES) {
        for (i = 1; i < LOG_LINES; i++)
            StrCopy(gLog[i - 1], gLog[i]);
        gLogCount--;
    }
    StrNCopy(gLog[gLogCount], line, LOG_CHARS - 1);
    gLog[gLogCount][LOG_CHARS - 1] = '\0';
    gLogCount++;
    EvtResetAutoOffTimer();
    DrawLog();
}

static void ClearLog(void)
{
    gLogCount = 0;
    DrawLog();
}

/* A tap or a key stops the run; an app switch stops it and then happens. */
Boolean ui_stop_requested(void)
{
    EventType e;

    if (!EvtSysEventAvail(true))
        return false;
    EvtGetEvent(&e, 0);
    if (e.eType == penDownEvent || e.eType == keyDownEvent)
        return true;
    if (e.eType == appStopEvent) {
        EvtAddEventToQueue(&e);
        return true;
    }
    return false;
}

/* ---------------------------------------------------------------- settings */

/* The name a tester starts with: the Palm's HotSync user name, if it has one. */
static void DefaultName(Char *name, UInt16 size)
{
    Char user[dlkUserNameBufSize];

    name[0] = '\0';
    user[0] = '\0';
    if (DlkGetSyncInfo(NULL, NULL, NULL, user, NULL, NULL) == errNone && user[0]) {
        StrNCopy(name, user, size - 1);
        name[size - 1] = '\0';
    }
}

static void LoadSettings(void)
{
    UInt16 size = sizeof(gSettings);

    if (PrefGetAppPreferences(APP_CREATOR, PREFS_ID, &gSettings, &size, true) != PREFS_VERSION ||
        size != sizeof(gSettings)) {
        MemSet(&gSettings, sizeof(gSettings), 0);
        StrCopy(gSettings.target, H2H_DEFAULT_TARGET);
        gSettings.reps = 3;
        gSettings.big = true;
        gSettings.sweep = true;
    }
    if (!gSettings.name[0])
        DefaultName(gSettings.name, sizeof(gSettings.name));
}

static void SaveSettings(void)
{
    PrefSetAppPreferences(APP_CREATOR, PREFS_ID, PREFS_VERSION, &gSettings, sizeof(gSettings), true);
}

static void *Ptr(FormType *frm, UInt16 id)
{
    return FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, id));
}

static void SetFieldText(FieldType *fld, const Char *text)
{
    MemHandle h = FldGetTextHandle(fld);
    Char *p;

    FldSetTextHandle(fld, NULL);
    if (h)
        MemHandleFree(h);
    if (!(h = MemHandleNew(StrLen(text) + 1)))
        return;
    p = MemHandleLock(h);
    StrCopy(p, text);
    MemHandleUnlock(h);
    FldSetTextHandle(fld, h);
}

static void ShowSettings(FormType *frm)
{
    Char reps[4];

    SetFieldText(Ptr(frm, MainTargetField), gSettings.target);
    StrPrintF(reps, "%u", gSettings.reps);
    SetFieldText(Ptr(frm, MainRepsField), reps);
    CtlSetValue(Ptr(frm, MainBigCheck), gSettings.big);
    CtlSetValue(Ptr(frm, MainSweepCheck), gSettings.sweep);
    SetFieldText(Ptr(frm, MainNameField), gSettings.name);
}

static void ReadSettings(FormType *frm)
{
    const Char *t = FldGetTextPtr(Ptr(frm, MainTargetField));
    const Char *r = FldGetTextPtr(Ptr(frm, MainRepsField));
    const Char *w = FldGetTextPtr(Ptr(frm, MainNameField));
    UInt16 n, i, j = 0;

    if (t) {
        /* No spaces or line breaks; no slash at the end. */
        for (i = 0; t[i] && j < sizeof(gSettings.target) - 1; i++)
            if (t[i] != ' ' && t[i] != '\n')
                gSettings.target[j++] = t[i];
        while (j > 0 && gSettings.target[j - 1] == '/')
            j--;
        gSettings.target[j] = '\0';
    }
    n = r ? (UInt16)StrAToI(r) : 0;
    gSettings.reps = n < 1 ? 1 : (n > 20 ? 20 : n);
    gSettings.big = CtlGetValue(Ptr(frm, MainBigCheck));
    gSettings.sweep = CtlGetValue(Ptr(frm, MainSweepCheck));
    /* The name without spaces at either end. */
    for (i = 0; w && w[i] == ' '; i++)
        ;
    for (j = 0; w && w[i] && j < sizeof(gSettings.name) - 1; i++)
        gSettings.name[j++] = w[i];
    while (j > 0 && gSettings.name[j - 1] == ' ')
        j--;
    gSettings.name[j] = '\0';
    SaveSettings();
}

/* ---------------------------------------------------------------- results */

static void BuildReport(void)
{
    Char *p;

    if (!gReport && !(gReport = MemHandleNew(REPORT_SIZE)))
        return;
    p = MemHandleLock(gReport);
    suite_report(p, REPORT_SIZE);
    MemHandleUnlock(gReport);
    gHaveResults = true;
}

static void UpdateScroll(FormType *frm)
{
    UInt16 pos, height, total, max;

    FldGetScrollValues(Ptr(frm, ResultsField), &pos, &total, &height);
    max = total > height ? total - height : 0;
    SclSetScrollBar(Ptr(frm, ResultsScroll), pos, 0, max, height > 1 ? height - 1 : 1);
}

static void Scroll(FormType *frm, Int16 lines)
{
    FieldType *fld = Ptr(frm, ResultsField);

    if (lines < 0)
        FldScrollField(fld, -lines, winUp);
    else if (lines > 0)
        FldScrollField(fld, lines, winDown);
    UpdateScroll(frm);
}

static void SaveMemo(void)
{
    DmOpenRef db;
    MemHandle h;
    UInt16 index = dmMaxRecordIndex, len;
    Char *text, *rec;
    Err err = dmErrCantFind;

    if (!gReport)
        return;
    db = DmOpenDatabaseByTypeCreator('DATA', 'memo', dmModeReadWrite);
    if (db) {
        text = MemHandleLock(gReport);
        len = StrLen(text);
        if (len > MEMO_MAX)
            len = MEMO_MAX;
        h = DmNewRecord(db, &index, len + 1);
        if (h) {
            rec = MemHandleLock(h);
            DmWrite(rec, 0, text, len);
            DmSet(rec, len, 1, 0);
            MemHandleUnlock(h);
            DmReleaseRecord(db, index, true);
            err = errNone;
        }
        MemHandleUnlock(gReport);
        DmCloseDatabase(db);
    }
    FrmCustomAlert(InfoAlert, err == errNone ? "Saved to Memo Pad." : "Memo Pad's database is missing.",
                   "", "");
}

static void SendResults(void)
{
    Char line[48];
    UInt16 sent = 0;
    Err err;

    if (!gSettings.name[0]) {
        FrmCustomAlert(InfoAlert, "Put your name in Name on the main screen first, ",
                       "so your results stay apart from other testers'.", "");
        return;
    }
    err = suite_upload(&gSettings, &sent);
    if (err == errNone)
        StrPrintF(line, "Sent %u requests to the h2h server.", sent);
    else
        StrPrintF(line, "Sent %u, then: %s", sent, link_err_text(err));
    FrmCustomAlert(InfoAlert, line, "", "");
}

static Boolean ResultsHandleEvent(EventType *e)
{
    FormType *frm = FrmGetActiveForm();
    FieldType *fld;

    switch (e->eType) {
    case frmOpenEvent:
        fld = Ptr(frm, ResultsField);
        /* Afresh each time, so a name typed after the run is in it (and in Memo). */
        if (gHaveResults)
            BuildReport();
        if (gReport)
            FldSetTextHandle(fld, gReport);
        FrmDrawForm(frm);
        UpdateScroll(frm);
        return true;
    case frmCloseEvent:
        FldSetTextHandle(Ptr(frm, ResultsField), NULL);
        return false;
    case sclRepeatEvent:
        Scroll(frm, e->data.sclRepeat.newValue - e->data.sclRepeat.value);
        return false;
    case keyDownEvent:
        if (e->data.keyDown.chr == pageUpChr) {
            Scroll(frm, -10);
            return true;
        }
        if (e->data.keyDown.chr == pageDownChr) {
            Scroll(frm, 10);
            return true;
        }
        return false;
    case ctlSelectEvent:
        switch (e->data.ctlSelect.controlID) {
        case ResultsDoneButton:
            FrmGotoForm(MainForm);
            return true;
        case ResultsSendButton:
            SendResults();
            return true;
        case ResultsMemoButton:
            SaveMemo();
            return true;
        }
        return false;
    default:
        return false;
    }
}

/* ---------------------------------------------------------------- main form */

static void Run(FormType *frm, Boolean pingOnly)
{
    ReadSettings(frm);
    ClearLog();
    ui_progress(pingOnly ? "Ping test. Tap to stop." : "Running. Tap to stop.");
    suite_run(&gSettings, pingOnly);
    BuildReport();
    FrmGotoForm(ResultsForm);
}

/* ---------------------------------------------------------------- copy to RAM */

/* Copies a resource database, resource by resource, into RAM (card 0) as
 * name/type/creator, replacing a database of that name there. On error no
 * half copy is left. */
static Err CopyResourceDb(UInt16 card, LocalID from, const Char *name, UInt32 dbType, UInt32 creator)
{
    UInt16 n, i, attr = 0, version = 0;
    LocalID id;
    DmOpenRef src = NULL, dst = NULL;
    DmResType type;
    DmResID resId;
    MemHandle h, nh;
    UInt32 size;
    Err err = errNone;

    id = DmFindDatabase(0, name);
    if (id)
        err = DmDeleteDatabase(0, id);
    id = 0;
    if (err == errNone)
        err = DmCreateDatabase(0, name, creator, dbType, true);
    if (err == errNone && (id = DmFindDatabase(0, name)) == 0)
        err = DmGetLastErr();
    if (err == errNone && (src = DmOpenDatabase(card, from, dmModeReadOnly)) == NULL)
        err = DmGetLastErr();
    if (err == errNone && (dst = DmOpenDatabase(0, id, dmModeReadWrite)) == NULL)
        err = DmGetLastErr();
    n = err == errNone ? DmNumResources(src) : 0;
    for (i = 0; i < n && err == errNone; i++) {
        if ((err = DmResourceInfo(src, i, &type, &resId, NULL)) != errNone)
            break;
        if ((h = DmGetResourceIndex(src, i)) == NULL) {
            err = DmGetLastErr();
            break;
        }
        size = MemHandleSize(h);
        if ((nh = DmNewResource(dst, type, resId, size)) == NULL) {
            err = DmGetLastErr();
        } else {
            DmWrite(MemHandleLock(nh), 0, MemHandleLock(h), size);
            MemHandleUnlock(nh);
            MemHandleUnlock(h);
            DmReleaseResource(nh);
        }
        DmReleaseResource(h);
    }
    if (dst)
        DmCloseDatabase(dst);
    if (src)
        DmCloseDatabase(src);
    if (err == errNone) {
        DmDatabaseInfo(card, from, NULL, &attr, &version, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        attr = dmHdrAttrResDB | dmHdrAttrBackup;
        err = DmSetDatabaseInfo(0, id, NULL, &attr, &version, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    }
    if (err != errNone && id)
        DmDeleteDatabase(0, id);   /* no half copy left behind */
    return err;
}

/* Copies this app into RAM as H2H_RAM_NAME, with its own creator so the two
 * sit side by side in the Launcher. Run from a Springboard module's ROM, the
 * Palm fetches every instruction over the bus; the copy runs from RAM and
 * labels its results so (link_stack). Settings are shared: both use this
 * build's creator for preferences. */
static void CopyToRam(void)
{
    UInt16 card = 0;
    LocalID self;
    Err err;
    Char line[96];   /* the longest message below, with a 31-character name, fits */

    if (SysCurAppDatabase(&card, &self) != errNone)
        return;
#ifndef H2H_COPY_ANY_CARD   /* defined only for the emulator test of this copy */
    if (card == 0) {
        FrmCustomAlert(InfoAlert, "This copy already runs from RAM.", "", "");
        return;
    }
#endif
    err = CopyResourceDb(card, self, H2H_RAM_NAME, sysFileTApplication, FOURCC(H2H_RAM_CREATOR_STR));
    if (err != errNone)
        StrPrintF(line, "Copy to RAM failed (error 0x%x).", err);
    else
        StrPrintF(line, "Copied to RAM as %s. Open it from the Launcher.", H2H_RAM_NAME);
    FrmCustomAlert(InfoAlert, line, "", "");
}

/* The module ROM carries NioLib as an inert database (type NLrm, so Palm OS
 * never loads it from the slow ROM); this copies it into RAM as the real
 * NioLib (libr/NiL1), which NioLib's own guide asks for. It will not replace
 * a NioLib that is loaded: that needs every NIO app ended first. */
#define NIOLIB_ROM_TYPE 'NLrm'
#define NIOLIB_CREATOR 'NiL1'

static void InstallNioLib(void)
{
    DmSearchStateType state;
    UInt16 card = 0, ref;
    LocalID from = 0;
    Err err;
    Char line[80];

    if (DmGetNextDatabaseByTypeCreator(true, &state, NIOLIB_ROM_TYPE, NIOLIB_CREATOR, false, &card, &from) != errNone ||
        !from) {
        FrmCustomAlert(InfoAlert, "The module ROM has no NioLib to install.", "", "");
        return;
    }
    if (SysLibFind("NioLib", &ref) == errNone) {
        FrmCustomAlert(InfoAlert, "NioLib is loaded: go Home, reset if need be, then try again.", "", "");
        return;
    }
    err = CopyResourceDb(card, from, "NioLib", sysFileTLibrary, NIOLIB_CREATOR);
    if (err != errNone)
        StrPrintF(line, "Install NioLib failed (error 0x%x).", err);
    else
        StrCopy(line, "NioLib is installed in RAM.");
    FrmCustomAlert(InfoAlert, line, "", "");
}

static Boolean MainHandleEvent(EventType *e)
{
    FormType *frm = FrmGetActiveForm();

    switch (e->eType) {
    case frmOpenEvent:
        FrmSetTitle(frm, gTitle);
        ShowSettings(frm);
        FrmDrawForm(frm);
        gLogVisible = true;
        DrawHeader();
        if (gLogCount == 0) {
            ui_progress("Check the target and your name,");
            ui_progress("then tap Run.");
            ui_progress(gHaveResults ? "Results: the last run." : "");
        } else {
            DrawLog();
        }
        return true;
    case frmCloseEvent:
        gLogVisible = false;
        ReadSettings(frm);
        return false;
    case menuEvent:
        if (e->data.menu.itemID == MainCopyToRamCmd) {
            ReadSettings(frm);
            SaveSettings();
            CopyToRam();
            return true;
        }
        if (e->data.menu.itemID == MainInstallNioLibCmd) {
            InstallNioLib();
            return true;
        }
        return false;
    case ctlSelectEvent:
        switch (e->data.ctlSelect.controlID) {
        case MainRunButton:
            Run(frm, false);
            return true;
        case MainPingButton:
            Run(frm, true);
            return true;
        case MainResultsButton:
            if (gHaveResults)
                FrmGotoForm(ResultsForm);
            else
                FrmCustomAlert(InfoAlert, "No results yet: tap Run.", "", "");
            return true;
        }
        return false;
    default:
        return false;
    }
}

/* ---------------------------------------------------------------- app */

static Boolean AppHandleEvent(EventType *e)
{
    FormType *frm;

    if (e->eType != frmLoadEvent)
        return false;
    frm = FrmInitForm(e->data.frmLoad.formID);
    FrmSetActiveForm(frm);
    if (e->data.frmLoad.formID == MainForm)
        FrmSetEventHandler(frm, MainHandleEvent);
    else
        FrmSetEventHandler(frm, ResultsHandleEvent);
    return true;
}

static void EventLoop(void)
{
    EventType e;
    Err err;

    do {
        EvtGetEvent(&e, evtWaitForever);
        if (SysHandleEvent(&e))
            continue;
        if (MenuHandleEvent(NULL, &e, &err))
            continue;
        if (AppHandleEvent(&e))
            continue;
        FrmDispatchEvent(&e);
    } while (e.eType != appStopEvent);
}

static Err RomVersionCompatible(UInt32 required, UInt16 launchFlags)
{
    UInt32 rom = 0;

    FtrGet(sysFtrCreator, sysFtrNumROMVersion, &rom);
    if (rom >= required)
        return errNone;
    if ((launchFlags & (sysAppLaunchFlagNewGlobals | sysAppLaunchFlagUIApp)) ==
        (sysAppLaunchFlagNewGlobals | sysAppLaunchFlagUIApp)) {
        FrmAlert(RomIncompatibleAlert);
        if (rom < sysMakeROMVersion(2, 0, 0, sysROMStageRelease, 0))
            AppLaunchWithCommand(sysFileCDefaultApp, sysAppLaunchCmdNormalLaunch, NULL);
    }
    return sysErrRomIncompatible;
}

UInt32 PilotMain(UInt16 cmd, MemPtr params, UInt16 launchFlags)
{
    Err err;

    (void)params;
    if (cmd != sysAppLaunchCmdNormalLaunch)
        return errNone;
    err = RomVersionCompatible(sysMakeROMVersion(3, 1, 0, sysROMStageRelease, 0), launchFlags);
    if (err != errNone)
        return err;
    {
        UInt16 card;
        LocalID self;

        StrCopy(gTitle, H2H_NAME);
        if (SysCurAppDatabase(&card, &self) == errNone)
            DmDatabaseInfo(card, self, gTitle, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    }
#ifdef H2H_TEST_NIOLIB_SERIAL
    /* Emulator test builds only (there is no Springboard there): point
     * NioLib at its serial transport, 115200, as NIO Config would. */
    {
        struct { UInt16 size, version, transport, serialOptions; UInt32 serialBaud, reserved[3]; } cfg;
        MemSet(&cfg, sizeof(cfg), 0);
        cfg.size = sizeof(cfg);
        cfg.version = 1;
        cfg.transport = 2;
        cfg.serialBaud = 115200;
        PrefSetAppPreferences('NiL1', 0x4e34, 1, &cfg, sizeof(cfg), true);
    }
#endif
    LoadSettings();
    FrmGotoForm(MainForm);
    EventLoop();
    FrmCloseAllForms();
    SaveSettings();
    if (gReport)
        MemHandleFree(gReport);
    return errNone;
}
