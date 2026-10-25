/* FN FujiPet for Palm OS.
 *
 * A virtual pet that grows up like the 1996 original (see pet.h), drawn in
 * the type's colours on a colour Palm (OS 3.5 and later, 256 colours) and
 * in black, white and a grey dither on the others: one drawing serves both
 * (tools/make_pet_art.py). The pet's life runs on while the app is closed:
 * on the next launch it catches up on the time that passed.
 *
 * Through FujiNet's N1: device the app keeps a TCP connection to a FujiPet
 * friend server (tools/fujipet-server), where the pet can register, see
 * which other pets are registered and which are online, and send the ones
 * online canned messages that arrive at once. The server's line protocol is
 * read by petproto.c.
 */
#include <PalmOS.h>

#include "fnnet.h"
#include "pet.h"
#include "petproto.h"
#include "petart.h"
#include "FujiPetRsc.h"

#define CREATOR APP_CREATOR
#define PREFS_ID 1
#define PREFS_VERSION 1

#ifndef DEFAULT_SERVER
#define DEFAULT_SERVER "localhost:7878"
#endif
#define SERVER_LEN 48
#define NET_UNIT 1

#define TICK_TICKS (SysTicksPerSecond() / 4)
#define FRAME_TICKS (SysTicksPerSecond() / 2)
#define POLL_TICKS (SysTicksPerSecond() / 2)
#define PING_TICKS (SysTicksPerSecond() * 20)
#define RETRY_TICKS (SysTicksPerSecond() * 60)
#define BUBBLE_TICKS (SysTicksPerSecond() * 8)
#define NOTICE_TICKS (SysTicksPerSecond() * 3)
#define CHEER_TICKS (SysTicksPerSecond() * 60)
/* Longest the pet catches up on at once: it is long dead by then. */
#define MAX_AWAY_SECONDS (30L * 24 * 3600)
#define MAX_CATCHUP (60L * PET_DAY)
#define NET_FAILURES 3
#define READS_PER_POLL 4

/* Pet minutes for each real minute. */
#define SPEED_NORMAL 1
#define SPEED_FAST 60
#define SPEED_TURBO 1440

#define LOG_LINES 3
#define LOG_LEN 40
#define NOTICE_LEN 40
#define LINE_LEN 80

/* Posted once the first form is up, to go online. */
#define connectEvent firstUserEvent

typedef struct {
    Pet pet;
    UInt32 lastSeconds;     /* when the pet last caught up */
    UInt16 speed;           /* SPEED_... */
    UInt16 petId;           /* on the friend server; 0 if not registered */
    char key[PP_KEY_LEN];
    char server[SERVER_LEN];
    UInt8 hasPet;
    UInt8 online;           /* meet friends: connect to the server */
    UInt8 sound;
    UInt8 spare;
} PetPrefs;

static PetPrefs gP;

/* ---- Pens: one colour each, or black, white or grey without colour ---- */

enum {
    PEN_BLACK, PEN_WHITE, PEN_SKY, PEN_GROUND, PEN_NIGHT, PEN_RED, PEN_BROWN, PEN_ICON,
    PEN_CREAM, PEN_GOLD, PEN_PINK, PEN_LIQUID, PEN_BAR,
    PEN_BODY,                       /* one per pet type */
    PEN_ACCENT = PEN_BODY + PET_TYPES,
    PEN_COUNT = PEN_ACCENT + PET_TYPES
};

static const UInt8 penRGB[PEN_COUNT][3] = {
    { 0, 0, 0 }, { 255, 255, 255 }, { 204, 232, 255 }, { 140, 210, 110 }, { 24, 28, 72 },
    { 230, 40, 64 }, { 140, 90, 40 }, { 40, 64, 150 }, { 255, 246, 220 }, { 250, 210, 40 },
    { 255, 200, 225 }, { 120, 200, 255 }, { 236, 236, 244 },
    { 255, 165, 70 }, { 205, 150, 95 }, { 250, 236, 244 }, { 110, 200, 90 },
    { 255, 140, 170 }, { 120, 75, 40 }, { 255, 130, 175 }, { 245, 215, 60 }
};

/* Without colour: 0 white, 1 black, 2 grey. */
#define MONO_WHITE 0
#define MONO_BLACK 1
#define MONO_GREY 2
static const UInt8 penMono[PEN_COUNT] = {
    1, 0, 0, 0, 1, 1, 2, 1, 0, 2, 0, 2, 0,
    0, 0, 0, 0,
    2, 2, 2, 2
};

static const CustomPatternType greyPattern = { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 };

static Boolean gColour;
static IndexedColorType gIndex[PEN_COUNT];
static WinHandle gOffscreen;

/* ---- The main screen ------------------------------------------------- */

#define BAR_H 20
#define AREA_Y 21
#define AREA_H 118
#define GROUND_Y 122
#define LOWER_BAR_Y 140
#define PET_SCALE 4
#define PET_SIZE (ART_SIZE * PET_SCALE)
#define PET_Y (GROUND_Y - PET_SIZE)
#define INFO_Y 22
#define STATUS_Y 126

enum {
    ICON_FEED, ICON_LIGHT, ICON_PLAY, ICON_MEDICINE,
    ICON_CLEAN, ICON_STATUS, ICON_SCOLD, ICON_FRIENDS
};

enum {
    ANIM_NONE, ANIM_MEAL, ANIM_SNACK, ANIM_REFUSE, ANIM_HAPPY, ANIM_CLEAN, ANIM_MEDICINE,
    ANIM_SCOLD, ANIM_HATCH, ANIM_EVOLVE
};
static const UInt8 animSteps[] = { 0, 4, 4, 4, 4, 4, 3, 3, 6, 6 };

static Boolean gCovered;        /* a menu or dialog is over the main form */
static UInt32 gNextFrame;
static UInt32 gLastCatchUp;
static UInt8 gFrame;
static Coord gPetX = 48;
static Int8 gDir = 1;
static UInt8 gAnim, gAnimStep;
static char gNotice[NOTICE_LEN];
static UInt32 gNoticeEnd;
static char gBubble[LOG_LEN];
static UInt32 gBubbleEnd;

/* The left-or-right game: five rounds, three to win. */
#define GAME_ROUNDS 5
static UInt8 gRound;            /* 0 when not playing */
static UInt8 gWins;
static UInt8 gShow;             /* frames left showing a round's answer */
static Int8 gLook;              /* where the pet looked */
static Boolean gRight;          /* the guess was right */

/* ---- The friend server ----------------------------------------------- */

enum { NET_OFF, NET_UP, NET_IN };   /* not connected, connected, logged in */
static UInt8 gNet;
static Boolean gPortOpen;
static Boolean gNoFujiNet;      /* FujiNet did not answer: no automatic retries */
static PpReader gReader;
static PpFriends gFriends;
static UInt32 gNextPoll, gNextPing, gNextRetry, gNextCheer;
static UInt8 gFailures;
static UInt8 gSentStage = 0xFF, gSentAge = 0xFF;
static char gNetText[32] = "Offline";
static char gLog[LOG_LINES][LOG_LEN];
static UInt8 gLogCount;
static UInt8 gSayCode;
static UInt8 gReadBuf[FN_NET_CHUNK];

static void DrawMain(void);
static void FriendsChanged(void);
static void LogChanged(void);

/* ---- Small helpers --------------------------------------------------- */

static Boolean Due(UInt32 when)
{
    return (Int32)(TimGetTicks() - when) >= 0;
}

static void *GetObject(FormType *form, UInt16 id)
{
    return FrmGetObjectPtr(form, FrmGetObjectIndex(form, id));
}

static void Beep(SndSysBeepType sound)
{
    if (gP.sound)
        SndPlaySystemSound(sound);
}

static void SavePrefs(void)
{
    PrefSetAppPreferences(CREATOR, PREFS_ID, PREFS_VERSION, &gP, sizeof(gP), true);
}

/* A notice on the main screen; %s in it is the pet's name. */
static void Notice(const char *text)
{
    StrPrintF(gNotice, text, gP.pet.name);
    gNoticeEnd = TimGetTicks() + NOTICE_TICKS;
}

static Boolean MainShowing(void)
{
    return FrmGetActiveFormID() == MainForm && !gCovered;
}

/* ---- Drawing ----------------------------------------------------------- */

static void Fill(UInt8 pen, Coord x, Coord y, Coord w, Coord h)
{
    RectangleType r;

    r.topLeft.x = x;
    r.topLeft.y = y;
    r.extent.x = w;
    r.extent.y = h;
    if (gColour) {
        WinSetForeColor(gIndex[pen]);
        WinDrawRectangle(&r, 0);
    } else if (penMono[pen] == MONO_BLACK) {
        WinDrawRectangle(&r, 0);
    } else if (penMono[pen] == MONO_WHITE) {
        WinEraseRectangle(&r, 0);
    } else {
        WinFillRectangle(&r, 0);
    }
}

static UInt8 Pixel(const UInt8 *art, UInt8 row, UInt8 col)
{
    return (art[row * 4 + (col >> 2)] >> (6 - 2 * (col & 3))) & 3;
}

/* Draws a sprite scaled up, a run of same-coloured pixels per rectangle.
 * ink is the outline's pen; flip mirrors it. */
static void DrawSprite(UInt16 art, Coord x, Coord y, UInt8 scale, UInt8 body, UInt8 accent,
                       UInt8 ink, Boolean flip)
{
    const UInt8 *bits = petArt[art];
    UInt8 row, col, start, value;

    for (row = 0; row < ART_SIZE; row++) {
        col = 0;
        while (col < ART_SIZE) {
            start = col;
            value = Pixel(bits, row, flip ? ART_SIZE - 1 - col : col);
            while (++col < ART_SIZE &&
                   Pixel(bits, row, flip ? ART_SIZE - 1 - col : col) == value)
                ;
            if (value != 0)
                Fill(value == 1 ? ink : value == 2 ? body : accent, x + start * scale,
                     y + row * scale, (col - start) * scale, scale);
        }
    }
}

/* Text on a pen's background: white on a dark one. */
static void Text(const char *text, Coord x, Coord y, UInt8 bg)
{
    Int16 len = StrLen(text);

    if (gColour) {
        WinSetTextColor(gIndex[penMono[bg] == MONO_BLACK ? PEN_WHITE : PEN_BLACK]);
        WinSetBackColor(gIndex[bg]);
        WinDrawChars(text, len, x, y);
    } else if (penMono[bg] == MONO_BLACK) {
        WinDrawInvertedChars(text, len, x, y);
    } else {
        WinDrawChars(text, len, x, y);
    }
}

static void TextCentred(const char *text, Coord y, UInt8 bg)
{
    Text(text, (160 - FntCharsWidth(text, StrLen(text))) / 2, y, bg);
}

static void BeginDraw(void)
{
    if (gColour)
        WinPushDrawState();
    else
        WinSetPattern(&greyPattern);
}

static void EndDraw(void)
{
    if (gColour)
        WinPopDrawState();
}

static UInt16 PetArt(const Pet *pet)
{
    UInt16 first = ART_TYPES + pet->type * ART_FORMS;

    switch (pet->stage) {
    case STAGE_EGG: return ART_EGG;
    case STAGE_BABY: return ART_BABY;
    case STAGE_CHILD: return first;
    case STAGE_TEEN: return first + 1 + (pet->form ? 1 : 0);
    case STAGE_ADULT: return first + 3 + (pet->form < 3 ? pet->form : 2);
    case STAGE_SENIOR: return first + 6;
    default: return ART_ANGEL;
    }
}

/* The pet's own sprite and colours, at any size. */
static void DrawPet(const Pet *pet, Coord x, Coord y, UInt8 scale, Boolean flip, Boolean dark)
{
    UInt8 body = PEN_BODY + pet->type, accent = PEN_ACCENT + pet->type;

    if (pet->stage == STAGE_EGG) {
        body = PEN_CREAM;
    } else if (pet->stage == STAGE_SENIOR) {
        accent = PEN_WHITE;
    } else if (pet->stage == STAGE_DEAD) {
        body = PEN_WHITE;
        accent = PEN_GOLD;
    }
    if (dark)
        body = accent = PEN_NIGHT;
    DrawSprite(PetArt(pet), x, y, scale, body, accent, dark ? PEN_WHITE : PEN_BLACK, flip);
}

static void IconCell(UInt8 icon, RectangleType *r)
{
    r->topLeft.x = (icon % 4) * 40;
    r->topLeft.y = icon < 4 ? 0 : LOWER_BAR_Y;
    r->extent.x = 40;
    r->extent.y = BAR_H;
}

static void DrawBars(void)
{
    RectangleType r;
    UInt8 i;

    Fill(PEN_BAR, 0, 0, 160, BAR_H);
    Fill(PEN_BAR, 0, LOWER_BAR_Y, 160, BAR_H);
    Fill(PEN_BLACK, 0, BAR_H, 160, 1);
    Fill(PEN_BLACK, 0, LOWER_BAR_Y - 1, 160, 1);
    for (i = 0; i < 8; i++) {
        IconCell(i, &r);
        DrawSprite(ART_ICON_FEED + i, r.topLeft.x + 12, r.topLeft.y + 2, 1, PEN_WHITE,
                   PEN_WHITE, PEN_ICON, false);
    }
}

static void PoopSpot(UInt8 i, Coord *x, Coord *y)
{
    *x = 128 - (i / 2) * 32;
    *y = GROUND_Y - 32 - (i % 2) * 30;
}

static Coord PetRoom(void)
{
    UInt8 n = gP.pet.poops;

    return 160 - PET_SIZE - (n == 0 ? 0 : n <= 2 ? 32 : 64);
}

static void DrawStatusLine(UInt8 bg)
{
    char text[NOTICE_LEN + 8];
    UInt8 online;

    FntSetFont(stdFont);
    if (gNotice[0] && !Due(gNoticeEnd)) {
        StrCopy(text, gNotice);
    } else if (gNet == NET_IN) {
        online = pp_friends_online(&gFriends);
        if (online == 1)
            StrCopy(text, "1 friend online");
        else
            StrPrintF(text, "%u friends online", online);
    } else if (gNet == NET_UP) {
        StrPrintF(text, gFriends.count == 1 ? "%u pet on the server" : "%u pets on the server",
                  gFriends.count);
    } else if (gP.online) {
        StrCopy(text, gNetText);
    } else {
        text[0] = '\0';
    }
    if (text[0])
        TextCentred(text, STATUS_Y, bg);
}

static void DrawBubble(void)
{
    Int16 w = FntCharsWidth(gBubble, StrLen(gBubble)) + 8;
    Coord x = (160 - w) / 2;

    Fill(PEN_BLACK, x - 1, 36, w + 2, 15);
    Fill(PEN_WHITE, x, 37, w, 13);
    Text(gBubble, x + 4, 38, PEN_WHITE);
}

static void DrawGame(UInt8 bg)
{
    char text[32];
    Pet *pet = &gP.pet;
    Coord x;

    FntSetFont(boldFont);
    StrPrintF(text, "Round %u of %u: wins %u", gRound, GAME_ROUNDS, gWins);
    TextCentred(text, 36, bg);
    FntSetFont(stdFont);
    x = 48;
    if (gShow) {
        TextCentred(gRight ? "Yes! Right guess!" : "Nope, wrong way.", 48, bg);
        /* Most pets look the same mirrored: lean that way, with an arrow. */
        x += gLook * 24;
        FntSetFont(boldFont);
        Text(gLook < 0 ? "<<" : ">>", gLook < 0 ? 4 : 142, PET_Y + 26, bg);
        FntSetFont(stdFont);
    } else {
        TextCentred("Which way will it look?", 48, bg);
    }
    DrawPet(pet, x, PET_Y, PET_SCALE, gShow ? gLook < 0 : false, false);
    FntSetFont(boldFont);
    Text("< Left", 6, STATUS_Y, PEN_GROUND);
    Text("Right >", 160 - 6 - FntCharsWidth("Right >", 7), STATUS_Y, PEN_GROUND);
    FntSetFont(stdFont);
}

static void DrawDead(UInt8 bg)
{
    char text[48];
    Pet *pet = &gP.pet;

    DrawPet(pet, 48, PET_Y - 10 + (gFrame & 1) * 4, PET_SCALE, false, false);
    FntSetFont(boldFont);
    StrPrintF(text, "%s %s", pet->name, pet_death_text(pet));
    TextCentred(text, 36, bg);
    FntSetFont(stdFont);
    TextCentred("Tap here for a new egg.", STATUS_Y, PEN_GROUND);
}

static void DrawScene(Boolean dark, UInt8 bg)
{
    Pet *pet = &gP.pet;
    Coord x = gPetX, y = PET_Y, fx;
    Boolean flip = gDir < 0;
    UInt8 i;

    if (!dark)
        for (i = 0; i < pet->poops; i++) {
            Coord px, py;
            PoopSpot(i, &px, &py);
            DrawSprite(ART_POOP, px, py, 2, PEN_BROWN, PEN_BROWN, PEN_BLACK, false);
        }

    switch (gAnim) {
    case ANIM_MEAL:
    case ANIM_SNACK:
        x = 24;
        flip = false;
        fx = 100;
        if (gAnim == ANIM_MEAL)
            DrawSprite(ART_MEAL, fx, GROUND_Y - 32, 2, PEN_WHITE, PEN_BLACK, PEN_BLACK, false);
        else
            DrawSprite(ART_SNACK, fx, GROUND_Y - 32, 2, PEN_PINK, PEN_RED, PEN_BLACK, false);
        if (gAnimStep > 0)
            Fill(bg, fx + 32 - gAnimStep * 8, GROUND_Y - 32, gAnimStep * 8, 32);
        y -= (gAnimStep & 1) * PET_SCALE;
        break;
    case ANIM_REFUSE:
    case ANIM_SCOLD:
        flip = (gAnimStep & 1) != 0;
        break;
    case ANIM_HAPPY:
        y -= (gAnimStep & 1) ? 8 : 0;
        DrawSprite(ART_HEART, x + 24, y - 18, 1, PEN_RED, PEN_RED, PEN_BLACK, false);
        break;
    case ANIM_EVOLVE:
        if (gAnimStep & 1) {
            TextCentred("!", PET_Y + 24, bg);
            x = -100; /* not drawn this frame */
        }
        break;
    }

    if (x > -100) {
        if (gAnim == ANIM_HATCH && gAnimStep < animSteps[ANIM_HATCH] - 1) {
            Pet egg = *pet;
            egg.stage = STAGE_EGG;
            DrawPet(&egg, 48 + ((gAnimStep & 1) ? 4 : -4), PET_Y, PET_SCALE, false, false);
        } else if (pet->stage == STAGE_EGG) {
            DrawPet(pet, 48 + ((gFrame & 2) ? 4 : 0), PET_Y, PET_SCALE, false, false);
        } else {
            if (!pet->asleep && gAnim == ANIM_NONE)
                y -= (gFrame & 1) * PET_SCALE;
            DrawPet(pet, x, y, PET_SCALE, flip, dark);
        }
    }

    if (gAnim == ANIM_CLEAN)
        DrawSprite(ART_ICON_CLEAN, gAnimStep * 40 - 16, GROUND_Y - 32, 2, PEN_WHITE, PEN_WHITE,
                   PEN_BROWN, false);
    if (gAnim == ANIM_MEDICINE)
        DrawSprite(ART_SYRINGE, gPetX + PET_SIZE - gAnimStep * 6, PET_Y + 20, 2, PEN_WHITE,
                   PEN_LIQUID, PEN_BLACK, true);
    if (gAnim == ANIM_SCOLD)
        DrawSprite(ART_ATTENTION, gPetX + PET_SIZE - 8, PET_Y - 4, 2, PEN_WHITE, PEN_WHITE,
                   PEN_RED, false);

    if (pet->sick && !dark)
        DrawSprite(ART_SKULL, 2, 36, 2, PEN_WHITE, PEN_WHITE, PEN_BLACK, false);
    if (pet->asleep) {
        FntSetFont(boldFont);
        Text((gFrame & 2) ? "z Z z" : "Z z Z", gPetX + 40, PET_Y - 6, bg);
        FntSetFont(stdFont);
    }
    if (pet->call != CALL_NONE && (gFrame & 1) == 0)
        DrawSprite(ART_ATTENTION, 142, 36, 1, PEN_WHITE, PEN_WHITE, PEN_RED, false);
}

static void DrawArea(void)
{
    Pet *pet = &gP.pet;
    Boolean dark = pet->lightsOff && pet->stage != STAGE_DEAD && gRound == 0;
    UInt8 bg = dark ? PEN_NIGHT : PEN_SKY;
    char clock[timeStringLength + 1];
    fn_u16 minute = pet_minute_of_day(pet);

    Fill(bg, 0, AREA_Y, 160, AREA_H);
    if (!dark) {
        Fill(PEN_GROUND, 0, GROUND_Y, 160, LOWER_BAR_Y - 1 - GROUND_Y);
        if (!gColour)
            Fill(PEN_BLACK, 0, GROUND_Y, 160, 1);
    }

    FntSetFont(boldFont);
    Text(pet->name, 2, INFO_Y, bg);
    FntSetFont(stdFont);
    TimeToAscii((UInt8)(minute / 60), (UInt8)(minute % 60),
                (TimeFormatType)PrefGetPreference(prefTimeFormat), clock);
    Text(clock, 158 - FntCharsWidth(clock, StrLen(clock)), INFO_Y, bg);

    if (pet->stage == STAGE_DEAD)
        DrawDead(bg);
    else if (gRound)
        DrawGame(bg);
    else
        DrawScene(dark, bg);

    if (gRound == 0 && pet->stage != STAGE_DEAD)
        DrawStatusLine(dark ? PEN_NIGHT : PEN_GROUND);
    if (gBubble[0] && !Due(gBubbleEnd))
        DrawBubble();
}

static void DrawMain(void)
{
    RectangleType all;
    WinHandle old = NULL;

    if (!MainShowing())
        return;
    all.topLeft.x = all.topLeft.y = 0;
    all.extent.x = all.extent.y = 160;
    if (gOffscreen)
        old = WinSetDrawWindow(gOffscreen);
    BeginDraw();
    DrawBars();
    DrawArea();
    EndDraw();
    if (gOffscreen) {
        WinSetDrawWindow(old);
        WinCopyRectangle(gOffscreen, NULL, &all, 0, 0, winPaint);
    }
}

/* ---- Time ------------------------------------------------------------- */

static void StartAnim(UInt8 anim)
{
    gAnim = anim;
    gAnimStep = 0;
}

static void SendUpdate(void);

/* What happened while time passed: quiet when it was while the app was
 * closed, so nothing beeps at launch. */
static void React(UInt16 events, Boolean quiet)
{
    Pet *pet = &gP.pet;
    char text[NOTICE_LEN];

    if (events & PET_EV_DIED) {
        gRound = 0;
        Notice("%s has gone to pet heaven.");
        if (!quiet)
            Beep(sndWarning);
        SavePrefs();
        return;
    }
    if (events & PET_EV_HATCH) {
        if (!quiet)
            StartAnim(ANIM_HATCH);
        Notice("%s hatched! Feed it!");
    } else if (events & PET_EV_EVOLVE) {
        if (!quiet)
            StartAnim(ANIM_EVOLVE);
        StrPrintF(text, "%%s grew into %s!", pet_form_name(pet));
        Notice(text);
    } else if (events & PET_EV_SICK) {
        Notice("%s is sick!");
    } else if (events & PET_EV_SLEEP) {
        Notice(pet->lightsOff ? "%s fell asleep." : "%s is sleepy. Lights off!");
    } else if (events & PET_EV_WAKE) {
        Notice("Good morning, %s!");
    }
    if (events & (PET_EV_HATCH | PET_EV_EVOLVE))
        SavePrefs();
    if (!quiet && (events & (PET_EV_HATCH | PET_EV_EVOLVE | PET_EV_CALL | PET_EV_SICK)))
        Beep(events & PET_EV_CALL ? sndInfo : sndConfirmation);
}

/* Lets the pet live the time since it last did. */
static void CatchUp(Boolean quiet)
{
    UInt32 now = TimGetSeconds(), elapsed, minutes;

    gLastCatchUp = TimGetTicks();
    if (!gP.hasPet || now <= gP.lastSeconds) {
        gP.lastSeconds = now;
        return;
    }
    elapsed = now - gP.lastSeconds;
    if (elapsed > MAX_AWAY_SECONDS) {
        gP.lastSeconds = now - MAX_AWAY_SECONDS;
        elapsed = MAX_AWAY_SECONDS;
    }
    minutes = elapsed * gP.speed / 60;
    if (minutes == 0)
        return;
    gP.lastSeconds += minutes * 60 / gP.speed;
    if (minutes > MAX_CATCHUP)
        minutes = MAX_CATCHUP;
    React(pet_advance(&gP.pet, minutes), quiet);
}

/* ---- The friend server ----------------------------------------------- */

static Err NetSend(const char *line)
{
    char buf[LINE_LEN + 1];
    UInt16 len = StrLen(line);

    if (gNet == NET_OFF || len >= LINE_LEN)
        return fnErrTooBig;
    StrCopy(buf, line);
    buf[len] = '\n';
    return FnNetWrite(NET_UNIT, buf, len + 1);
}

static void SendPet(const char *verb)
{
    char line[LINE_LEN];
    Pet *pet = &gP.pet;

    gSentStage = pet->stage;
    gSentAge = (UInt8)(pet_age_days(pet) > 99 ? 99 : pet_age_days(pet));
    StrPrintF(line, "%s|%s|%u|%u|%u", verb, pet->name, pet->type, gSentStage, gSentAge);
    NetSend(line);
}

static void SendUpdate(void)
{
    if (gNet == NET_IN)
        SendPet("UPD");
}

static void NetClose(void)
{
    UInt8 i;

    if (gNet != NET_OFF)
        FnNetClose(NET_UNIT);
    gNet = NET_OFF;
    for (i = 0; i < gFriends.count; i++)
        gFriends.list[i].online = 0;
    FriendsChanged();
}

static void NetDrop(const char *why)
{
    NetClose();
    StrCopy(gNetText, why);
    gNextRetry = TimGetTicks() + RETRY_TICKS / 2;
}

static void NetConnect(void)
{
    char url[SERVER_LEN + 16], line[LINE_LEN];
    FnNetStatus status;
    Err err;

    NetClose();
    if (!gP.online || !gPortOpen)
        return;
    StrCopy(gNetText, "Connecting...");
    DrawMain();
    FriendsChanged();
    StrPrintF(url, "N1:TCP://%s/", gP.server);
    err = FnNetOpen(NET_UNIT, url, FN_NET_READ_WRITE, FN_NET_TRANS_NONE);
    if (err == errNone)
        err = FnNetGetStatus(NET_UNIT, &status);
    gNoFujiNet = err == fnErrNoReply;
    if (err != errNone || !status.connected) {
        if (err != fnErrNoReply)
            FnNetClose(NET_UNIT);
        StrCopy(gNetText, gNoFujiNet ? "No FujiNet in the cradle" : "No friend server");
        gNextRetry = TimGetTicks() + RETRY_TICKS;
        FriendsChanged();
        return;
    }
    gNet = NET_UP;
    gFailures = 0;
    gFriends.count = 0;
    pp_reader_reset(&gReader);
    StrCopy(gNetText, "Online");
    gNextPoll = TimGetTicks();
    gNextPing = TimGetTicks() + PING_TICKS;
    if (gP.petId) {
        StrPrintF(line, "HELLO|%u|%s|%u|%u", gP.petId, gP.key, gP.pet.stage,
                  (UInt16)(pet_age_days(&gP.pet) > 99 ? 99 : pet_age_days(&gP.pet)));
        NetSend(line);
    } else {
        NetSend("LIST");
    }
    FriendsChanged();
}

static void Register(void)
{
    if (gNet == NET_OFF)
        NetConnect();
    if (gNet == NET_UP && gP.petId == 0)
        SendPet("REG");
}

static void AddLog(const char *who, const char *text)
{
    UInt8 i;

    if (gLogCount == LOG_LINES) {
        for (i = 1; i < LOG_LINES; i++)
            StrCopy(gLog[i - 1], gLog[i]);
        gLogCount--;
    }
    StrNCopy(gLog[gLogCount], who, LOG_LEN - 1);
    gLog[gLogCount][LOG_LEN - 1] = '\0';
    StrNCat(gLog[gLogCount], text, LOG_LEN);
    gLogCount++;
    LogChanged();
}

static const char *FriendName(UInt16 id)
{
    PpFriend *f = pp_friend_find(&gFriends, id);

    return f ? f->name : "?";
}

static void Heard(PpEvent *e)
{
    char who[PET_NAME_LEN + 4];

    StrPrintF(who, "%s: ", e->name);
    AddLog(who, pp_message(e->code));
    StrPrintF(gBubble, "%s: %s", e->name, pp_message(e->code));
    gBubbleEnd = TimGetTicks() + BUBBLE_TICKS;
    Beep(sndConfirmation);
    if (pp_message_cheers(e->code) && Due(gNextCheer) && pet_cheer(&gP.pet) == PET_OK) {
        gNextCheer = TimGetTicks() + CHEER_TICKS;
        StartAnim(ANIM_HAPPY);
    }
}

static void NetLine(char *line)
{
    PpEvent e;
    PpFriend *f;
    char who[PET_NAME_LEN + 8];

    pp_parse(line, &e);
    switch (e.kind) {
    case PP_REGOK:
        gP.petId = e.id;
        StrCopy(gP.key, e.key);
        gNet = NET_IN;
        SavePrefs();
        Notice("%s joined the friend server!");
        break;
    case PP_HI:
        gNet = NET_IN;
        SendPet("UPD");
        NetSend("LIST");
        break;
    case PP_PET:
    case PP_ON:
        if (e.id == gP.petId && gP.petId != 0)
            break;
        f = pp_friend_update(&gFriends, e.id);
        if (f == NULL)
            break;
        if (e.kind == PP_ON && !f->online && gNet == NET_IN && f->name[0]) {
            StrPrintF(gBubble, "%s is online!", e.name);
            gBubbleEnd = TimGetTicks() + BUBBLE_TICKS;
        }
        StrCopy(f->name, e.name);
        f->type = e.type;
        f->stage = e.stage;
        f->age = e.age;
        f->online = e.online;
        if (e.kind == PP_ON)
            pp_friends_sort(&gFriends);
        break;
    case PP_END:
        pp_friends_sort(&gFriends);
        break;
    case PP_OFF:
        if ((f = pp_friend_find(&gFriends, e.id)) != NULL) {
            f->online = 0;
            pp_friends_sort(&gFriends);
        }
        break;
    case PP_MSG:
        Heard(&e);
        break;
    case PP_SENT:
        StrPrintF(who, "You to %s: ", FriendName(e.id));
        AddLog(who, pp_message(e.code));
        break;
    case PP_ERR:
        if (StrCompare(e.key, "unknown") == 0 || StrCompare(e.key, "key") == 0) {
            /* The server forgot this pet: it can register again. */
            gP.petId = 0;
            gNet = NET_UP;
            SavePrefs();
            NetSend("LIST");
            Notice("Register %s again.");
        } else if (StrCompare(e.key, "offline") == 0) {
            StrPrintF(who, "%s left: ", FriendName(e.id));
            AddLog(who, "not sent");
        }
        break;
    default:
        return;
    }
    FriendsChanged();
}

static void NetPoll(void)
{
    FnNetStatus status;
    UInt16 got, i, want, reads;
    Err err;

    for (reads = 0; reads < READS_PER_POLL; reads++) {
        err = FnNetGetStatus(NET_UNIT, &status);
        if (err != errNone) {
            if (++gFailures >= NET_FAILURES)
                NetDrop("Lost FujiNet");
            return;
        }
        gFailures = 0;
        if (status.avail == 0) {
            if (!status.connected)
                NetDrop("The server hung up");
            return;
        }
        want = status.avail < sizeof(gReadBuf) ? status.avail : sizeof(gReadBuf);
        if (FnNetRead(NET_UNIT, gReadBuf, want, &got) != errNone) {
            /* Bytes lost: the line they were in is spoilt. */
            gReader.overflow = 1;
            return;
        }
        for (i = 0; i < got; i++)
            if (pp_reader_push(&gReader, gReadBuf[i]))
                NetLine(gReader.line);
    }
}

static void NetIdle(void)
{
    UInt16 age;

    if (gNet == NET_OFF) {
        if (gP.online && !gNoFujiNet && gNextRetry && Due(gNextRetry))
            NetConnect();
        return;
    }
    EvtResetAutoOffTimer();
    if (Due(gNextPoll)) {
        gNextPoll = TimGetTicks() + POLL_TICKS;
        NetPoll();
    }
    if (gNet == NET_OFF)
        return;
    if (Due(gNextPing)) {
        gNextPing = TimGetTicks() + PING_TICKS;
        NetSend("PING");
    }
    age = pet_age_days(&gP.pet);
    if (gNet == NET_IN && (gP.pet.stage != gSentStage || (age > 99 ? 99 : age) != gSentAge))
        SendUpdate();
}

/* ---- Care ------------------------------------------------------------- */

static Boolean CanCare(void)
{
    Pet *pet = &gP.pet;

    if (pet->stage == STAGE_EGG)
        Notice("Wait for the egg to hatch.");
    else if (pet->stage == STAGE_DEAD)
        Notice("Tap for a new egg.");
    else if (pet->asleep)
        Notice("Shh! %s is asleep.");
    else
        return true;
    return false;
}

static void Outcome(int result, UInt8 anim, const char *no)
{
    switch (result) {
    case PET_OK:
    case PET_CURED:
        StartAnim(anim);
        break;
    case PET_REFUSED:
        StartAnim(ANIM_REFUSE);
        Notice(no);
        break;
    case PET_NO:
        Notice(no);
        break;
    }
}

static void Feed(void)
{
    FormType *form = FrmGetActiveForm();
    ListType *list = GetObject(form, MainFeedList);
    Int16 choice;

    if (!CanCare())
        return;
    LstSetSelection(list, 0);
    choice = LstPopupList(list);
    if (choice == 0)
        Outcome(pet_meal(&gP.pet), ANIM_MEAL,
                gP.pet.sick ? "%s is too sick to eat." : "%s is full!");
    else if (choice == 1)
        Outcome(pet_snack(&gP.pet), ANIM_SNACK, "%s is too sick to eat.");
}

static void StartGame(void)
{
    if (!CanCare())
        return;
    if (gP.pet.sick) {
        Notice("%s is too sick to play.");
        return;
    }
    gRound = 1;
    gWins = 0;
    gShow = 0;
    gAnim = ANIM_NONE;
}

static void Guess(Boolean right)
{
    if (gShow)
        return;
    gLook = pet_random(&gP.pet, 2) ? 1 : -1;
    gRight = right == (gLook > 0);
    if (gRight)
        gWins++;
    gShow = 3;
}

static void GameStep(void)
{
    char text[NOTICE_LEN];
    Boolean won;

    if (gShow == 0 || --gShow > 0)
        return;
    if (gRound < GAME_ROUNDS) {
        gRound++;
        return;
    }
    gRound = 0;
    won = gWins >= 3;
    pet_play(&gP.pet, won);
    StrPrintF(text, won ? "%%s won %u of 5! Happy!" : "Only %u of 5. Try again!", gWins);
    Notice(text);
    StartAnim(won ? ANIM_HAPPY : ANIM_REFUSE);
}

static void DoIcon(UInt8 icon)
{
    Pet *pet = &gP.pet;

    switch (icon) {
    case ICON_FEED:
        Feed();
        break;
    case ICON_LIGHT:
        if (pet->stage == STAGE_DEAD)
            break;
        Notice(pet_lights(pet) ? "Lights off." : "Lights on.");
        break;
    case ICON_PLAY:
        StartGame();
        break;
    case ICON_MEDICINE:
        if (CanCare())
            Outcome(pet_medicine(pet), ANIM_MEDICINE, "%s isn't sick.");
        if (!pet->sick && gAnim == ANIM_MEDICINE)
            Notice("%s is all better!");
        break;
    case ICON_CLEAN:
        if (pet->stage != STAGE_EGG && pet->stage != STAGE_DEAD)
            Outcome(pet_clean(pet), ANIM_CLEAN, "Nothing to clean.");
        break;
    case ICON_STATUS:
        FrmPopupForm(StatusForm);
        break;
    case ICON_SCOLD:
        if (!CanCare())
            break;
        if (pet_scold(pet) == PET_OK) {
            StartAnim(ANIM_SCOLD);
            Notice("%s learned a lesson.");
        } else {
            StartAnim(ANIM_REFUSE);
            Notice("%s did nothing wrong!");
        }
        break;
    case ICON_FRIENDS:
        FrmGotoForm(FriendsForm);
        break;
    }
}

/* ---- Main form --------------------------------------------------------- */

static Boolean InRect(const RectangleType *r, Coord x, Coord y)
{
    return x >= r->topLeft.x && x < r->topLeft.x + r->extent.x &&
           y >= r->topLeft.y && y < r->topLeft.y + r->extent.y;
}

/* Tracks the pen in a button; true if let go inside. */
static Boolean TrackPen(const RectangleType *r)
{
    Coord x, y;
    Boolean down = true, inside = true, shown = true;

    WinInvertRectangle(r, 0);
    while (down) {
        EvtGetPen(&x, &y, &down);
        inside = InRect(r, x, y);
        if (inside != shown) {
            WinInvertRectangle(r, 0);
            shown = inside;
        }
    }
    if (shown)
        WinInvertRectangle(r, 0);
    return inside;
}

static Boolean MainPen(Coord x, Coord y)
{
    RectangleType r;
    UInt8 icon;

    if (y < BAR_H || y >= LOWER_BAR_Y) {
        icon = (UInt8)((x / 40) + (y >= LOWER_BAR_Y ? 4 : 0));
        IconCell(icon, &r);
        if (TrackPen(&r) && (gRound == 0 || icon == ICON_STATUS || icon == ICON_FRIENDS)) {
            gRound = 0;
            DoIcon(icon);
        }
        DrawMain();
        return true;
    }
    if (gP.pet.stage == STAGE_DEAD) {
        FrmGotoForm(NewPetForm);
        return true;
    }
    if (gRound) {
        Guess(x >= 80);
    } else if (gBubble[0] && !Due(gBubbleEnd)) {
        gBubbleEnd = TimGetTicks();
    } else if (x >= gPetX && x < gPetX + PET_SIZE && pet_is_awake(&gP.pet)) {
        StartAnim(ANIM_HAPPY);
    }
    DrawMain();
    return true;
}

static void Animate(void)
{
    Pet *pet = &gP.pet;
    Coord room = PetRoom();

    gFrame++;
    if (gRound) {
        GameStep();
        return;
    }
    if (gAnim != ANIM_NONE) {
        if (++gAnimStep >= animSteps[gAnim])
            gAnim = ANIM_NONE;
        return;
    }
    if (gPetX > room)
        gPetX = room;
    if (!pet_is_awake(pet) || (gFrame & 1))
        return;
    if (pet_random(pet, 4) == 0)
        gDir = -gDir;
    gPetX += gDir * PET_SCALE;
    if (gPetX <= 0 || gPetX >= room) {
        gPetX = gPetX <= 0 ? 0 : room;
        gDir = -gDir;
    }
}

static void Idle(void)
{
    if (gP.hasPet && Due(gLastCatchUp + SysTicksPerSecond()))
        CatchUp(false);
    NetIdle();
    if (FrmGetActiveFormID() == MainForm && Due(gNextFrame)) {
        gNextFrame = TimGetTicks() + FRAME_TICKS;
        Animate();
        DrawMain();
    }
}

static void MainMenuChoice(UInt16 id)
{
    switch (id) {
    case MainMenuStatus:
        FrmPopupForm(StatusForm);
        break;
    case MainMenuFriends:
        FrmGotoForm(FriendsForm);
        break;
    case MainMenuNewEgg:
        if (gP.pet.stage == STAGE_DEAD ||
            FrmCustomAlert(NewEggAlert, gP.pet.name, "", "") == 0)
            FrmGotoForm(NewPetForm);
        break;
    case MainMenuSettings:
        FrmPopupForm(SettingsForm);
        break;
    case MainMenuReconnect:
        gNoFujiNet = false;
        NetConnect();
        break;
    case MainMenuAbout:
        FrmAlert(AboutAlert);
        break;
    }
}

static Boolean MainHandleEvent(EventType *event)
{
    EventType connect;

    switch (event->eType) {
    case frmOpenEvent:
        FrmDrawForm(FrmGetActiveForm());
        gCovered = false;
        DrawMain();
        if (gNet == NET_OFF && gNextRetry == 0) {
            /* Online once the pet is on screen. */
            MemSet(&connect, sizeof(connect), 0);
            connect.eType = connectEvent;
            EvtAddEventToQueue(&connect);
        }
        return true;
    case frmUpdateEvent:
        DrawMain();
        return true;
    case connectEvent:
        gNextRetry = 1;
        NetConnect();
        return true;
    case penDownEvent:
        return MainPen(event->screenX, event->screenY);
    case keyDownEvent:
        if (gRound && (event->data.keyDown.chr == vchrPageUp ||
                       event->data.keyDown.chr == vchrPageDown)) {
            Guess(event->data.keyDown.chr == vchrPageDown);
            DrawMain();
            return true;
        }
        break;
    case menuEvent:
        MainMenuChoice(event->data.menu.itemID);
        return true;
    default:
        break;
    }
    return false;
}

/* ---- New pet form ---------------------------------------------------- */

#define PREVIEW_X 80
#define PREVIEW_Y 46
#define PREVIEW_SIZE 76

static void DrawPreview(FormType *form)
{
    Pet pet;
    Int16 type = LstGetSelection(GetObject(form, NewTypeList));

    MemSet(&pet, sizeof(pet), 0);
    pet.type = (fn_u8)(type < 0 ? 0 : type);
    pet.stage = STAGE_ADULT;
    BeginDraw();
    Fill(PEN_SKY, PREVIEW_X, PREVIEW_Y, PREVIEW_SIZE, PREVIEW_SIZE);
    Fill(PEN_GROUND, PREVIEW_X, PREVIEW_Y + PREVIEW_SIZE - 8, PREVIEW_SIZE, 8);
    DrawPet(&pet, PREVIEW_X + 6, PREVIEW_Y + 4, PET_SCALE, false, false);
    pet.stage = STAGE_EGG;
    DrawPet(&pet, PREVIEW_X + PREVIEW_SIZE - 18, PREVIEW_Y + PREVIEW_SIZE - 22, 1, false,
            false);
    EndDraw();
    FntSetFont(stdFont);
    WinDrawChars("Hatches in 5 pet minutes;", 25, 4, 126);
    WinDrawChars("grows up over days.", 19, 4, 136);
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

static void Hatch(FormType *form)
{
    char name[32];
    Int16 type = LstGetSelection(GetObject(form, NewTypeList));

    GetFieldText(form, NewNameField, name, sizeof(name));
    pp_clean_name(name);
    if (name[0] == '\0') {
        FrmCustomAlert(MessageAlert, "Give your pet a name: ", "letters and digits, up to 10.",
                       "");
        return;
    }
    pet_new(&gP.pet, name, (fn_u8)(type < 0 ? 0 : type), TimGetSeconds() / 60,
            TimGetTicks() ^ TimGetSeconds());
    gP.hasPet = 1;
    gP.lastSeconds = TimGetSeconds();
    gRound = 0;
    gAnim = ANIM_NONE;
    gPetX = 48;
    SavePrefs();
    SendUpdate();
    Notice("Keep %s's egg warm...");
    FrmGotoForm(MainForm);
}

static Boolean NewPetHandleEvent(EventType *event)
{
    FormType *form = FrmGetActiveForm();

    switch (event->eType) {
    case frmOpenEvent:
        LstSetSelection(GetObject(form, NewTypeList),
                        gP.hasPet && gP.pet.type < PET_TYPES ? gP.pet.type : 0);
        if (!gP.hasPet)
            FrmHideObject(form, FrmGetObjectIndex(form, NewCancelButton));
        FrmDrawForm(form);
        DrawPreview(form);
        FrmSetFocus(form, FrmGetObjectIndex(form, NewNameField));
        return true;
    case lstSelectEvent:
        DrawPreview(form);
        return true;
    case ctlSelectEvent:
        if (event->data.ctlSelect.controlID == NewHatchButton)
            Hatch(form);
        else if (event->data.ctlSelect.controlID == NewCancelButton)
            FrmGotoForm(MainForm);
        return true;
    default:
        break;
    }
    return false;
}

/* ---- Friends form ----------------------------------------------------- */

#define FRIENDS_INFO_Y 16
#define LOG_Y 103

static void DrawFriendRow(Int16 item, RectangleType *bounds, Char **unused)
{
    const PpFriend *f;
    char text[32];
    Coord x = bounds->topLeft.x + 2, y = bounds->topLeft.y;
    RectangleType dot;

    if (item < 0 || item >= gFriends.count)
        return;
    f = &gFriends.list[item];
    dot.topLeft.x = x;
    dot.topLeft.y = y + 3;
    dot.extent.x = dot.extent.y = 6;
    if (f->online)
        WinDrawRectangle(&dot, 3);
    else
        WinDrawRectangleFrame(roundFrame, &dot);
    FntSetFont(f->online ? boldFont : stdFont);
    WinDrawChars(f->name, StrLen(f->name), x + 10, y);
    FntSetFont(stdFont);
    StrPrintF(text, "%s %s", pet_type_name(f->type), pet_stage_name(f->stage));
    WinDrawChars(text, StrLen(text), 80, y);
    StrPrintF(text, "%ud", f->age);
    WinDrawChars(text, StrLen(text), bounds->topLeft.x + bounds->extent.x - 2 -
                 FntCharsWidth(text, StrLen(text)), y);
}

static void DrawFriendsInfo(void)
{
    char text[48];
    RectangleType r;

    r.topLeft.x = 0;
    r.topLeft.y = FRIENDS_INFO_Y;
    r.extent.x = 160;
    r.extent.y = 11;
    WinEraseRectangle(&r, 0);
    if (gNet == NET_IN)
        StrPrintF(text, "%s is #%u. %u of %u online.", gP.pet.name, gP.petId,
                  pp_friends_online(&gFriends), gFriends.count);
    else if (gNet == NET_UP && gP.petId == 0)
        StrPrintF(text, gFriends.count == 1 ? "%u pet here. Tap Register!" :
                  "%u pets here. Tap Register!", gFriends.count);
    else if (gNet == NET_UP)
        StrCopy(text, "Saying hello...");
    else
        StrCopy(text, gNetText);
    FntSetFont(stdFont);
    WinDrawChars(text, StrLen(text), 2, FRIENDS_INFO_Y);
}

static void DrawLog(void)
{
    RectangleType r;
    UInt8 i;

    r.topLeft.x = 0;
    r.topLeft.y = LOG_Y;
    r.extent.x = 160;
    r.extent.y = LOG_LINES * 11 + 2;
    WinEraseRectangle(&r, 0);
    WinDrawGrayLine(0, LOG_Y - 2, 159, LOG_Y - 2);
    FntSetFont(stdFont);
    if (gLogCount == 0)
        WinDrawChars("Messages from friends show here.", 32, 2, LOG_Y);
    for (i = 0; i < gLogCount; i++)
        WinDrawChars(gLog[i], StrLen(gLog[i]), 2, LOG_Y + i * 11);
}

static void ShowFriends(FormType *form)
{
    ListType *list = GetObject(form, FriendsList);
    Int16 sel = LstGetSelection(list);
    UInt16 keep = (sel >= 0 && sel < gFriends.count) ? gFriends.list[sel].id : 0;
    Int16 i;

    LstSetListChoices(list, NULL, gFriends.count);
    LstSetDrawFunction(list, DrawFriendRow);
    /* Keep the friend picked; else pick the first one online (they sort first). */
    sel = gFriends.count > 0 && gFriends.list[0].online ? 0 : noListSelection;
    for (i = 0; i < gFriends.count; i++)
        if (gFriends.list[i].id == keep)
            sel = i;
    LstSetSelection(list, sel);
    if (gNet == NET_IN || gP.petId != 0)
        FrmHideObject(form, FrmGetObjectIndex(form, FriendsRegisterButton));
    else
        FrmShowObject(form, FrmGetObjectIndex(form, FriendsRegisterButton));
}

static void RedrawFriends(void)
{
    FormType *form = FrmGetActiveForm();
    RectangleType r;

    ShowFriends(form);
    FrmGetObjectBounds(form, FrmGetObjectIndex(form, FriendsList), &r);
    WinEraseRectangle(&r, 0);
    LstDrawList(GetObject(form, FriendsList));
    DrawFriendsInfo();
}

static Boolean FriendsShowing(void)
{
    FormType *form = FrmGetFormPtr(FriendsForm);

    return form && FrmGetActiveForm() == form && FrmVisible(form);
}

static void FriendsChanged(void)
{
    if (FriendsShowing())
        RedrawFriends();
}

static void LogChanged(void)
{
    if (FriendsShowing())
        DrawLog();
}

static void Send(FormType *form)
{
    Int16 sel = LstGetSelection(GetObject(form, FriendsList));
    char line[24];
    const PpFriend *f;

    if (gNet != NET_IN) {
        FrmCustomAlert(MessageAlert, gP.petId ? "Not connected to the friend server."
                                              : "Register your pet first.", "", "");
        return;
    }
    if (sel < 0 || sel >= gFriends.count) {
        FrmCustomAlert(MessageAlert, "Pick a friend in the list.", "", "");
        return;
    }
    f = &gFriends.list[sel];
    if (!f->online) {
        FrmCustomAlert(MessageAlert, f->name, " is not online now.", "");
        return;
    }
    StrPrintF(line, "SAY|%u|%u", f->id, gSayCode);
    NetSend(line);
}

static Boolean FriendsHandleEvent(EventType *event)
{
    FormType *form = FrmGetActiveForm();
    EventType connect;

    switch (event->eType) {
    case frmOpenEvent:
        ShowFriends(form);
        CtlSetLabel(GetObject(form, FriendsSayTrigger), pp_message(gSayCode));
        LstSetSelection(GetObject(form, FriendsSayList), gSayCode);
        FrmDrawForm(form);
        DrawFriendsInfo();
        DrawLog();
        if (gNet == NET_OFF && gP.online) {
            MemSet(&connect, sizeof(connect), 0);
            connect.eType = connectEvent;
            EvtAddEventToQueue(&connect);
        }
        return true;
    case frmUpdateEvent:
        FrmDrawForm(form);
        DrawFriendsInfo();
        DrawLog();
        return true;
    case connectEvent:
        gNoFujiNet = false;
        NetConnect();
        return true;
    case popSelectEvent:
        if (event->data.popSelect.listID == FriendsSayList)
            gSayCode = (UInt8)event->data.popSelect.selection;
        return false;
    case ctlSelectEvent:
        switch (event->data.ctlSelect.controlID) {
        case FriendsSendButton:
            Send(form);
            return true;
        case FriendsDoneButton:
            FrmGotoForm(MainForm);
            return true;
        case FriendsRegisterButton:
            Register();
            return true;
        case FriendsRefreshButton:
            if (gNet == NET_OFF) {
                gNoFujiNet = false;
                NetConnect();
            } else {
                gFriends.count = 0;
                NetSend("LIST");
                RedrawFriends();
            }
            return true;
        }
        break;
    case menuEvent:
        MainMenuChoice(event->data.menu.itemID);
        return true;
    default:
        break;
    }
    return false;
}

/* ---- Status form ------------------------------------------------------- */

static void DrawHearts(const char *label, UInt8 full, Coord y)
{
    UInt8 i;

    FntSetFont(boldFont);
    WinDrawChars(label, StrLen(label), 4, y + 3);
    BeginDraw();
    for (i = 0; i < PET_HEARTS; i++)
        DrawSprite(ART_HEART, 64 + i * 18, y, 1, PEN_WHITE, i < full ? PEN_RED : PEN_WHITE,
                   PEN_BLACK, false);
    EndDraw();
}

static void DrawStatus(void)
{
    Pet *pet = &gP.pet;
    char text[48];
    UInt8 i;

    FntSetFont(boldFont);
    StrPrintF(text, "%s the %s", pet->name, pet_type_name(pet->type));
    WinDrawChars(text, StrLen(text), 4, 15);
    FntSetFont(stdFont);
    if (pet->stage == STAGE_EGG || pet->stage == STAGE_DEAD)
        StrCopy(text, pet_stage_name(pet->stage));
    else
        StrPrintF(text, "%s: %s", pet_stage_name(pet->stage), pet_form_name(pet));
    WinDrawChars(text, StrLen(text), 4, 27);
    StrPrintF(text, "Age %u   Weight %ug", pet_age_days(pet), pet->weight);
    WinDrawChars(text, StrLen(text), 4, 39);
    DrawHearts("Hungry", pet->hunger, 52);
    DrawHearts("Happy", pet->happy, 72);
    FntSetFont(boldFont);
    WinDrawChars("Discipline", 10, 4, 95);
    BeginDraw();
    for (i = 0; i < 4; i++) {
        Fill(PEN_BLACK, 64 + i * 18, 95, 16, 10);
        Fill(i < pet->discipline ? PEN_ICON : PEN_WHITE, 65 + i * 18, 96, 14, 8);
    }
    EndDraw();
    FntSetFont(stdFont);
    if (pet->stage == STAGE_DEAD)
        StrPrintF(text, "%s", pet_death_text(pet));
    else if (pet->sick)
        StrCopy(text, "Sick! It needs medicine.");
    else
        StrPrintF(text, "Care mistakes: %u", pet->totalMistakes);
    WinDrawChars(text, StrLen(text), 4, 111);
}

static Boolean StatusHandleEvent(EventType *event)
{
    switch (event->eType) {
    case frmOpenEvent:
    case frmUpdateEvent:
        FrmDrawForm(FrmGetActiveForm());
        DrawStatus();
        return true;
    case ctlSelectEvent:
        if (event->data.ctlSelect.controlID == StatusDoneButton) {
            FrmReturnToForm(0);
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

/* ---- Settings form ----------------------------------------------------- */

static void SettingsOK(FormType *form)
{
    char server[SERVER_LEN];
    UInt16 speed = SPEED_NORMAL;
    UInt8 online;
    Boolean reconnect;

    GetFieldText(form, SettingsServerField, server, sizeof(server));
    if (server[0] == '\0')
        StrCopy(server, DEFAULT_SERVER);
    if (CtlGetValue(GetObject(form, SettingsFastPush)))
        speed = SPEED_FAST;
    else if (CtlGetValue(GetObject(form, SettingsTurboPush)))
        speed = SPEED_TURBO;
    online = CtlGetValue(GetObject(form, SettingsOnlineCheck)) ? 1 : 0;
    gP.sound = CtlGetValue(GetObject(form, SettingsSoundCheck)) ? 1 : 0;

    if (speed != gP.speed) {
        CatchUp(false);
        gP.speed = speed;
        gP.lastSeconds = TimGetSeconds();
        /* Back to normal time, the pet's clock is the Palm's again. */
        if (speed == SPEED_NORMAL)
            gP.pet.clock = TimGetSeconds() / 60;
    }
    reconnect = online != gP.online || StrCompare(server, gP.server) != 0;
    gP.online = online;
    StrCopy(gP.server, server);
    SavePrefs();
    FrmReturnToForm(0);
    if (reconnect) {
        NetClose();
        gNoFujiNet = false;
        StrCopy(gNetText, online ? "Connecting..." : "Offline");
        if (online)
            gNextRetry = TimGetTicks();
    }
}

static Boolean SettingsHandleEvent(EventType *event)
{
    FormType *form = FrmGetActiveForm();
    UInt16 push;

    switch (event->eType) {
    case frmOpenEvent:
        SetFieldText(form, SettingsServerField, gP.server);
        push = gP.speed == SPEED_TURBO ? SettingsTurboPush :
               gP.speed == SPEED_FAST ? SettingsFastPush : SettingsNormalPush;
        CtlSetValue(GetObject(form, push), 1);
        CtlSetValue(GetObject(form, SettingsOnlineCheck), gP.online);
        CtlSetValue(GetObject(form, SettingsSoundCheck), gP.sound);
        FrmDrawForm(form);
        return true;
    case ctlSelectEvent:
        if (event->data.ctlSelect.controlID == SettingsOKButton) {
            SettingsOK(form);
            return true;
        }
        if (event->data.ctlSelect.controlID == SettingsCancelButton) {
            FrmReturnToForm(0);
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

/* ---- Application ------------------------------------------------------- */

static Boolean AppHandleEvent(EventType *event)
{
    FormType *form;
    UInt16 id;

    if (event->eType == winExitEvent || event->eType == winEnterEvent) {
        form = FrmGetFormPtr(MainForm);
        if (form && event->eType == winExitEvent &&
            event->data.winExit.exitWindow == (WinHandle)FrmGetWindowHandle(form))
            gCovered = true;
        if (form && event->eType == winEnterEvent &&
            event->data.winEnter.enterWindow == (WinHandle)FrmGetWindowHandle(form))
            gCovered = false;
        return false;
    }
    if (event->eType != frmLoadEvent)
        return false;
    id = event->data.frmLoad.formID;
    form = FrmInitForm(id);
    FrmSetActiveForm(form);
    switch (id) {
    case MainForm: FrmSetEventHandler(form, MainHandleEvent); break;
    case NewPetForm: FrmSetEventHandler(form, NewPetHandleEvent); break;
    case FriendsForm: FrmSetEventHandler(form, FriendsHandleEvent); break;
    case StatusForm: FrmSetEventHandler(form, StatusHandleEvent); break;
    case SettingsForm: FrmSetEventHandler(form, SettingsHandleEvent); break;
    }
    return true;
}

static void EventLoop(void)
{
    EventType event;
    Err err;

    do {
        EvtGetEvent(&event, TICK_TICKS);
        /* A menu or the system drew over the screen: skip this pass's frame. */
        if (SysHandleEvent(&event))
            continue;
        if (MenuHandleEvent(0, &event, &err))
            continue;
        if (!AppHandleEvent(&event))
            FrmDispatchEvent(&event);
        if (event.eType != appStopEvent)
            Idle();
    } while (event.eType != appStopEvent);
}

/* Palm OS 3.1, the oldest release the app is tested on. */
static Boolean RomCompatible(void)
{
    UInt32 rom = 0;

    FtrGet(sysFtrCreator, sysFtrNumROMVersion, &rom);
    return rom >= sysMakeROMVersion(3, 1, 0, sysROMStageRelease, 0);
}

/* Colour on OS 3.5 and later, if the screen takes 8 bits (or 4 or 2
 * greys); believe only what it reports after the request. */
static Boolean OpenColour(void)
{
    UInt32 rom = 0, depth, got = 1;
    RGBColorType rgb;
    UInt8 i;

    FtrGet(sysFtrCreator, sysFtrNumROMVersion, &rom);
    if (rom < sysMakeROMVersion(3, 5, 0, sysROMStageRelease, 0))
        return false;
    for (depth = 8; depth >= 2; depth /= 2) {
        WinScreenMode(winScreenModeSet, NULL, NULL, &depth, NULL);
        if (WinScreenMode(winScreenModeGet, NULL, NULL, &got, NULL) == errNone && got == depth)
            break;
    }
    if (got < 2) {
        WinScreenMode(winScreenModeSetToDefaults, NULL, NULL, NULL, NULL);
        return false;
    }
    for (i = 0; i < PEN_COUNT; i++) {
        rgb.index = 0;
        rgb.r = penRGB[i][0];
        rgb.g = penRGB[i][1];
        rgb.b = penRGB[i][2];
        gIndex[i] = WinRGBToIndex(&rgb);
    }
    return true;
}

static void LoadPrefs(void)
{
    UInt16 size = sizeof(gP);

    if (PrefGetAppPreferences(CREATOR, PREFS_ID, &gP, &size, true) != PREFS_VERSION ||
        size != sizeof(gP)) {
        MemSet(&gP, sizeof(gP), 0);
        StrCopy(gP.server, DEFAULT_SERVER);
        gP.speed = SPEED_NORMAL;
        gP.online = 1;
        gP.sound = 1;
    }
    gP.server[SERVER_LEN - 1] = '\0';
    gP.key[PP_KEY_LEN - 1] = '\0';
    gP.pet.name[PET_NAME_LEN - 1] = '\0';
    if (gP.speed != SPEED_FAST && gP.speed != SPEED_TURBO)
        gP.speed = SPEED_NORMAL;
}

static void AppStart(void)
{
    Err err;

    LoadPrefs();
    gColour = OpenColour();
    gOffscreen = WinCreateOffscreenWindow(160, 160, screenFormat, &err);
    gPortOpen = FnOpen() == errNone;
    if (!gPortOpen)
        StrCopy(gNetText, "Cradle port busy");
    CatchUp(true);
}

static void AppStop(void)
{
    if (gP.hasPet)
        CatchUp(true);
    FrmCloseAllForms();
    NetClose();
    SavePrefs();
    FnClose();
    if (gOffscreen)
        WinDeleteWindow(gOffscreen, false);
    gOffscreen = NULL;
    if (gColour)
        WinScreenMode(winScreenModeSetToDefaults, NULL, NULL, NULL, NULL);
}

UInt32 PilotMain(UInt16 cmd, MemPtr cmdPBP, UInt16 launchFlags)
{
    if (cmd != sysAppLaunchCmdNormalLaunch)
        return 0;
    if (!RomCompatible()) {
        FrmAlert(RomIncompatibleAlert);
        return 0;
    }
    AppStart();
    FrmGotoForm(gP.hasPet ? MainForm : NewPetForm);
    EventLoop();
    AppStop();
    return 0;
}
