#include "pet.h"

/* Per stage: minutes for a hunger and a happy heart to drain, between
 * poops, the least weight, and the sleep hours. Teens and adults in the
 * poorer forms get hungry and bored sooner. */
#define SNACK_MEMORY 120
#define MAX_WEIGHT 99
#define NAUGHTY_GAP 15

/* Names as NUL-separated lists, not tables of pointers: a pointer table
 * would need relocating, which a Palm code segment cannot have. */
static const char type_names[] = "Cat\0Dog\0Bunny\0Dino";
static const char stage_names[] = "Egg\0Baby\0Child\0Teen\0Adult\0Senior\0Angel";
/* For each type: child, good teen, poor teen, best/fair/poor adult, senior. */
#define FORMS_PER_TYPE 7
static const char form_names[] =
    "Kitling\0Whiskit\0Scruffkit\0Starcat\0Tabbykit\0Grumpuss\0Old Tom\0"
    "Pupling\0Wagster\0Scamp\0Starpup\0Barkley\0Grumbark\0Old Rover\0"
    "Bunling\0Hopsy\0Fluffnub\0Starbun\0Thumper\0Grumbun\0Old Hare\0"
    "Dinolet\0Stompy\0Snaggle\0Stardino\0Rexxy\0Grumblesaur\0Old Rex";

static const char *nth(const char *list, fn_u16 n)
{
    while (n-- > 0)
        while (*list++)
            ;
    return list;
}

fn_u16 pet_random(Pet *pet, fn_u16 n)
{
    pet->seed = pet->seed * 1103515245UL + 12345UL;
    return n ? (fn_u16)((pet->seed >> 16) % n) : 0;
}

static fn_u16 hunger_minutes(const Pet *pet)
{
    switch (pet->stage) {
    case STAGE_BABY: return 8;
    case STAGE_CHILD: return 45;
    case STAGE_TEEN: return pet->form ? 55 : 65;
    case STAGE_ADULT: return 80 - 10 * pet->form;
    default: return 70;
    }
}

static fn_u16 happy_minutes(const Pet *pet)
{
    switch (pet->stage) {
    case STAGE_BABY: return 7;
    case STAGE_CHILD: return 40;
    case STAGE_TEEN: return pet->form ? 50 : 60;
    case STAGE_ADULT: return 75 - 10 * pet->form;
    default: return 65;
    }
}

static fn_u16 poop_minutes(Pet *pet)
{
    switch (pet->stage) {
    case STAGE_BABY: return 20 + pet_random(pet, 10);
    case STAGE_CHILD: return 90 + pet_random(pet, 30);
    case STAGE_TEEN: return 150 + pet_random(pet, 30);
    case STAGE_ADULT: return 180 + pet_random(pet, 30);
    default: return 160 + pet_random(pet, 30);
    }
}

/* Children and teens test their discipline every few hours. */
static fn_u16 naughty_minutes(Pet *pet)
{
    if (pet->stage == STAGE_CHILD)
        return 120 + pet_random(pet, 120);
    if (pet->stage == STAGE_TEEN)
        return 180 + pet_random(pet, 180);
    return 0;
}

static fn_u8 base_weight(fn_u8 stage)
{
    switch (stage) {
    case STAGE_BABY: return 5;
    case STAGE_CHILD: return 10;
    case STAGE_TEEN: return 20;
    case STAGE_ADULT: return 30;
    default: return 25;
    }
}

void pet_bedtime(const Pet *pet, fn_u16 *sleep, fn_u16 *wake)
{
    *sleep = *wake = 0;
    switch (pet->stage) {
    case STAGE_CHILD: *sleep = 20 * 60; *wake = 9 * 60; break;
    case STAGE_TEEN: *sleep = 21 * 60; *wake = 9 * 60; break;
    case STAGE_ADULT: *sleep = 22 * 60; *wake = 9 * 60; break;
    case STAGE_SENIOR: *sleep = 21 * 60; *wake = 8 * 60; break;
    }
}

static int bedtime(const Pet *pet)
{
    fn_u16 sleep, wake, now = pet_minute_of_day(pet);

    pet_bedtime(pet, &sleep, &wake);
    if (sleep == wake)
        return 0;
    return now >= sleep || now < wake;
}

fn_u16 pet_minute_of_day(const Pet *pet)
{
    return (fn_u16)(pet->clock % PET_DAY);
}

fn_u16 pet_age_days(const Pet *pet)
{
    return (fn_u16)(pet->ageMin / PET_DAY);
}

static int alive(const Pet *pet)
{
    return pet->stage != STAGE_EGG && pet->stage != STAGE_DEAD;
}

int pet_is_awake(const Pet *pet)
{
    return alive(pet) && !pet->asleep;
}

static void reset_timers(Pet *pet)
{
    pet->hungerMin = hunger_minutes(pet);
    pet->happyMin = happy_minutes(pet);
    pet->poopMin = poop_minutes(pet);
    pet->naughtyMin = naughty_minutes(pet);
    if (pet->weight < base_weight(pet->stage))
        pet->weight = base_weight(pet->stage);
}

static void start_call(Pet *pet, fn_u8 call)
{
    pet->call = call;
    pet->callMin = 0;
    pet->mistakeMade = 0;
}

/* Calls for food or fun when a heart runs out, and stops when it is
 * filled; a naughty call gives way to a real one. */
static fn_u16 update_call(Pet *pet)
{
    fn_u8 want = CALL_NONE;

    if (pet->asleep)
        return 0;
    if (pet->hunger == 0)
        want = CALL_HUNGRY;
    else if (pet->happy == 0)
        want = CALL_UNHAPPY;
    if (want == pet->call)
        return 0;
    if (want == CALL_NONE) {
        if (pet->call == CALL_HUNGRY || pet->call == CALL_UNHAPPY)
            pet->call = CALL_NONE;
        return 0;
    }
    start_call(pet, want);
    return PET_EV_CALL;
}

static void get_sick(Pet *pet, fn_u16 percent, fn_u16 *events)
{
    if (pet->sick || pet_random(pet, 100) >= percent)
        return;
    pet->sick = 1 + (fn_u8)pet_random(pet, 2);
    pet->sickMin = 0;
    if (events)
        *events |= PET_EV_SICK;
}

static void check_weight(Pet *pet, fn_u16 *events)
{
    if (pet->weight > base_weight(pet->stage) + 20)
        get_sick(pet, 15, events);
}

static void die(Pet *pet, fn_u8 cause)
{
    pet->stage = STAGE_DEAD;
    pet->died = cause;
    pet->asleep = 0;
    pet->lightsOff = 0;
    pet->call = CALL_NONE;
    pet->sick = 0;
}

static fn_u16 grow(Pet *pet)
{
    switch (pet->stage) {
    case STAGE_BABY:
        if (pet->stageMin < PET_BABY_MINUTES)
            return 0;
        pet->stage = STAGE_CHILD;
        break;
    case STAGE_CHILD:
        if (pet->stageMin < PET_CHILD_MINUTES)
            return 0;
        pet->stage = STAGE_TEEN;
        pet->form = pet->mistakes <= 2 ? 0 : 1;
        break;
    case STAGE_TEEN:
        if (pet->stageMin < PET_TEEN_MINUTES)
            return 0;
        pet->stage = STAGE_ADULT;
        if (pet->form == 0 && pet->mistakes <= 2 && pet->discipline >= 3)
            pet->form = 0;
        else if (pet->mistakes <= 4 && pet->discipline >= 2)
            pet->form = 1;
        else
            pet->form = 2;
        pet->lifespan = (fn_u8)(10 + (pet->form == 0 ? 6 : pet->form == 1 ? 3 : 1) +
                                pet_random(pet, 3));
        pet->lifespan -= (fn_u8)(pet->totalMistakes / 4 > 4 ? 4 : pet->totalMistakes / 4);
        if (pet->lifespan < 7)
            pet->lifespan = 7;
        break;
    case STAGE_ADULT:
        if (pet->lifespan <= PET_SENIOR_DAYS || pet_age_days(pet) < PET_SENIOR_DAYS)
            return 0;
        pet->stage = STAGE_SENIOR;
        break;
    default:
        return 0;
    }
    pet->stageMin = 0;
    pet->mistakes = 0;
    if (pet->call == CALL_NAUGHTY)
        pet->call = CALL_NONE;
    reset_timers(pet);
    return PET_EV_EVOLVE;
}

static fn_u16 tick_asleep(Pet *pet)
{
    fn_u16 events = 0;

    if (pet->call == CALL_LIGHTS) {
        if (pet->lightsOff) {
            pet->call = CALL_NONE;
        } else if (++pet->callMin >= PET_CALL_LIMIT && !pet->mistakeMade) {
            pet->mistakes++;
            pet->totalMistakes++;
            pet->mistakeMade = 1;
            events |= PET_EV_MISTAKE;
        }
    }
    if (!bedtime(pet)) {
        pet->asleep = 0;
        pet->lightsOff = 0;
        if (pet->call == CALL_LIGHTS)
            pet->call = CALL_NONE;
        events |= PET_EV_WAKE | update_call(pet);
    }
    return events;
}

static fn_u16 tick_awake(Pet *pet)
{
    fn_u16 events = 0;

    if (bedtime(pet)) {
        pet->asleep = 1;
        pet->call = CALL_NONE;
        events |= PET_EV_SLEEP;
        if (!pet->lightsOff) {
            start_call(pet, CALL_LIGHTS);
            events |= PET_EV_CALL;
        }
        return events;
    }

    if (--pet->hungerMin == 0) {
        pet->hungerMin = hunger_minutes(pet);
        if (pet->hunger > 0) {
            pet->hunger--;
            events |= PET_EV_HUNGER;
        }
    }
    if (--pet->happyMin == 0) {
        pet->happyMin = happy_minutes(pet);
        if (pet->happy > 0) {
            pet->happy--;
            events |= PET_EV_HUNGER;
        }
    }
    if (--pet->poopMin == 0) {
        pet->poopMin = poop_minutes(pet);
        if (pet->poops < 4) {
            pet->poops++;
            events |= PET_EV_POOP;
        }
        if (pet->poops >= 2)
            get_sick(pet, 10 * (pet->poops - 1), &events);
    }
    if (pet->sweets && --pet->sweetMin == 0) {
        pet->sweets--;
        pet->sweetMin = SNACK_MEMORY;
    }

    events |= update_call(pet);
    if (pet->call == CALL_NONE && pet->naughtyMin && --pet->naughtyMin == 0) {
        start_call(pet, CALL_NAUGHTY);
        events |= PET_EV_CALL;
    }
    if (pet->call == CALL_NAUGHTY) {
        /* Nobody scolded it in time: the chance to teach it has passed. */
        if (++pet->callMin >= PET_CALL_LIMIT) {
            pet->call = CALL_NONE;
            pet->naughtyMin = naughty_minutes(pet);
        }
    } else if (pet->call != CALL_NONE && ++pet->callMin >= PET_CALL_LIMIT &&
               !pet->mistakeMade) {
        pet->mistakes++;
        pet->totalMistakes++;
        pet->mistakeMade = 1;
        events |= PET_EV_MISTAKE;
    }

    pet->starveMin = pet->hunger == 0 ? pet->starveMin + 1 : 0;
    if (pet->sick)
        pet->sickMin++;
    if (pet->starveMin >= PET_FATAL_MINUTES) {
        die(pet, DIED_STARVED);
        return events | PET_EV_DIED;
    }
    if (pet->sick && pet->sickMin >= PET_FATAL_MINUTES) {
        die(pet, DIED_SICK);
        return events | PET_EV_DIED;
    }
    return events | grow(pet);
}

static fn_u16 tick(Pet *pet)
{
    pet->clock++;
    pet->stageMin++;
    if (pet->stage == STAGE_EGG) {
        if (pet->stageMin < PET_EGG_MINUTES)
            return 0;
        /* Hatched hungry and bored, as they are. */
        pet->stage = STAGE_BABY;
        pet->stageMin = 0;
        pet->ageMin = 0;
        pet->hunger = pet->happy = 0;
        reset_timers(pet);
        return PET_EV_HATCH | update_call(pet);
    }
    pet->ageMin++;
    if (pet->stage >= STAGE_ADULT && pet->lifespan &&
        pet_age_days(pet) >= pet->lifespan) {
        die(pet, DIED_OLD);
        return PET_EV_DIED;
    }
    return pet->asleep ? tick_asleep(pet) : tick_awake(pet);
}

fn_u16 pet_advance(Pet *pet, fn_u32 minutes)
{
    fn_u16 events = 0;

    while (minutes-- > 0 && pet->stage != STAGE_DEAD)
        events |= tick(pet);
    return events;
}

void pet_new(Pet *pet, const char *name, fn_u8 type, fn_u32 clock, fn_u32 seed)
{
    fn_u16 i;

    fn_memset(pet, 0, sizeof(*pet));
    for (i = 0; name[i] && i < PET_NAME_LEN - 1; i++)
        pet->name[i] = name[i];
    pet->type = type < PET_TYPES ? type : PET_CAT;
    pet->stage = STAGE_EGG;
    pet->clock = clock;
    pet->seed = seed ? seed : 1;
}

/* ---- Care ------------------------------------------------------------ */

static int can_act(const Pet *pet)
{
    if (!alive(pet))
        return PET_NOT_YET;
    if (pet->asleep)
        return PET_ASLEEP;
    return PET_OK;
}

int pet_meal(Pet *pet)
{
    int ok = can_act(pet);

    if (ok != PET_OK)
        return ok;
    if (pet->sick || pet->hunger >= PET_HEARTS)
        return PET_REFUSED;
    pet->hunger++;
    if (pet->weight < MAX_WEIGHT)
        pet->weight++;
    pet->hungerMin = hunger_minutes(pet);
    check_weight(pet, 0);
    update_call(pet);
    return PET_OK;
}

int pet_snack(Pet *pet)
{
    int ok = can_act(pet);

    if (ok != PET_OK)
        return ok;
    if (pet->sick)
        return PET_REFUSED;
    if (pet->happy < PET_HEARTS)
        pet->happy++;
    pet->weight = pet->weight + 2 > MAX_WEIGHT ? MAX_WEIGHT : pet->weight + 2;
    pet->sweets++;
    pet->sweetMin = SNACK_MEMORY;
    if (pet->sweets >= 4)
        get_sick(pet, 25, 0);
    check_weight(pet, 0);
    update_call(pet);
    return PET_OK;
}

int pet_play(Pet *pet, int won)
{
    int ok = can_act(pet);

    if (ok != PET_OK)
        return ok;
    if (pet->sick)
        return PET_REFUSED;
    if (pet->weight > base_weight(pet->stage))
        pet->weight--;
    if (won && pet->happy < PET_HEARTS)
        pet->happy++;
    update_call(pet);
    return PET_OK;
}

int pet_clean(Pet *pet)
{
    if (!alive(pet))
        return PET_NOT_YET;
    if (pet->poops == 0)
        return PET_NO;
    pet->poops = 0;
    return PET_OK;
}

int pet_medicine(Pet *pet)
{
    if (!alive(pet))
        return PET_NOT_YET;
    if (!pet->sick)
        return PET_NO;
    if (--pet->sick > 0)
        return PET_OK;
    pet->sickMin = 0;
    return PET_CURED;
}

int pet_lights(Pet *pet)
{
    pet->lightsOff = !pet->lightsOff;
    if (pet->lightsOff && pet->call == CALL_LIGHTS)
        pet->call = CALL_NONE;
    return pet->lightsOff;
}

int pet_scold(Pet *pet)
{
    int ok = can_act(pet);

    if (ok != PET_OK)
        return ok;
    if (pet->call == CALL_NAUGHTY) {
        if (pet->discipline < 4)
            pet->discipline++;
        pet->call = CALL_NONE;
        pet->naughtyMin = naughty_minutes(pet);
        return PET_OK;
    }
    if (pet->happy > 0)
        pet->happy--;
    update_call(pet);
    return PET_NO;
}

int pet_cheer(Pet *pet)
{
    int ok = can_act(pet);

    if (ok != PET_OK)
        return ok;
    if (pet->happy >= PET_HEARTS)
        return PET_NO;
    pet->happy++;
    update_call(pet);
    return PET_OK;
}

/* ---- Names ----------------------------------------------------------- */

const char *pet_type_name(fn_u8 type)
{
    return type < PET_TYPES ? nth(type_names, type) : "?";
}

const char *pet_stage_name(fn_u8 stage)
{
    return stage < PET_STAGES ? nth(stage_names, stage) : "?";
}

const char *pet_form_name(const Pet *pet)
{
    fn_u16 first = (pet->type < PET_TYPES ? pet->type : 0) * FORMS_PER_TYPE;

    switch (pet->stage) {
    case STAGE_BABY: return "Bitty";
    case STAGE_CHILD: return nth(form_names, first);
    case STAGE_TEEN: return nth(form_names, first + 1 + (pet->form ? 1 : 0));
    case STAGE_ADULT: return nth(form_names, first + 3 + (pet->form < 3 ? pet->form : 2));
    case STAGE_SENIOR: return nth(form_names, first + 6);
    default: return pet_stage_name(pet->stage);
    }
}

const char *pet_death_text(const Pet *pet)
{
    switch (pet->died) {
    case DIED_OLD:
        return pet->lifespan > PET_SENIOR_DAYS ? "lived a long, happy life." : "grew old.";
    case DIED_STARVED: return "went hungry too long.";
    case DIED_SICK: return "was sick too long.";
    default: return "";
    }
}
