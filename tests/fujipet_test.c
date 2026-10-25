/* Grows FujiPets on the desktop (apps/FujiPet/pet.c) and reads the friend
 * server's lines (petproto.c). */
#include <stdio.h>
#include <string.h>

#include "pet.h"
#include "petproto.h"

static int failures;

#define CHECK(cond) do { if (!(cond)) { \
    printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

#define EIGHT_AM (8L * 60)

/* Answers every need the minute it shows: the best keeper there is. */
static void care_well(Pet *p)
{
    if (p->stage == STAGE_EGG || p->stage == STAGE_DEAD)
        return;
    if (p->asleep) {
        if (!p->lightsOff)
            pet_lights(p);
        return;
    }
    while (p->sick)
        pet_medicine(p);
    pet_clean(p);
    if (p->call == CALL_NAUGHTY)
        pet_scold(p);
    while (p->hunger < 3 && pet_meal(p) == PET_OK)
        ;
    if (p->happy < 3)
        pet_play(p, 1);
}

/* Looks in every four hours, feeds it and never scolds. */
static void care_lazily(Pet *p, long minute)
{
    if (minute % 240 != 0 || p->stage == STAGE_EGG || p->stage == STAGE_DEAD)
        return;
    if (p->asleep) {
        if (!p->lightsOff)
            pet_lights(p);
        return;
    }
    while (p->sick)
        pet_medicine(p);
    pet_clean(p);
    while (pet_meal(p) == PET_OK)
        ;
    while (p->happy < PET_HEARTS && pet_play(p, 1) == PET_OK)
        ;
}

typedef struct {
    long at[PET_STAGES];   /* minute each stage began, -1 if never */
    fn_u8 teenForm, adultForm;
} Life;

static void live(Pet *p, Life *life, int lazy, long maxMinutes)
{
    long m;
    int i;
    fn_u8 last = p->stage;

    for (i = 0; i < PET_STAGES; i++)
        life->at[i] = -1;
    life->at[STAGE_EGG] = 0;
    for (m = 1; m <= maxMinutes && p->stage != STAGE_DEAD; m++) {
        pet_advance(p, 1);
        if (p->stage != last) {
            last = p->stage;
            life->at[last] = m;
            if (last == STAGE_TEEN)
                life->teenForm = p->form;
            if (last == STAGE_ADULT)
                life->adultForm = p->form;
        }
        if (lazy)
            care_lazily(p, m);
        else
            care_well(p);
    }
}

static void test_a_good_life(void)
{
    Pet p;
    Life life;

    pet_new(&p, "Pip", PET_CAT, EIGHT_AM, 42);
    CHECK(p.stage == STAGE_EGG && strcmp(p.name, "Pip") == 0);
    live(&p, &life, 0, 40 * PET_DAY);

    CHECK(life.at[STAGE_BABY] == PET_EGG_MINUTES);
    CHECK(life.at[STAGE_CHILD] == PET_EGG_MINUTES + PET_BABY_MINUTES);
    /* Each later stage begins on time, or the next morning if asleep. */
    CHECK(life.at[STAGE_TEEN] >= life.at[STAGE_CHILD] + PET_CHILD_MINUTES);
    CHECK(life.at[STAGE_TEEN] < life.at[STAGE_CHILD] + PET_CHILD_MINUTES + 13 * 60);
    CHECK(life.at[STAGE_ADULT] >= life.at[STAGE_TEEN] + PET_TEEN_MINUTES);
    CHECK(life.teenForm == 0);
    CHECK(life.adultForm == 0);
    CHECK(life.at[STAGE_SENIOR] > 0);
    CHECK(p.stage == STAGE_DEAD && p.died == DIED_OLD);
    CHECK(p.lifespan >= 13);
    CHECK(p.totalMistakes == 0);
    CHECK(p.discipline >= 3);
    printf("good life: hatched %ld, child %ld, teen %ld, adult %ld, senior %ld (minutes), "
           "died at %u days\n", life.at[STAGE_BABY], life.at[STAGE_CHILD], life.at[STAGE_TEEN],
           life.at[STAGE_ADULT], life.at[STAGE_SENIOR], pet_age_days(&p));
}

static void test_a_poor_life(void)
{
    Pet p;
    Life life;

    pet_new(&p, "Mochi", PET_DOG, EIGHT_AM, 7);
    live(&p, &life, 1, 40 * PET_DAY);
    CHECK(life.at[STAGE_TEEN] > 0);
    CHECK(life.teenForm == 1);
    CHECK(life.at[STAGE_ADULT] > 0);
    CHECK(life.adultForm == 2);
    CHECK(life.at[STAGE_SENIOR] < 0);
    CHECK(p.stage == STAGE_DEAD);
    CHECK(p.totalMistakes > 4);
    printf("poor life: teen form %u, adult form %u, %u care mistakes, died at %u days (%s)\n",
           life.teenForm, life.adultForm, p.totalMistakes, pet_age_days(&p),
           pet_death_text(&p));
}

static void test_neglect_starves(void)
{
    Pet p;
    fn_u16 events;

    pet_new(&p, "Lost", PET_DINO, EIGHT_AM, 3);
    events = pet_advance(&p, PET_EGG_MINUTES);
    CHECK(events & PET_EV_HATCH);
    CHECK(events & PET_EV_CALL);
    CHECK(p.call == CALL_HUNGRY);
    events = pet_advance(&p, PET_CALL_LIMIT);
    CHECK(events & PET_EV_MISTAKE);
    CHECK(p.mistakes == 1);
    /* It sleeps through the first night, which slows starving. */
    events = pet_advance(&p, 3 * PET_DAY);
    CHECK(events & PET_EV_DIED);
    CHECK(p.stage == STAGE_DEAD && p.died == DIED_STARVED);
}

static void test_care_actions(void)
{
    Pet p;

    pet_new(&p, "Hop", PET_BUNNY, EIGHT_AM, 9);
    CHECK(pet_meal(&p) == PET_NOT_YET);
    pet_advance(&p, PET_EGG_MINUTES);
    CHECK(pet_meal(&p) == PET_OK);
    CHECK(p.call == CALL_UNHAPPY);
    CHECK(pet_meal(&p) == PET_OK && pet_meal(&p) == PET_OK && pet_meal(&p) == PET_OK);
    CHECK(pet_meal(&p) == PET_REFUSED);
    CHECK(pet_snack(&p) == PET_OK);
    CHECK(p.call == CALL_NONE);
    CHECK(pet_clean(&p) == PET_NO);
    CHECK(pet_medicine(&p) == PET_NO);
    CHECK(pet_scold(&p) == PET_NO);
    CHECK(p.happy == 0);
    CHECK(pet_cheer(&p) == PET_OK && p.happy == 1);

    p.sick = 2;
    CHECK(pet_meal(&p) == PET_REFUSED);
    CHECK(pet_medicine(&p) == PET_OK);
    CHECK(pet_medicine(&p) == PET_CURED);

    p.poops = 3;
    CHECK(pet_clean(&p) == PET_OK && p.poops == 0);

    p.weight = 98;
    pet_snack(&p);
    CHECK(p.weight == 99);
}

static void test_bedtime(void)
{
    Pet p;
    fn_u16 events;

    pet_new(&p, "Nap", PET_CAT, 19L * 60, 5);
    pet_advance(&p, PET_EGG_MINUTES);
    p.stage = STAGE_CHILD;
    p.hunger = p.happy = PET_HEARTS;
    p.call = CALL_NONE;
    events = pet_advance(&p, 60);
    CHECK(events & PET_EV_SLEEP);
    CHECK(p.asleep && p.call == CALL_LIGHTS);
    CHECK(pet_meal(&p) == PET_ASLEEP);
    events = pet_advance(&p, PET_CALL_LIMIT);
    CHECK(events & PET_EV_MISTAKE);
    CHECK(pet_lights(&p) == 1 && p.call == CALL_NONE);
    events = pet_advance(&p, 13L * 60);
    CHECK(events & PET_EV_WAKE);
    CHECK(!p.asleep && !p.lightsOff);
    CHECK(pet_minute_of_day(&p) >= 9 * 60);
}

static void test_names(void)
{
    Pet p;

    pet_new(&p, "A very long name", PET_DINO, 0, 1);
    CHECK(strcmp(p.name, "A very lon") == 0);
    CHECK(strcmp(pet_type_name(PET_BUNNY), "Bunny") == 0);
    CHECK(strcmp(pet_stage_name(STAGE_DEAD), "Angel") == 0);
    p.stage = STAGE_ADULT;
    p.form = 2;
    CHECK(strcmp(pet_form_name(&p), "Grumblesaur") == 0);
    p.type = PET_CAT;
    p.stage = STAGE_TEEN;
    p.form = 0;
    CHECK(strcmp(pet_form_name(&p), "Whiskit") == 0);
    p.stage = STAGE_SENIOR;
    CHECK(strcmp(pet_form_name(&p), "Old Tom") == 0);
}

static void test_protocol(void)
{
    PpReader r;
    PpEvent e;
    PpFriends friends;
    PpFriend *f;
    char line[PP_LINE_MAX + 1], name[32];
    const char *bytes = "PET|3|Mochi|1|2|1|0\r\nON|4|Rex|3|4|6\nMSG|4|Rex|5\n";
    int lines = 0, i;

    memset(&friends, 0, sizeof(friends));
    pp_reader_reset(&r);
    for (i = 0; bytes[i]; i++) {
        if (!pp_reader_push(&r, (fn_u8)bytes[i]))
            continue;
        lines++;
        pp_parse(r.line, &e);
        if (lines == 1) {
            CHECK(e.kind == PP_PET && e.id == 3 && strcmp(e.name, "Mochi") == 0);
            CHECK(e.type == 1 && e.stage == 2 && e.age == 1 && e.online == 0);
        } else if (lines == 2) {
            CHECK(e.kind == PP_ON && e.id == 4 && e.online == 1 && e.age == 6);
        } else {
            CHECK(e.kind == PP_MSG && e.id == 4 && e.code == 5);
            CHECK(strcmp(pp_message(e.code), "Yay! Hooray!") == 0);
        }
        if (e.kind == PP_PET || e.kind == PP_ON) {
            f = pp_friend_update(&friends, e.id);
            strcpy(f->name, e.name);
            f->online = e.online;
        }
    }
    CHECK(lines == 3);
    pp_friends_sort(&friends);
    CHECK(friends.count == 2 && friends.list[0].id == 4 && pp_friends_online(&friends) == 1);

    strcpy(line, "REGOK|12|a1b2c3d4");
    pp_parse(line, &e);
    CHECK(e.kind == PP_REGOK && e.id == 12 && strcmp(e.key, "a1b2c3d4") == 0);
    strcpy(line, "ERR|offline|7");
    pp_parse(line, &e);
    CHECK(e.kind == PP_ERR && strcmp(e.key, "offline") == 0 && e.id == 7);
    strcpy(line, "MSG|4|Rex|99");
    pp_parse(line, &e);
    CHECK(e.kind == PP_NONE);
    strcpy(line, "WHAT");
    pp_parse(line, &e);
    CHECK(e.kind == PP_NONE);

    /* A line longer than the buffer is dropped, and the next one read. */
    pp_reader_reset(&r);
    for (i = 0; i < PP_LINE_MAX + 20; i++)
        CHECK(!pp_reader_push(&r, 'x'));
    CHECK(!pp_reader_push(&r, '\n'));
    for (i = 0; "PONG\n"[i]; i++)
        lines = pp_reader_push(&r, (fn_u8)"PONG\n"[i]);
    CHECK(lines == 1 && strcmp(r.line, "PONG") == 0);

    strcpy(name, "  Mo|chi! 2  ");
    pp_clean_name(name);
    CHECK(strcmp(name, "Mochi 2") == 0);
    CHECK(pp_message_cheers(0) && !pp_message_cheers(2) && !pp_message_cheers(8));
    CHECK(strcmp(pp_message(7), "Bye bye!") == 0);
}

int main(void)
{
    test_a_good_life();
    test_a_poor_life();
    test_neglect_starves();
    test_care_actions();
    test_bedtime();
    test_names();
    test_protocol();
    printf("fujipet_test: %s\n", failures ? "FAILED" : "ok");
    return failures != 0;
}
