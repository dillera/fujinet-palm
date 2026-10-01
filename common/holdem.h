/* The FujiNet Texas Hold'em server's binary protocol.
 *
 * The server (github.com/dillera/servers, fujinet-game-system/texasholdem)
 * is plain HTTP(S), and runs every rule; a client polls the table and
 * sends back the two-letter move codes the server offers:
 *
 *   GET <server>tables?bin=1&be=1                      the public tables
 *   GET <server>state?table=T&player=P&bin=1&be=1      join (first call) and poll
 *   GET <server>move/XX?table=T&player=P&bin=1&be=1    play a move; replies the state
 *   GET <server>leave?table=T&player=P&bin=1&be=1      give up the seat
 *
 * bin=1 asks for the packed structs the 8-bit clients read (src/misc.h in
 * github.com/dillera/fujinet-texasHoldEm); be=1 makes its UInt16s big-endian,
 * as for the CoCo. Strings are fixed-size, NUL-padded and lowercased. The
 * replies are parsed field by field, since a 68000 cannot read a UInt16 at
 * the odd offsets the packed layout puts them at.
 *
 * Portable C89, with no libc, for the Palm and the desktop test.
 */
#ifndef HOLDEM_H
#define HOLDEM_H

#include "fn_types.h"

#define HS_RESULT_LEN    81
#define HS_COMMUNITY_LEN 11
#define HS_CODE_LEN      3
#define HS_MOVE_NAME_LEN 10
#define HS_NAME_LEN      9
#define HS_LAST_MOVE_LEN 8
#define HS_HAND_LEN      11
#define HS_TABLE_ID_LEN  9
#define HS_TABLE_NAME_LEN 21
#define HS_TABLE_SEATS_LEN 6

#define HS_MAX_MOVES   5
#define HS_MAX_PLAYERS 8
#define HS_MAX_TABLES  10

/* Wire sizes. A state reply is the header and playerCount players; the
 * table list is a count byte and count tables. */
#define HS_PLAYER_SIZE 33
#define HS_STATE_HEADER 165
#define HS_STATE_MAX (HS_STATE_HEADER + HS_MAX_PLAYERS * HS_PLAYER_SIZE) /* 429 */
#define HS_TABLE_SIZE 36
#define HS_TABLES_MAX (1 + HS_MAX_TABLES * HS_TABLE_SIZE)                /* 361 */

/* Player status. All-in players are sent as playing. */
#define HS_WAITING 0
#define HS_PLAYING 1
#define HS_FOLDED  2
#define HS_LEFT    3

/* round: 0 waiting for players, 1 pre-flop, 2 flop, 3 turn, 4 river,
 * 5 hand over (showdown, or won by folds). */
#define HS_ROUND_OVER 5

typedef struct {
    char code[HS_CODE_LEN];         /* "fo", "ch", "ca", "bl", "bh", "rl", "rh", "ai" */
    char name[HS_MOVE_NAME_LEN];    /* "fold", "call 10", "raise 20", "all-in" */
} HsMove;

typedef struct {
    char name[HS_NAME_LEN];
    fn_u8 status;
    fn_u16 bet;                     /* in front of them this street */
    char move[HS_LAST_MOVE_LEN];    /* last action: "check", "post", "raise"... */
    fn_u16 purse;
    char hand[HS_HAND_LEN];         /* "kcah"; "????" hidden, "??" folded */
} HsPlayer;

typedef struct {
    char lastResult[HS_RESULT_LEN];
    fn_u8 round;
    fn_u16 pot;
    signed char activePlayer;       /* 0 is this client; -1 nobody */
    fn_u8 moveTime;                 /* seconds left for this client's move */
    fn_u8 viewing;                  /* 1: watching, not seated (table full) */
    char community[HS_COMMUNITY_LEN];
    fn_u8 moveCount;
    HsMove moves[HS_MAX_MOVES];
    fn_u8 playerCount;
    HsPlayer players[HS_MAX_PLAYERS]; /* rotated so this client is 0 */
} HsGame;

typedef struct {
    char id[HS_TABLE_ID_LEN];       /* the table= value */
    char name[HS_TABLE_NAME_LEN];
    char seats[HS_TABLE_SEATS_LEN]; /* "1 / 4": humans seated / human seats */
} HsTable;

typedef struct {
    fn_u8 count;
    HsTable tables[HS_MAX_TABLES];
} HsTables;

/* Each returns nonzero if buf is a whole, sane reply. */
int hs_parse_game(const fn_u8 *buf, fn_u16 len, HsGame *game);
int hs_parse_tables(const fn_u8 *buf, fn_u16 len, HsTables *tables);

/* A card from a two-letter code ("ah", "tc", "??"): rank 0..12 for 2..A
 * or HS_UNKNOWN, suit 0..3 for c, d, h, s or HS_UNKNOWN. Returns 0 if the
 * code is not a card at all. */
#define HS_UNKNOWN 13
int hs_card(const char *code, fn_u8 *rank, fn_u8 *suit);
/* How many cards a hand or board string holds. */
fn_u8 hs_card_count(const char *cards);

/* Builds "N1:<server><path>?table=..&player=..&bin=1&be=1" into out.
 * server gets https:// if it names no scheme, and a '/' before path if it
 * lacks one; table and player may be NULL (as for "tables"). Returns 0 if
 * it did not fit. */
int hs_build_url(char *out, fn_u16 size, const char *server, const char *path,
                 const char *table, const char *player);

/* Splits a lobby-style address, "https://host/?table=ai2", into the server
 * ("https://host/") and table ("ai2", cut to tableSize - 1). Returns nonzero
 * if server held a table; else leaves both alone. */
int hs_split_server(char *server, char *table, fn_u16 tableSize);

/* Keeps letters, digits and single spaces, at most HS_NAME_LEN - 1 of them,
 * as the 8-bit clients' name entry does. Returns the new length. */
fn_u16 hs_clean_name(char *name);

/* Splits a move name for a button: "raise 20" gives "Raise" and "20";
 * "all-in" gives "All-in" and "". */
void hs_split_move(const char *name, char *label, fn_u16 labelSize,
                   char *amount, fn_u16 amountSize);

/* "Waiting", "Pre-flop", "Flop", "Turn", "River", "Showdown". */
const char *hs_street(fn_u8 round);

#endif /* HOLDEM_H */
