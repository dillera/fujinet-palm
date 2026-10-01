/* FN HotSync for Palm OS.
 *
 * Browses and edits FujiNet's host slots, queues Palm apps and databases for the next
 * HotSync by copying them into /palm/install on FujiNet's SD card, and
 * starts that HotSync. It talks to FujiNet with FujiBus over the cradle,
 * the way FujiNet CONFIG does on RS232 machines.
 */
#include <PalmOS.h>

#include "fnlink.h"
#include "FujiConfigRsc.h"

#define MAX_ENTRIES 100
#define PATH_LEN 200
#define TITLE_LEN 40
#define INSTALL_FOLDER "/palm/install/"
#define SD_HOST_NAME "SD"
#define EMPTY_SLOT "(empty)"

static char gHosts[FN_MAX_HOSTS][FN_HOSTNAME_LEN];
static char *gHostChoices[FN_MAX_HOSTS];
static Int16 gSdSlot = -1;

static UInt8 gSlot;
static UInt8 gEditSlot;
static char gPath[PATH_LEN];
static char gTitle[TITLE_LEN];

static char (*gEntries)[FN_DIR_ENTRY_LEN];
static char **gChoices;
static UInt16 gCount;

static void ShowMessage(const char *line1, const char *line2)
{
    FrmCustomAlert(MessageAlert, line1, line2 ? line2 : "", "");
}

static void ShowError(const char *what, Err err)
{
    char detail[64];

    if (err == fnErrNoReply)
        StrCopy(detail, "\nNo answer from FujiNet. Is the cradle cabled to it?");
    else if (err == fnErrRefused)
        StrCopy(detail, "\nFujiNet refused the request.");
    else
        StrPrintF(detail, "\nError %x.", err);
    ShowMessage(what, detail);
}

static void SetBusy(FormType *form, const char *title)
{
    StrNCopy(gTitle, title, TITLE_LEN - 1);
    gTitle[TITLE_LEN - 1] = '\0';
    FrmSetTitle(form, gTitle);
}

static Boolean EndsWith(const char *s, const char *suffix)
{
    UInt16 n = StrLen(s), m = StrLen(suffix);
    return n >= m && StrCaselessCompare(s + n - m, suffix) == 0;
}

static Boolean IsFolder(const char *name)
{
    UInt16 n = StrLen(name);
    return n > 0 && name[n - 1] == '/';
}

static Boolean IsInstallable(const char *name)
{
    return EndsWith(name, ".prc") || EndsWith(name, ".pdb") || EndsWith(name, ".pqa");
}

/* ---- FujiNet -------------------------------------------------------- */

static Err LoadHosts(void)
{
    UInt16 i;
    Err err = FnReadHostSlots(gHosts);

    gSdSlot = -1;
    for (i = 0; i < FN_MAX_HOSTS; i++) {
        gHostChoices[i] = gHosts[i][0] ? gHosts[i] : EMPTY_SLOT;
        if (err == errNone && gSdSlot < 0 && StrCaselessCompare(gHosts[i], SD_HOST_NAME) == 0)
            gSdSlot = i;
    }
    return err;
}

/* Reads a whole directory into gEntries, keeping folders unless filesOnly. */
static Err ReadDirectory(UInt8 slot, const char *path, Boolean filesOnly)
{
    Boolean more = true;
    UInt16 index = 0;
    Err err;

    gCount = 0;
    if ((err = FnMountHost(slot)) != errNone || (err = FnOpenDirectory(slot, path)) != errNone)
        return err;
    while (gCount < MAX_ENTRIES) {
        err = FnReadDirEntry(index++, gEntries[gCount], &more);
        if (err != errNone || !more)
            break;
        if (filesOnly && IsFolder(gEntries[gCount]))
            continue;
        gChoices[gCount] = gEntries[gCount];
        gCount++;
    }
    FnCloseDirectory();
    return err;
}

static void StartHotSync(void)
{
    DmSearchStateType search;
    UInt16 card;
    LocalID app;

    FnClose();
    if (DmGetNextDatabaseByTypeCreator(true, &search, sysFileTApplication, sysFileCSync, true,
                                       &card, &app) != errNone || app == 0) {
        ShowMessage("The HotSync app is missing.", NULL);
        return;
    }
    SysUIAppSwitch(card, app, sysAppLaunchCmdSyncRequestLocal, NULL);
}

/* ---- Main form: host slots ------------------------------------------ */

static void MainShowHosts(FormType *form)
{
    ListType *list = FrmGetObjectPtr(form, FrmGetObjectIndex(form, MainHostList));
    Err err;

    SetBusy(form, "Asking FujiNet...");
    err = LoadHosts();
    SetBusy(form, "FN HotSync");
    LstSetListChoices(list, gHostChoices, FN_MAX_HOSTS);
    LstSetSelection(list, noListSelection);
    LstDrawList(list);
    if (err != errNone)
        ShowError("Could not read the host slots.", err);
}

static void MainBrowse(FormType *form)
{
    ListType *list = FrmGetObjectPtr(form, FrmGetObjectIndex(form, MainHostList));
    Int16 slot = LstGetSelection(list);

    if (slot == noListSelection || gHosts[slot][0] == '\0') {
        ShowMessage("Pick a host slot to browse.", NULL);
        return;
    }
    gSlot = (UInt8)slot;
    StrCopy(gPath, "/");
    FrmGotoForm(BrowseForm);
}

static void MainEdit(FormType *form)
{
    ListType *list = FrmGetObjectPtr(form, FrmGetObjectIndex(form, MainHostList));
    Int16 slot = LstGetSelection(list);

    if (slot == noListSelection) {
        ShowMessage("Pick a host slot to edit.", NULL);
        return;
    }
    gEditSlot = (UInt8)slot;
    FrmGotoForm(EditForm);
}

static void MainInfo(void)
{
    FnAdapterConfig config;
    char text[120];
    Err err = FnGetAdapterConfig(&config);

    if (err != errNone) {
        ShowError("Could not read FujiNet's settings.", err);
        return;
    }
    config.ssid[sizeof(config.ssid) - 1] = '\0';
    config.fn_version[sizeof(config.fn_version) - 1] = '\0';
    StrPrintF(text, "Wi-Fi: %s\nIP: %d.%d.%d.%d\nFirmware: %s", config.ssid,
              config.localIP[0], config.localIP[1], config.localIP[2], config.localIP[3],
              config.fn_version);
    ShowMessage(text, NULL);
}

static Boolean MainHandleEvent(EventType *event)
{
    FormType *form = FrmGetActiveForm();

    switch (event->eType) {
    case frmOpenEvent:
        FrmDrawForm(form);
        MainShowHosts(form);
        return true;
    case ctlSelectEvent:
        switch (event->data.ctlSelect.controlID) {
        case MainBrowseButton:
            MainBrowse(form);
            return true;
        case MainQueueButton:
            if (gSdSlot < 0)
                ShowMessage("No host slot is named SD, where queued files go.", NULL);
            else
                FrmGotoForm(QueueForm);
            return true;
        case MainInfoButton:
            MainInfo();
            return true;
        case MainEditButton:
            MainEdit(form);
            return true;
        case MainHotSyncButton:
            StartHotSync();
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

/* ---- Browse form ----------------------------------------------------- */

static void BrowseLoad(FormType *form)
{
    ListType *list = FrmGetObjectPtr(form, FrmGetObjectIndex(form, BrowseList));
    Err err;

    SetBusy(form, "Reading...");
    err = ReadDirectory(gSlot, gPath, false);
    StrPrintF(gTitle, "%s:", gHosts[gSlot]);
    StrNCat(gTitle, gPath, TITLE_LEN);
    FrmSetTitle(form, gTitle);
    LstSetListChoices(list, gChoices, gCount);
    LstSetSelection(list, noListSelection);
    LstDrawList(list);
    if (err != errNone)
        ShowError("Could not read this folder.", err);
}

static void BrowseUp(FormType *form)
{
    UInt16 n = StrLen(gPath);

    if (n <= 1)
        return;
    /* Drop the trailing '/' and the last folder name. */
    n--;
    while (n > 0 && gPath[n - 1] != '/')
        n--;
    gPath[n] = '\0';
    BrowseLoad(form);
}

static void BrowseQueue(const char *name)
{
    char path[PATH_LEN];
    Err err;

    if (gSdSlot < 0) {
        ShowMessage("No host slot is named SD, where queued files go.", NULL);
        return;
    }
    if (FrmCustomAlert(ConfirmQueueAlert, name, "", "") != 0)
        return;
    if (StrLen(gPath) + StrLen(name) >= PATH_LEN)
        return;
    StrCopy(path, gPath);
    StrCat(path, name);
    err = FnCopyFile(gSlot, path, (UInt8)gSdSlot, INSTALL_FOLDER);
    if (err != errNone)
        ShowError("Could not queue that file.", err);
    else
        ShowMessage(name, " is queued. Tap HotSync to install it.");
}

static void BrowseOpen(FormType *form)
{
    ListType *list = FrmGetObjectPtr(form, FrmGetObjectIndex(form, BrowseList));
    Int16 item = LstGetSelection(list);
    const char *name;

    if (item == noListSelection)
        return;
    name = gEntries[item];
    if (IsFolder(name)) {
        if (StrLen(gPath) + StrLen(name) >= PATH_LEN)
            return;
        StrCat(gPath, name);
        BrowseLoad(form);
    } else if (IsInstallable(name)) {
        BrowseQueue(name);
    } else {
        ShowMessage("Only .prc, .pdb and .pqa files install on a Palm.", NULL);
    }
}

static Boolean BrowseHandleEvent(EventType *event)
{
    FormType *form = FrmGetActiveForm();

    switch (event->eType) {
    case frmOpenEvent:
        FrmDrawForm(form);
        BrowseLoad(form);
        return true;
    case ctlSelectEvent:
        switch (event->data.ctlSelect.controlID) {
        case BrowseOpenButton:
            BrowseOpen(form);
            return true;
        case BrowseUpButton:
            BrowseUp(form);
            return true;
        case BrowseDoneButton:
            FrmGotoForm(MainForm);
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

/* ---- Edit form: one host slot ---------------------------------------- */

static FieldType *EditGetField(FormType *form)
{
    return FrmGetObjectPtr(form, FrmGetObjectIndex(form, EditField));
}

static void EditShow(FormType *form)
{
    FieldType *field = EditGetField(form);
    MemHandle text = MemHandleNew(FN_HOSTNAME_LEN);

    if (text != NULL) {
        StrCopy(MemHandleLock(text), gHosts[gEditSlot]);
        MemHandleUnlock(text);
        FldSetTextHandle(field, text);
    }
    FrmDrawForm(form);
    FrmSetFocus(form, FrmGetObjectIndex(form, EditField));
}

static void EditSave(FormType *form)
{
    char hosts[FN_MAX_HOSTS][FN_HOSTNAME_LEN];
    const char *text = FldGetTextPtr(EditGetField(form));
    Err err;

    MemMove(hosts, gHosts, sizeof(hosts));
    MemSet(hosts[gEditSlot], FN_HOSTNAME_LEN, 0);
    if (text)
        StrNCopy(hosts[gEditSlot], text, FN_HOSTNAME_LEN - 1);
    err = FnWriteHostSlots(hosts);
    if (err != errNone) {
        ShowError("Could not save the host slots.", err);
        return;
    }
    FrmGotoForm(MainForm);
}

static Boolean EditHandleEvent(EventType *event)
{
    FormType *form = FrmGetActiveForm();

    switch (event->eType) {
    case frmOpenEvent:
        EditShow(form);
        return true;
    case ctlSelectEvent:
        switch (event->data.ctlSelect.controlID) {
        case EditSaveButton:
            EditSave(form);
            return true;
        case EditCancelButton:
            FrmGotoForm(MainForm);
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

/* ---- Queue form ------------------------------------------------------ */

static void QueueLoad(FormType *form)
{
    ListType *list = FrmGetObjectPtr(form, FrmGetObjectIndex(form, QueueList));
    Err err;

    SetBusy(form, "Reading...");
    err = ReadDirectory((UInt8)gSdSlot, INSTALL_FOLDER, true);
    SetBusy(form, gCount ? "Queued for HotSync" : "Nothing queued");
    LstSetListChoices(list, gChoices, gCount);
    LstSetSelection(list, noListSelection);
    LstDrawList(list);
    if (err != errNone)
        ShowError("Could not read the queue.", err);
}

static Boolean QueueHandleEvent(EventType *event)
{
    FormType *form = FrmGetActiveForm();

    switch (event->eType) {
    case frmOpenEvent:
        FrmDrawForm(form);
        QueueLoad(form);
        return true;
    case ctlSelectEvent:
        switch (event->data.ctlSelect.controlID) {
        case QueueHotSyncButton:
            StartHotSync();
            return true;
        case QueueDoneButton:
            FrmGotoForm(MainForm);
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
    switch (event->data.frmLoad.formID) {
    case MainForm:
        FrmSetEventHandler(form, MainHandleEvent);
        break;
    case BrowseForm:
        FrmSetEventHandler(form, BrowseHandleEvent);
        break;
    case QueueForm:
        FrmSetEventHandler(form, QueueHandleEvent);
        break;
    case EditForm:
        FrmSetEventHandler(form, EditHandleEvent);
        break;
    }
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
    gEntries = MemPtrNew(MAX_ENTRIES * FN_DIR_ENTRY_LEN);
    gChoices = MemPtrNew(MAX_ENTRIES * sizeof(char *));
    if (gEntries == NULL || gChoices == NULL)
        return memErrNotEnoughSpace;
    return FnOpen();
}

static void AppStop(void)
{
    FrmCloseAllForms();
    FnClose();
    if (gEntries)
        MemPtrFree(gEntries);
    if (gChoices)
        MemPtrFree(gChoices);
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
        ShowError("Could not open the cradle port.", err);
        AppStop();
        return 0;
    }
    FrmGotoForm(MainForm);
    EventLoop();
    AppStop();
    return 0;
}
