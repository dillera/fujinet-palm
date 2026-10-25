/* A FujiPet's life, after the original Tamagotchi (P1): portable C, so
 * tests/fujipet_test.c can grow pets on a desktop.
 *
 * Time runs in pet minutes; pet_advance() is told how many have passed. An
 * egg hatches into a baby after 5 minutes, which becomes a child after an
 * hour, a teen a day later and an adult two days after that. How well it was
 * looked after picks the teen and adult it becomes, and how long it lives:
 * the best live into old age as seniors. Hunger and happiness each drain a
 * heart at a time; an empty one makes the pet call for attention, and a
 * call left for 15 minutes is a care mistake. So is leaving the light on
 * once it is asleep. It poops, gets sick from mess and sweets (medicine
 * cures it), and children and teens call for no reason to test their
 * discipline: scolding them then teaches it, scolding otherwise only upsets
 * them. Twelve hours starving or sick kills it.
 */
#ifndef PET_H
#define PET_H

#include "fn_types.h"

#define PET_NAME_LEN 11 /* 10 letters and a NUL */
#define PET_HEARTS 4
#define PET_CALL_LIMIT 15           /* minutes before a call is a care mistake */
#define PET_DAY (24L * 60)
#define PET_EGG_MINUTES 5
#define PET_BABY_MINUTES 65
#define PET_CHILD_MINUTES PET_DAY
#define PET_TEEN_MINUTES (2 * PET_DAY)
#define PET_SENIOR_DAYS 10
#define PET_FATAL_MINUTES (12 * 60) /* starving or sick this long */

enum { PET_CAT, PET_DOG, PET_BUNNY, PET_DINO, PET_TYPES };

enum {
    STAGE_EGG, STAGE_BABY, STAGE_CHILD, STAGE_TEEN, STAGE_ADULT, STAGE_SENIOR, STAGE_DEAD,
    PET_STAGES
};

/* What the pet is calling for (Pet.call). */
enum { CALL_NONE, CALL_HUNGRY, CALL_UNHAPPY, CALL_LIGHTS, CALL_NAUGHTY };

enum { DIED_NOT, DIED_OLD, DIED_STARVED, DIED_SICK };

/* Bits pet_advance() returns, for what happened while time passed. */
#define PET_EV_HATCH   0x0001
#define PET_EV_EVOLVE  0x0002
#define PET_EV_POOP    0x0004
#define PET_EV_SICK    0x0008
#define PET_EV_CALL    0x0010
#define PET_EV_MISTAKE 0x0020
#define PET_EV_SLEEP   0x0040
#define PET_EV_WAKE    0x0080
#define PET_EV_DIED    0x0100
#define PET_EV_HUNGER  0x0200 /* a heart drained */

/* What an action did. */
enum {
    PET_OK,         /* done */
    PET_NO,         /* nothing to do: no mess, not sick, not naughty */
    PET_REFUSED,    /* will not: full, or too sick to eat */
    PET_ASLEEP,
    PET_NOT_YET,    /* still an egg, or gone */
    PET_CURED
};

typedef struct {
    char name[PET_NAME_LEN];
    fn_u8 type;        /* PET_CAT ... */
    fn_u8 stage;       /* STAGE_EGG ... */
    fn_u8 form;        /* teen 0 good, 1 poor; adult 0 best, 1 fair, 2 poor */
    fn_u8 hunger;      /* full hearts, 0..PET_HEARTS */
    fn_u8 happy;
    fn_u8 discipline;  /* quarters, 0..4 */
    fn_u8 weight;
    fn_u8 poops;       /* 0..4 */
    fn_u8 sick;        /* doses of medicine still needed; 0 is well */
    fn_u8 asleep;
    fn_u8 lightsOff;
    fn_u8 call;        /* CALL_... */
    fn_u8 mistakes;    /* care mistakes in this stage */
    fn_u8 totalMistakes;
    fn_u8 sweets;      /* snacks lately */
    fn_u8 died;        /* DIED_... */
    fn_u8 lifespan;    /* days, fixed when it grows up */
    fn_u8 mistakeMade; /* the current call already counted as a mistake */
    fn_u16 callMin;    /* minutes the call has waited */
    fn_u16 hungerMin;  /* minutes to the next heart lost */
    fn_u16 happyMin;
    fn_u16 poopMin;    /* minutes to the next poop */
    fn_u16 naughtyMin; /* minutes to the next call for no reason */
    fn_u16 sweetMin;   /* minutes until a snack is forgotten */
    fn_u16 starveMin;  /* minutes with no hunger hearts */
    fn_u16 sickMin;    /* minutes sick */
    fn_u32 ageMin;     /* since hatching */
    fn_u32 stageMin;   /* since this stage began */
    fn_u32 clock;      /* the pet's clock in minutes; % PET_DAY is the time of day */
    fn_u32 seed;
} Pet;

void pet_new(Pet *pet, const char *name, fn_u8 type, fn_u32 clock, fn_u32 seed);
/* Lets minutes pass; returns the PET_EV_ bits of what happened. */
fn_u16 pet_advance(Pet *pet, fn_u32 minutes);

int pet_meal(Pet *pet);
int pet_snack(Pet *pet);
/* After the left-or-right game: won is whether the player won it. */
int pet_play(Pet *pet, int won);
int pet_clean(Pet *pet);
int pet_medicine(Pet *pet);
/* Turns the light off or on; returns the new lightsOff. */
int pet_lights(Pet *pet);
int pet_scold(Pet *pet);
/* A friend's kind message; PET_OK if it cheered the pet up. */
int pet_cheer(Pet *pet);

/* A random number below n, from the pet's own generator. */
fn_u16 pet_random(Pet *pet, fn_u16 n);

fn_u16 pet_age_days(const Pet *pet);
fn_u16 pet_minute_of_day(const Pet *pet);
int pet_is_awake(const Pet *pet);
/* The pet's sleep hours as minutes of the day; both 0 when it naps instead. */
void pet_bedtime(const Pet *pet, fn_u16 *sleep, fn_u16 *wake);

const char *pet_type_name(fn_u8 type);
const char *pet_stage_name(fn_u8 stage);
/* The name of what it has grown into: "Kitling", "Starcat"... */
const char *pet_form_name(const Pet *pet);
const char *pet_death_text(const Pet *pet);

#endif /* PET_H */
