/* Desktop test for common/holdem.c and apps/Holdem/cards.c: parses replies
 * saved from the live server (th.carr-designs.com, dev3, as palmtest), and
 * checks URL building, move labels, names, cards and the card art. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "holdem.h"
#include "cards.h"

static int failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

static fn_u8 buf[1024];

static fn_u16 Load(const char *path)
{
    FILE *f = fopen(path, "rb");
    size_t n;

    if (!f) {
        printf("FAIL: cannot open %s\n", path);
        failures++;
        return 0;
    }
    n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    return (fn_u16)n;
}

static void TestTurn(void)
{
    HsGame g;
    fn_u16 len = Load("data/holdem_state_turn.bin");

    CHECK(len == 165 + 4 * 33);
    CHECK(hs_parse_game(buf, len, &g));
    CHECK(g.round == 1);
    CHECK(g.pot == 15);
    CHECK(g.activePlayer == 0);
    CHECK(g.moveTime == 35);
    CHECK(g.viewing == 0);
    CHECK(g.community[0] == '\0');
    CHECK(g.moveCount == 5);
    CHECK(strcmp(g.moves[0].code, "fo") == 0 && strcmp(g.moves[0].name, "fold") == 0);
    CHECK(strcmp(g.moves[1].code, "ca") == 0 && strcmp(g.moves[1].name, "call 10") == 0);
    CHECK(strcmp(g.moves[3].code, "rh") == 0 && strcmp(g.moves[3].name, "raise 30") == 0);
    CHECK(strcmp(g.moves[4].code, "ai") == 0 && strcmp(g.moves[4].name, "all-in") == 0);
    CHECK(g.playerCount == 4);
    CHECK(strcmp(g.players[0].name, "palmtest") == 0);
    CHECK(g.players[0].purse == 1000);
    CHECK(strcmp(g.players[0].hand, "4sjd") == 0);
    CHECK(strcmp(g.players[1].hand, "????") == 0);
    CHECK(strcmp(g.players[3].name, "kirk bot") == 0);
    CHECK(g.players[3].bet == 10 && g.players[3].purse == 990);
    CHECK(strcmp(g.players[3].move, "post") == 0);

    /* A cut or padded reply is refused. */
    CHECK(!hs_parse_game(buf, len - 1, &g));
    CHECK(!hs_parse_game(buf, len + 1, &g));
    CHECK(!hs_parse_game(buf, 100, &g));
    buf[164] = 9;
    CHECK(!hs_parse_game(buf, 165 + 9 * 33, &g));
}

static void TestFlopAndShowdown(void)
{
    HsGame g;
    fn_u16 len = Load("data/holdem_state_flop.bin");

    CHECK(hs_parse_game(buf, len, &g));
    CHECK(g.round == 2);
    CHECK(strcmp(g.community, "4c7c8s") == 0);
    CHECK(hs_card_count(g.community) == 3);
    CHECK(g.activePlayer == 3);
    CHECK(g.moveCount == 0);
    CHECK(g.players[1].status == HS_FOLDED && strcmp(g.players[1].hand, "??") == 0);

    len = Load("data/holdem_state_showdown.bin");
    CHECK(hs_parse_game(buf, len, &g));
    CHECK(g.round == HS_ROUND_OVER);
    CHECK(g.activePlayer == -1);
    CHECK(hs_card_count(g.community) == 5);
    CHECK(strcmp(g.lastResult,
                 "kirk bot won with pair, eights, kickers queen, seven, five") == 0);
    CHECK(strcmp(g.players[3].hand, "8c5c") == 0);
    CHECK(g.players[3].purse == 1015);
}

static void TestTables(void)
{
    HsTables t;
    fn_u16 len = Load("data/holdem_tables.bin");

    CHECK(hs_parse_tables(buf, len, &t));
    CHECK(t.count == 5);
    CHECK(strcmp(t.tables[0].id, "ai6") == 0);
    CHECK(strcmp(t.tables[0].name, "ai room - 6 bots") == 0);
    CHECK(strcmp(t.tables[0].seats, "0 / 2") == 0);
    CHECK(strcmp(t.tables[4].id, "basement") == 0);
    CHECK(!hs_parse_tables(buf, len - 1, &t));
    CHECK(!hs_parse_tables(buf, 0, &t));
    buf[0] = 0;
    CHECK(hs_parse_tables(buf, 1, &t) && t.count == 0);
}

static void TestUrl(void)
{
    char url[160];

    CHECK(hs_build_url(url, sizeof(url), "https://th.carr-designs.com/", "state", "ai2", "Palm Pilot"));
    CHECK(strcmp(url, "N1:https://th.carr-designs.com/state?table=ai2&player=Palm+Pilot&bin=1&be=1") == 0);
    CHECK(hs_build_url(url, sizeof(url), "th.carr-designs.com", "tables", NULL, NULL));
    CHECK(strcmp(url, "N1:https://th.carr-designs.com/tables?bin=1&be=1") == 0);
    CHECK(hs_build_url(url, sizeof(url), "http://127.0.0.1:8080/", "move/ca", "dev3", "a&b"));
    CHECK(strcmp(url, "N1:http://127.0.0.1:8080/move/ca?table=dev3&player=a%26b&bin=1&be=1") == 0);
    CHECK(!hs_build_url(url, 20, "https://th.carr-designs.com/", "state", "ai2", "x"));
    CHECK(strlen(url) < 20);
}

static void TestSplitServer(void)
{
    char server[64], table[9];

    strcpy(server, "https://th.carr-designs.com/?table=basement");
    strcpy(table, "x");
    CHECK(hs_split_server(server, table, sizeof(table)));
    CHECK(strcmp(server, "https://th.carr-designs.com/") == 0 && strcmp(table, "basement") == 0);
    strcpy(server, "http://h:8080/?x=1&table=dev3&y=2");
    CHECK(hs_split_server(server, table, sizeof(table)));
    CHECK(strcmp(server, "http://h:8080/") == 0 && strcmp(table, "dev3") == 0);
    strcpy(server, "https://th.carr-designs.com/");
    strcpy(table, "ai2");
    CHECK(!hs_split_server(server, table, sizeof(table)) && strcmp(table, "ai2") == 0);
}

static void TestNamesAndMoves(void)
{
    char name[32], label[10], amount[10];

    strcpy(name, "  Ann  O'Neil!");
    CHECK(hs_clean_name(name) == 8 && strcmp(name, "Ann ONei") == 0);
    strcpy(name, "abcdefghijk");
    CHECK(hs_clean_name(name) == 8 && strcmp(name, "abcdefgh") == 0);
    strcpy(name, "!!!");
    CHECK(hs_clean_name(name) == 0);

    hs_split_move("raise 20", label, sizeof(label), amount, sizeof(amount));
    CHECK(strcmp(label, "Raise") == 0 && strcmp(amount, "20") == 0);
    hs_split_move("all-in", label, sizeof(label), amount, sizeof(amount));
    CHECK(strcmp(label, "All-in") == 0 && amount[0] == '\0');
    hs_split_move("check", label, sizeof(label), amount, sizeof(amount));
    CHECK(strcmp(label, "Check") == 0 && amount[0] == '\0');
    hs_split_move("", label, sizeof(label), amount, sizeof(amount));
    CHECK(label[0] == '\0' && amount[0] == '\0');

    CHECK(strcmp(hs_street(2), "Flop") == 0);
    CHECK(strcmp(hs_street(5), "Showdown") == 0);
    CHECK(strcmp(hs_street(9), "") == 0);
}

static void TestCards(void)
{
    fn_u8 rank, suit;
    fn_u16 rows[CARD_H], mini[MINI_H], i;

    CHECK(hs_card("ah", &rank, &suit) && rank == 12 && suit == 2);
    CHECK(hs_card("2c", &rank, &suit) && rank == 0 && suit == 0);
    CHECK(hs_card("TS", &rank, &suit) && rank == 8 && suit == 3);
    CHECK(hs_card("??", &rank, &suit) && rank == HS_UNKNOWN);
    CHECK(!hs_card("x", &rank, &suit));
    CHECK(!hs_card("zz", &rank, &suit));

    /* The Apple II card: edge rows, felt padding on the right. */
    card_table(12, 3, rows);
    CHECK(rows[0] == rows[CARD_H - 1]);
    for (i = 0; i < CARD_H; i++)
        CHECK((rows[i] & 3) == 3);
    /* The back differs from any face. */
    card_table(HS_UNKNOWN, HS_UNKNOWN, mini);
    CHECK(memcmp(rows, mini, sizeof(fn_u16) * 8) != 0);

    /* Mini cards stay within 10 px. */
    card_mini(12, 3, mini);
    for (i = 0; i < MINI_H; i++)
        CHECK((mini[i] & 0x003F) == 0);
    card_mini(HS_UNKNOWN, HS_UNKNOWN, mini);
    for (i = 0; i < MINI_H; i++)
        CHECK((mini[i] & 0x003F) == 0);
}

int main(void)
{
    TestTurn();
    TestFlopAndShowdown();
    TestTables();
    TestUrl();
    TestSplitServer();
    TestNamesAndMoves();
    TestCards();
    if (failures) {
        printf("holdem_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("holdem_test: all passed\n");
    return 0;
}
