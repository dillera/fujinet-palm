#include "holdem.h"

/* Wire offsets in a state reply (src/misc.h Game in fujinet-texasHoldEm). */
#define OFF_RESULT      0
#define OFF_ROUND       81
#define OFF_POT         82
#define OFF_ACTIVE      84
#define OFF_MOVE_TIME   85
#define OFF_VIEWING     86
#define OFF_COMMUNITY   87
#define OFF_MOVE_COUNT  98
#define OFF_MOVES       99
#define MOVE_SIZE       13
#define OFF_PLAYER_COUNT 164
#define OFF_PLAYERS     165

/* Within a player. */
#define P_NAME   0
#define P_STATUS 9
#define P_BET    10
#define P_MOVE   12
#define P_PURSE  20
#define P_HAND   22

static void CopyField(char *dst, const fn_u8 *src, fn_u16 size)
{
    fn_u16 i;

    for (i = 0; i + 1 < size && src[i] != 0; i++)
        dst[i] = (char)src[i];
    dst[i] = '\0';
}

static fn_u16 Get16(const fn_u8 *p)
{
    return (fn_u16)(((fn_u16)p[0] << 8) | p[1]);
}

static fn_u16 Len(const char *s)
{
    fn_u16 n = 0;

    while (s[n])
        n++;
    return n;
}

int hs_parse_game(const fn_u8 *buf, fn_u16 len, HsGame *game)
{
    fn_u16 i;
    const fn_u8 *p;

    if (len < HS_STATE_HEADER)
        return 0;
    game->playerCount = buf[OFF_PLAYER_COUNT];
    game->moveCount = buf[OFF_MOVE_COUNT];
    game->round = buf[OFF_ROUND];
    if (game->playerCount > HS_MAX_PLAYERS || game->moveCount > HS_MAX_MOVES ||
        game->round > HS_ROUND_OVER ||
        len != HS_STATE_HEADER + (fn_u16)game->playerCount * HS_PLAYER_SIZE)
        return 0;

    CopyField(game->lastResult, buf + OFF_RESULT, HS_RESULT_LEN);
    game->pot = Get16(buf + OFF_POT);
    game->activePlayer = (signed char)buf[OFF_ACTIVE];
    game->moveTime = buf[OFF_MOVE_TIME];
    game->viewing = buf[OFF_VIEWING];
    CopyField(game->community, buf + OFF_COMMUNITY, HS_COMMUNITY_LEN);
    for (i = 0; i < HS_MAX_MOVES; i++) {
        p = buf + OFF_MOVES + i * MOVE_SIZE;
        CopyField(game->moves[i].code, p, HS_CODE_LEN);
        CopyField(game->moves[i].name, p + HS_CODE_LEN, HS_MOVE_NAME_LEN);
    }
    for (i = 0; i < game->playerCount; i++) {
        HsPlayer *pl = &game->players[i];

        p = buf + OFF_PLAYERS + i * HS_PLAYER_SIZE;
        CopyField(pl->name, p + P_NAME, HS_NAME_LEN);
        pl->status = p[P_STATUS];
        pl->bet = Get16(p + P_BET);
        CopyField(pl->move, p + P_MOVE, HS_LAST_MOVE_LEN);
        pl->purse = Get16(p + P_PURSE);
        CopyField(pl->hand, p + P_HAND, HS_HAND_LEN);
    }
    if (game->activePlayer >= (signed char)game->playerCount)
        game->activePlayer = -1;
    return 1;
}

int hs_parse_tables(const fn_u8 *buf, fn_u16 len, HsTables *tables)
{
    fn_u16 i;
    const fn_u8 *p;

    if (len < 1 || buf[0] > HS_MAX_TABLES || len != 1 + (fn_u16)buf[0] * HS_TABLE_SIZE)
        return 0;
    tables->count = buf[0];
    for (i = 0; i < tables->count; i++) {
        p = buf + 1 + i * HS_TABLE_SIZE;
        CopyField(tables->tables[i].id, p, HS_TABLE_ID_LEN);
        CopyField(tables->tables[i].name, p + HS_TABLE_ID_LEN, HS_TABLE_NAME_LEN);
        CopyField(tables->tables[i].seats, p + HS_TABLE_ID_LEN + HS_TABLE_NAME_LEN,
                  HS_TABLE_SEATS_LEN);
    }
    return 1;
}

int hs_card(const char *code, fn_u8 *rank, fn_u8 *suit)
{
    static const char ranks[] = "23456789tjqka";
    static const char suits[] = "cdhs";
    char r, s;
    fn_u8 i;

    if (code[0] == '\0' || code[1] == '\0')
        return 0;
    r = code[0];
    s = code[1];
    if (r >= 'A' && r <= 'Z')
        r = (char)(r - 'A' + 'a');
    if (s >= 'A' && s <= 'Z')
        s = (char)(s - 'A' + 'a');
    *rank = HS_UNKNOWN;
    *suit = HS_UNKNOWN;
    for (i = 0; i < 13; i++)
        if (ranks[i] == r)
            *rank = i;
    for (i = 0; i < 4; i++)
        if (suits[i] == s)
            *suit = i;
    if (r == '?')
        return 1;
    return *rank != HS_UNKNOWN && *suit != HS_UNKNOWN;
}

fn_u8 hs_card_count(const char *cards)
{
    return (fn_u8)(Len(cards) / 2);
}

/* Appends to out at *at, keeping room for the NUL; 0 if it did not fit. */
static int Put(char *out, fn_u16 size, fn_u16 *at, const char *text)
{
    while (*text) {
        if (*at + 1 >= size) {
            out[*at] = '\0';
            return 0;
        }
        out[(*at)++] = *text++;
    }
    out[*at] = '\0';
    return 1;
}

/* A query value: letters, digits and -._~ as they are, space as '+'. */
static int PutValue(char *out, fn_u16 size, fn_u16 *at, const char *text)
{
    static const char hex[] = "0123456789ABCDEF";
    char esc[4];
    unsigned char c;

    for (; *text; text++) {
        c = (unsigned char)*text;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '.' || c == '_' || c == '~') {
            esc[0] = (char)c;
            esc[1] = '\0';
        } else if (c == ' ') {
            esc[0] = '+';
            esc[1] = '\0';
        } else {
            esc[0] = '%';
            esc[1] = hex[c >> 4];
            esc[2] = hex[c & 15];
            esc[3] = '\0';
        }
        if (!Put(out, size, at, esc))
            return 0;
    }
    return 1;
}

static int HasScheme(const char *s)
{
    for (; *s && *s != '/' && *s != '?'; s++)
        if (s[0] == ':' && s[1] == '/' && s[2] == '/')
            return 1;
    return 0;
}

int hs_build_url(char *out, fn_u16 size, const char *server, const char *path,
                 const char *table, const char *player)
{
    fn_u16 at = 0, n;

    if (size == 0)
        return 0;
    out[0] = '\0';
    if (!Put(out, size, &at, "N1:"))
        return 0;
    if (!HasScheme(server) && !Put(out, size, &at, "https://"))
        return 0;
    if (!Put(out, size, &at, server))
        return 0;
    n = Len(server);
    if ((n == 0 || server[n - 1] != '/') && !Put(out, size, &at, "/"))
        return 0;
    if (!Put(out, size, &at, path) || !Put(out, size, &at, "?"))
        return 0;
    if (table != 0 && table[0]) {
        if (!Put(out, size, &at, "table=") || !PutValue(out, size, &at, table) ||
            !Put(out, size, &at, "&"))
            return 0;
    }
    if (player != 0 && player[0]) {
        if (!Put(out, size, &at, "player=") || !PutValue(out, size, &at, player) ||
            !Put(out, size, &at, "&"))
            return 0;
    }
    return Put(out, size, &at, "bin=1&be=1");
}

int hs_split_server(char *server, char *table, fn_u16 tableSize)
{
    static const char key[] = "table=";
    fn_u16 i, k, at;

    for (i = 0; server[i] && server[i] != '?'; i++)
        ;
    if (server[i] != '?')
        return 0;
    for (k = i + 1; server[k]; k++) {
        if ((k == i + 1 || server[k - 1] == '&') && server[k] == 't') {
            fn_u16 j = 0;
            while (key[j] && server[k + j] == key[j])
                j++;
            if (key[j] == '\0') {
                k += j;
                for (at = 0; server[k] && server[k] != '&' && at + 1 < tableSize; k++)
                    table[at++] = server[k];
                if (tableSize > 0)
                    table[at] = '\0';
                server[i] = '\0';
                return 1;
            }
        }
    }
    return 0;
}

fn_u16 hs_clean_name(char *name)
{
    fn_u16 in, out = 0;
    char c;

    for (in = 0; name[in] && out < HS_NAME_LEN - 1; in++) {
        c = name[in];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
            name[out++] = c;
        else if (c == ' ' && out > 0 && name[out - 1] != ' ')
            name[out++] = ' ';
    }
    while (out > 0 && name[out - 1] == ' ')
        out--;
    name[out] = '\0';
    return out;
}

void hs_split_move(const char *name, char *label, fn_u16 labelSize,
                   char *amount, fn_u16 amountSize)
{
    fn_u16 i, space = 0, n = Len(name), at;

    for (i = 0; i < n; i++)
        if (name[i] == ' ')
            space = i;
    if (space == 0 || name[space + 1] < '0' || name[space + 1] > '9')
        space = n;
    for (at = 0; at < space && at + 1 < labelSize; at++)
        label[at] = name[at];
    if (labelSize > 0) {
        label[at] = '\0';
        if (label[0] >= 'a' && label[0] <= 'z')
            label[0] = (char)(label[0] - 'a' + 'A');
    }
    at = 0;
    if (space < n)
        for (i = space + 1; i < n && at + 1 < amountSize; i++)
            amount[at++] = name[i];
    if (amountSize > 0)
        amount[at] = '\0';
}

const char *hs_street(fn_u8 round)
{
    static const char *const names[] = {
        "Waiting", "Pre-flop", "Flop", "Turn", "River", "Showdown"
    };

    return round <= HS_ROUND_OVER ? names[round] : "";
}
