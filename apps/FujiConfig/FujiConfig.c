/* FI Config for Palm OS.
 *
 * Keeps a list of TNFS servers, browses their folders and installs Palm apps
 * and databases from them, streaming each file into the Palm's memory with
 * ExgDBRead. On the cradle it can instead queue a file for the next HotSync,
 * copying it into the HotSync install folder on FujiNet NIO's persistent
 * filesystem, and start that HotSync. It talks to FujiNet NIO through
 * fujinet-nio-lib: its FileDevice for the files (filedev.c), FujiDevice and
 * WiFi for Info.
 *
 * The Springboard build (FI_NO_HOTSYNC) leaves out the queue and HotSync: a
 * Visor's module has no HotSync behind it.
 */
#include <PalmOS.h>

#include "fn_types.h"
#include "fujinet-nio.h"
#include "filedev.h"
#include "FujiConfigRsc.h"

#define CREATOR APP_CREATOR
#define PREFS_ID 1
#define PREFS_VERSION 1

#define MAX_SERVERS 8
#define HOST_LEN 32
#define MAX_ENTRIES 100
#define PATH_LEN 200
#define TITLE_LEN 40
#define EMPTY_SLOT "(empty)"
/* NIO's HotSync installs what is in here (hotsync.root is /hotsync). */
#define QUEUE_URI "persist:///hotsync/install/"
#define QUEUE_PARENT_URI "persist:///hotsync/"

/* A Palm database's 78-byte header ends with its entry count; then come
 * 10-byte resource entries, offset at +6, or 8-byte record entries, offset
 * first. Bit 0 of the attributes marks a resource database. */
#define PDB_HEADER_LEN 78
#define PDB_ATTR_LOW 33
#define PDB_COUNT 76

typedef struct {
    char hosts[MAX_SERVERS][HOST_LEN];
} Prefs;

static Prefs gPrefs;
static char *gHostChoices[MAX_SERVERS];

static UInt8 gServer;
static UInt8 gEditServer;
static char gPath[PATH_LEN];
static char gTitle[TITLE_LEN];

static FdEntry *gEntries;
static char **gChoices;
static UInt16 gCount;
static UInt8 gChunk[FD_CHUNK];

static void ShowMessage(const char *line1, const char *line2)
{
    FrmCustomAlert(MessageAlert, line1, line2 ? line2 : "", "");
}

#ifdef FI_NO_HOTSYNC
#define NO_ANSWER "\nNo answer from FujiNet NIO. Is it running?"
#define NO_LINK "Could not find the FujiNet module."
#else
#define NO_ANSWER "\nNo answer from FujiNet. Is the cradle cabled to it?"
#define NO_LINK "Could not open the cradle port."
#endif

static void ShowError(const char *what, uint8_t err)
{
    char detail[64];

    if (err == FN_ERR_TIMEOUT)
        StrCopy(detail, NO_ANSWER);
    else
        StrPrintF(detail, "\nFujiNet: %s.", fn_error_string(err));
    ShowMessage(what, detail);
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

static void SetBusy(FormType *form, const char *title)
{
    StrNCopy(gTitle, title, TITLE_LEN - 1);
    gTitle[TITLE_LEN - 1] = '\0';
    SetFormTitle(form, gTitle);
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

/* ---- Preferences: the TNFS servers ----------------------------------- */

static void SetDefaults(void)
{
    MemSet(&gPrefs, sizeof(gPrefs), 0);
    StrCopy(gPrefs.hosts[0], "fujinet.diller.org");
    StrCopy(gPrefs.hosts[1], "apps.irata.online");
}

static void LoadPrefs(void)
{
    UInt16 size = sizeof(gPrefs), i;

    if (PrefGetAppPreferences(CREATOR, PREFS_ID, &gPrefs, &size, true) != PREFS_VERSION ||
        size != sizeof(gPrefs))
        SetDefaults();
    for (i = 0; i < MAX_SERVERS; i++) {
        gPrefs.hosts[i][HOST_LEN - 1] = '\0';
        gHostChoices[i] = gPrefs.hosts[i][0] ? gPrefs.hosts[i] : EMPTY_SLOT;
    }
}

static void SavePrefs(void)
{
    PrefSetAppPreferences(CREATOR, PREFS_ID, PREFS_VERSION, &gPrefs, sizeof(gPrefs), true);
}

/* ---- FujiNet -------------------------------------------------------- */

/* Reads a whole folder into gEntries, keeping folders unless filesOnly. */
static uint8_t ReadDirectory(const char *uri, Boolean filesOnly)
{
    UInt16 start = 0, next, got, i, kept;
    Boolean more = true;
    uint8_t err = FN_OK;

    gCount = 0;
    while (more && gCount < MAX_ENTRIES) {
        err = FdList(uri, start, gEntries + gCount, MAX_ENTRIES - gCount, &got, &next, &more);
        if (err != FN_OK)
            break;
        gCount += got;
        if (next == start)
            break; /* a name too long for a page: stop rather than ask again */
        start = next;
    }
    kept = 0;
    for (i = 0; i < gCount; i++) {
        if (filesOnly && IsFolder(gEntries[i].name))
            continue;
        if (kept != i)
            gEntries[kept] = gEntries[i];
        gChoices[kept] = gEntries[kept].name;
        kept++;
    }
    gCount = kept;
    return err;
}

/* The server's URI for a name in the current folder, or false if too long. */
static Boolean ServerUri(const char *name, char *uri)
{
    const char *host = gPrefs.hosts[gServer];

    if (7 + StrLen(host) + StrLen(gPath) + StrLen(name) > FD_URI_LEN)
        return false;
    StrCopy(uri, "tnfs://");
    StrCat(uri, host);
    StrCat(uri, gPath);
    StrCat(uri, name);
    return true;
}

/* The copy's progress goes on the free line above the browse list. */
#define PROGRESS_TOP 15
#define PROGRESS_HEIGHT 12

static void ClearProgress(void)
{
    RectangleType rect;

    RctSetRectangle(&rect, 0, PROGRESS_TOP, 160, PROGRESS_HEIGHT);
    WinEraseRectangle(&rect, 0);
}

static void ShowProgress(const char *doing, UInt32 done, UInt32 size, UInt16 *shown)
{
    char line[48];
    UInt16 percent = size ? (UInt16)(done * 100 / size) : 0;

    if (percent == *shown)
        return;
    *shown = percent;
    StrPrintF(line, "%s: %lu of %lu bytes", doing, done, size);
    ClearProgress();
    WinDrawChars(line, StrLen(line), 2, PROGRESS_TOP);
}

/* ---- Installing on this Palm ---------------------------------------- */

/* One file streaming from the server into ExgDBRead. */
typedef struct {
    const char *uri;
    UInt32 offset;    /* of the next FileDevice read */
    UInt32 size;
    UInt16 shown;
    UInt16 held;      /* bytes in gChunk not yet given to ExgDBRead */
    UInt16 next;      /* where they start */
    uint8_t err;      /* FujiNet's error, if a read failed */
    Boolean eof;
    Boolean declined; /* the user kept the copy already installed */
    Boolean self;     /* the file would replace this app while it runs */
    Boolean inUse;    /* the copy already installed could not be deleted */
} Install;

/* ExgDBRead's read callback. ExgDBRead asks for a few bytes at a time (a
 * header, then each 10-byte entry), and every FileDevice read costs NIO a
 * TNFS open, read and close, so this reads whole chunks into gChunk and hands
 * them out from there. It fills the buffer and comes back short only at the
 * end of the file. A reply lost on the way is asked for again at the same
 * offset (see filedev.c). */
static Err InstallRead(void *data, UInt32 *size, void *user)
{
    Install *in = user;
    UInt32 want = *size, done = 0;
    UInt16 take;

    while (done < want) {
        if (in->held == 0) {
            if (in->eof)
                break;
            in->next = 0;
            in->err = FdRead(in->uri, in->offset, gChunk, FD_CHUNK, &in->held, &in->eof);
            if (in->err != FN_OK)
                break;
            if (in->held == 0)
                in->eof = true; /* nothing more to come, whatever the flag said */
            in->offset += in->held;
            ShowProgress("Installing", in->offset, in->size, &in->shown);
            EvtResetAutoOffTimer();
            continue;
        }
        take = want - done < in->held ? (UInt16)(want - done) : in->held;
        MemMove((UInt8 *)data + done, gChunk + in->next, take);
        in->next += take;
        in->held -= take;
        done += take;
    }
    *size = done;
    return in->err == FN_OK ? errNone : exgErrUnknown;
}

/* ExgDBRead's callback for a database of the same name already on the Palm:
 * true once it is out of the way. */
static Boolean InstallReplace(const char *name, UInt16 version, UInt16 card, LocalID db,
                              void *user)
{
    Install *in = user;
    UInt16 ourCard;
    LocalID ours;

    /* One in ROM stays; the copy in RAM takes its place (an odd LocalID is
     * a RAM handle). */
    if ((db & 0x01) == 0)
        return true;
    if (SysCurAppDatabase(&ourCard, &ours) == errNone && ourCard == card && ours == db) {
        in->self = true;
        return false;
    }
    if (FrmCustomAlert(ConfirmReplaceAlert, name, "", "") != 0) {
        in->declined = true;
        return false;
    }
    if (DmDeleteDatabase(card, db) != errNone) {
        in->inUse = true;
        return false;
    }
    return true;
}

/* As HotSync does, an app is told it was just installed, so it can set up
 * its alarms or notifications. */
static void NotifyInstalled(LocalID db)
{
    UInt32 type = 0, result;

    if (DmDatabaseInfo(0, db, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &type,
                       NULL) == errNone &&
        type == sysFileTApplication)
        SysAppLaunch(0, db, 0, sysAppLaunchCmdSyncNotify, NULL, &result);
}

static void InstallFailed(const char *name, const Install *in, Err err)
{
    char code[32];

    if (in->declined)
        return;
    if (in->self)
        ShowMessage("FI Config cannot replace itself while it runs.", NULL);
    else if (in->inUse)
        ShowMessage(name, " is already on this Palm and could not be deleted. Is it running?");
    else if (in->err != FN_OK)
        ShowError("Could not read that file.", in->err);
    else if (err == dmErrMemError || err == memErrNotEnoughSpace)
        ShowMessage("Not enough free memory for ", name);
    else if (err == dmErrCorruptDatabase || err == exgErrBadData)
        ShowMessage(name, " is not a Palm app or database.");
    else {
        StrPrintF(code, " (Palm OS error 0x%x)", err);
        ShowMessage("Could not install that file.", code);
    }
}

/* Streams a file from the server into the Palm's memory. ExgDBRead builds
 * the database as the bytes come and deletes it again if they stop short. */
static void InstallFile(const FdEntry *entry)
{
    char uri[FD_URI_LEN + 1];
    Install in;
    LocalID db = 0;
    Boolean reset = false;
    Err err;

    if (!ServerUri(entry->name, uri))
        return;
    MemSet(&in, sizeof(in), 0);
    in.uri = uri;
    in.size = entry->size;
    in.shown = 0xFFFF;
    err = ExgDBRead(InstallRead, InstallReplace, &in, &db, 0, &reset, true);
    ClearProgress();
    if (err != errNone) {
        InstallFailed(entry->name, &in, err);
        return;
    }
    NotifyInstalled(db);
    ShowMessage(entry->name, reset ? " is installed. Reset the Palm to finish." : " is installed.");
}

#ifndef FI_NO_HOTSYNC
/* ---- Queuing for HotSync -------------------------------------------- */

/* FileDevice can neither rename nor delete, so a copy cut short stays in
 * the queue, and HotSync would install what it holds. Before the first
 * chunk is written its first entry's offset is set past the end of any
 * file, so HotSync refuses the file (it reads "Install failed" in its log)
 * until the real offset, held back here, is written last. Returns where
 * those 4 bytes go, or 0 for a database with no entries. */
static UInt32 HoldBack(UInt8 *chunk, UInt16 len, UInt8 *saved)
{
    UInt16 at;

    if (len < PDB_HEADER_LEN || (chunk[PDB_COUNT] == 0 && chunk[PDB_COUNT + 1] == 0))
        return 0;
    at = PDB_HEADER_LEN + ((chunk[PDB_ATTR_LOW] & 0x01) ? 6 : 0);
    if (at + 4 > len)
        return 0;
    MemMove(saved, chunk + at, 4);
    MemSet(chunk + at, 4, 0xFF);
    return at;
}

/* Copies a file of size bytes from the server into the HotSync queue, a
 * chunk at a time with ReadFile and WriteFile, showing how far it has got.
 * A reply lost on the cradle is asked for again at the same offset (see
 * filedev.c). */
static uint8_t CopyToQueue(const char *from, const char *to, UInt32 size)
{
    UInt32 offset = 0, patchAt = 0;
    UInt16 got, shown = 0xFFFF;
    UInt8 patch[4];
    Boolean eof = false;
    uint8_t err;

    if ((err = FdMakeFolder(QUEUE_URI)) != FN_OK)
        return err;
    while (!eof) {
        ShowProgress("Queuing", offset, size, &shown);
        EvtResetAutoOffTimer();
        if ((err = FdRead(from, offset, gChunk, FD_CHUNK, &got, &eof)) != FN_OK)
            return err;
        if (got == 0)
            break;
        if (offset == 0)
            patchAt = HoldBack(gChunk, got, patch);
        if ((err = FdWrite(to, offset, gChunk, got)) != FN_OK)
            return err;
        offset += got;
    }
    ShowProgress("Queuing", offset, size, &shown);
    if (offset != size)
        return FN_ERR_IO; /* the server sent more or less than it listed */
    if (patchAt != 0)
        return FdWrite(to, patchAt, patch, 4);
    return FN_OK;
}

/* The queue's URI for name, made FAT-safe as NIO's HotSync names its files. */
static void QueueUri(const char *name, char *uri)
{
    UInt16 at = StrLen(QUEUE_URI), i;
    char c;

    StrCopy(uri, QUEUE_URI);
    for (i = 0; name[i] && i < FD_NAME_LEN; i++) {
        c = name[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == ' ' || c == '-' || c == '_' || c == '.'))
            c = '_';
        uri[at++] = c;
    }
    uri[at] = '\0';
}

static void StartHotSync(void)
{
    DmSearchStateType search;
    UInt16 card;
    LocalID app;

    fn_shutdown(); /* HotSync needs the cradle port */
    if (DmGetNextDatabaseByTypeCreator(true, &search, sysFileTApplication, sysFileCSync, true,
                                       &card, &app) != errNone || app == 0) {
        ShowMessage("The HotSync app is missing.", NULL);
        return;
    }
    SysUIAppSwitch(card, app, sysAppLaunchCmdSyncRequestLocal, NULL);
}
#endif /* FI_NO_HOTSYNC */

/* ---- Main form: TNFS servers ----------------------------------------- */

static void MainShowServers(FormType *form)
{
    ListType *list = FrmGetObjectPtr(form, FrmGetObjectIndex(form, MainHostList));

    LstSetListChoices(list, gHostChoices, MAX_SERVERS);
    LstSetSelection(list, noListSelection);
    LstDrawList(list);
}

static void MainBrowse(FormType *form)
{
    ListType *list = FrmGetObjectPtr(form, FrmGetObjectIndex(form, MainHostList));
    Int16 slot = LstGetSelection(list);

    if (slot == noListSelection || gPrefs.hosts[slot][0] == '\0') {
        ShowMessage("Pick a server to browse.", NULL);
        return;
    }
    gServer = (UInt8)slot;
    StrCopy(gPath, "/");
    FrmGotoForm(BrowseForm);
}

static void MainEdit(FormType *form)
{
    ListType *list = FrmGetObjectPtr(form, FrmGetObjectIndex(form, MainHostList));
    Int16 slot = LstGetSelection(list);

    if (slot == noListSelection) {
        ShowMessage("Pick a server to edit.", NULL);
        return;
    }
    gEditServer = (UInt8)slot;
    FrmGotoForm(EditForm);
}

static void MainInfo(void)
{
    fn_fuji_info_t info;
    fn_wifi_status_t status;
    fn_wifi_config_t config;
    char text[160];
    uint8_t err = fn_fuji_get_info(&info);

    if (err == FN_ERR_UNSUPPORTED) {
        StrCopy(info.firmware, "unknown");
        StrCopy(info.profile, "unknown");
    } else if (err != FN_OK) {
        ShowError("Could not reach FujiNet.", err);
        return;
    }
    if (fn_wifi_get_status(&status) != FN_OK)
        status.ip[0] = '\0';
    if (fn_wifi_get_config(&config) != FN_OK)
        config.ssid[0] = '\0';
    StrPrintF(text, "Wi-Fi: %s\nIP: %s\nFirmware: %s\nBoard: %s\nReplies asked again: %u",
              config.ssid[0] ? config.ssid : "unknown", status.ip[0] ? status.ip : "none",
              info.firmware, info.profile, gFdRetries);
    ShowMessage(text, NULL);
}

static Boolean MainHandleEvent(EventType *event)
{
    FormType *form = FrmGetActiveForm();

    switch (event->eType) {
    case frmOpenEvent:
        FrmDrawForm(form);
        MainShowServers(form);
        return true;
    case ctlSelectEvent:
        switch (event->data.ctlSelect.controlID) {
        case MainBrowseButton:
            MainBrowse(form);
            return true;
        case MainInfoButton:
            MainInfo();
            return true;
        case MainEditButton:
            MainEdit(form);
            return true;
#ifndef FI_NO_HOTSYNC
        case MainQueueButton:
            FrmGotoForm(QueueForm);
            return true;
        case MainHotSyncButton:
            StartHotSync();
            return true;
#endif
        }
        break;
    default:
        break;
    }
    return false;
}

/* ---- Browse form ----------------------------------------------------- */

static void BrowseSetTitle(FormType *form)
{
    StrPrintF(gTitle, "%s:", gPrefs.hosts[gServer]);
    StrNCat(gTitle, gPath, TITLE_LEN);
    SetFormTitle(form, gTitle);
}

static void BrowseLoad(FormType *form)
{
    ListType *list = FrmGetObjectPtr(form, FrmGetObjectIndex(form, BrowseList));
    char uri[FD_URI_LEN + 1];
    uint8_t err = FN_ERR_URL_TOO_LONG;

    SetBusy(form, "Reading...");
    gCount = 0;
    if (ServerUri("", uri))
        err = ReadDirectory(uri, false);
    BrowseSetTitle(form);
    LstSetListChoices(list, gChoices, gCount);
    LstSetSelection(list, noListSelection);
    LstDrawList(list);
    if (err != FN_OK)
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

#ifndef FI_NO_HOTSYNC
static void BrowseQueue(const FdEntry *entry)
{
    char from[FD_URI_LEN + 1], to[sizeof(QUEUE_URI) + FD_NAME_LEN];
    uint8_t err;

    if (!ServerUri(entry->name, from))
        return;
    QueueUri(entry->name, to);
    err = CopyToQueue(from, to, entry->size);
    ClearProgress();
    if (err != FN_OK)
        ShowError("Could not queue that file. HotSync will skip what was copied.", err);
    else
        ShowMessage(entry->name, " is queued. Tap HotSync to install it.");
}
#endif

/* ConfirmInstallAlert's buttons. */
#ifdef FI_NO_HOTSYNC
enum { InstallNow, InstallCancel };
#else
enum { InstallNow, InstallQueue, InstallCancel };
#endif

/* Installs a file now or, on the cradle, queues it for the next HotSync. */
static void BrowseFile(const FdEntry *entry)
{
    UInt16 choice = FrmCustomAlert(ConfirmInstallAlert, entry->name, "", "");

    if (choice == InstallCancel)
        return;
    if (entry->size < PDB_HEADER_LEN) {
        ShowMessage(entry->name, " is too short to be a Palm app or database.");
        return;
    }
#ifndef FI_NO_HOTSYNC
    if (choice == InstallQueue) {
        BrowseQueue(entry);
        return;
    }
#endif
    InstallFile(entry);
}

static void BrowseOpen(FormType *form)
{
    ListType *list = FrmGetObjectPtr(form, FrmGetObjectIndex(form, BrowseList));
    Int16 item = LstGetSelection(list);
    const char *name;

    if (item == noListSelection)
        return;
    name = gEntries[item].name;
    if (IsFolder(name)) {
        if (StrLen(gPath) + StrLen(name) >= PATH_LEN)
            return;
        StrCat(gPath, name);
        BrowseLoad(form);
    } else if (IsInstallable(name)) {
        BrowseFile(&gEntries[item]);
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

/* ---- Edit form: one server ------------------------------------------- */

static FieldType *EditGetField(FormType *form)
{
    return FrmGetObjectPtr(form, FrmGetObjectIndex(form, EditField));
}

static void EditShow(FormType *form)
{
    FieldType *field = EditGetField(form);
    MemHandle text = MemHandleNew(HOST_LEN);

    if (text != NULL) {
        StrCopy(MemHandleLock(text), gPrefs.hosts[gEditServer]);
        MemHandleUnlock(text);
        FldSetTextHandle(field, text);
    }
    FrmDrawForm(form);
    FrmSetFocus(form, FrmGetObjectIndex(form, EditField));
}

static void EditSave(FormType *form)
{
    const char *text = FldGetTextPtr(EditGetField(form));
    char *host = gPrefs.hosts[gEditServer];

    MemSet(host, HOST_LEN, 0);
    if (text)
        StrNCopy(host, text, HOST_LEN - 1);
    gHostChoices[gEditServer] = host[0] ? host : EMPTY_SLOT;
    SavePrefs();
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

#ifndef FI_NO_HOTSYNC
/* ---- Queue form ------------------------------------------------------ */

static void QueueLoad(FormType *form)
{
    ListType *list = FrmGetObjectPtr(form, FrmGetObjectIndex(form, QueueList));
    FdEntry skip;
    UInt16 got, next;
    Boolean more;
    uint8_t err;

    SetBusy(form, "Reading...");
    gCount = 0;
    /* NIO makes the folder when HotSync is on; make it here in case not. */
    err = FdMakeFolder(QUEUE_URI);
    /* NIO answers a folder listed in the last two minutes from a copy it
     * kept, which misses what a HotSync moved out since. It keeps one such
     * copy, so listing the folder above first makes it read the queue. */
    if (err == FN_OK)
        err = FdList(QUEUE_PARENT_URI, 0, &skip, 1, &got, &next, &more);
    if (err == FN_OK)
        err = ReadDirectory(QUEUE_URI, true);
    SetBusy(form, gCount ? "Queued for HotSync" : "Nothing queued");
    LstSetListChoices(list, gChoices, gCount);
    LstSetSelection(list, noListSelection);
    LstDrawList(list);
    if (err != FN_OK)
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

#endif /* FI_NO_HOTSYNC */

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
#ifndef FI_NO_HOTSYNC
    case QueueForm:
        FrmSetEventHandler(form, QueueHandleEvent);
        break;
#endif
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

/* Palm OS 3.1, the oldest release the app is tested on. */
static Boolean RomCompatible(void)
{
    UInt32 rom = 0;

    FtrGet(sysFtrCreator, sysFtrNumROMVersion, &rom);
    return rom >= sysMakeROMVersion(3, 1, 0, sysROMStageRelease, 0);
}

static uint8_t AppStart(void)
{
    gEntries = MemPtrNew(MAX_ENTRIES * sizeof(FdEntry));
    gChoices = MemPtrNew(MAX_ENTRIES * sizeof(char *));
    if (gEntries == NULL || gChoices == NULL)
        return FN_ERR_INTERNAL;
    LoadPrefs();
    return fn_init();
}

static void AppStop(void)
{
    FrmCloseAllForms();
    fn_shutdown();
    if (gEntries)
        MemPtrFree(gEntries);
    if (gChoices)
        MemPtrFree(gChoices);
}

UInt32 PilotMain(UInt16 cmd, MemPtr cmdPBP, UInt16 launchFlags)
{
    uint8_t err;

    if (cmd != sysAppLaunchCmdNormalLaunch)
        return 0;
    if (!RomCompatible()) {
        FrmAlert(RomIncompatibleAlert);
        return 0;
    }
    err = AppStart();
    if (err != FN_OK) {
        ShowError(NO_LINK, err);
        AppStop();
        return 0;
    }
    FrmGotoForm(MainForm);
    EventLoop();
    AppStop();
    return 0;
}
