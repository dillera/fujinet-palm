#include "petproto.h"

#define MAX_FIELDS 8

/* Same order as MESSAGES in tools/fujipet-server/fujipet_server.py. A list,
 * not a table of pointers (see pet.c). */
static const char messages[] =
    "Hi, friend!\0Want to play?\0I'm hungry!\0You're the best!\0"
    "Let's be pals!\0Yay! Hooray!\0Good night!\0Bye bye!";

void pp_reader_reset(PpReader *reader)
{
    reader->len = 0;
    reader->overflow = 0;
}

int pp_reader_push(PpReader *reader, fn_u8 byte)
{
    if (byte == '\r')
        return 0;
    if (byte == '\n') {
        int whole = !reader->overflow && reader->len > 0;
        reader->line[reader->len] = '\0';
        reader->len = 0;
        reader->overflow = 0;
        return whole;
    }
    if (reader->len >= PP_LINE_MAX) {
        reader->overflow = 1;
        return 0;
    }
    reader->line[reader->len++] = (char)byte;
    return 0;
}

fn_u16 pp_number(const char *text)
{
    fn_u16 value = 0;

    while (*text >= '0' && *text <= '9')
        value = (fn_u16)(value * 10 + (*text++ - '0'));
    return value;
}

static int same(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void copy(char *out, const char *in, fn_u16 size)
{
    fn_u16 i;

    for (i = 0; in[i] && i + 1 < size; i++)
        out[i] = in[i];
    out[i] = '\0';
}

void pp_clean_name(char *name)
{
    fn_u16 in, out = 0;
    char c;

    for (in = 0; name[in]; in++) {
        c = name[in];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '\'' || c == '.' || (c == ' ' && out > 0)) {
            if (out < PET_NAME_LEN - 1)
                name[out++] = c;
        }
    }
    while (out > 0 && name[out - 1] == ' ')
        out--;
    name[out] = '\0';
}

/* Splits at '|' in place; returns how many fields. */
static fn_u8 split(char *line, char *fields[], fn_u8 max)
{
    fn_u8 n = 0;

    fields[n++] = line;
    for (; *line; line++) {
        if (*line == '|') {
            *line = '\0';
            if (n < max)
                fields[n++] = line + 1;
        }
    }
    return n;
}

/* The fields of ON and PET from the id on: id, name, type, stage, age. */
static void read_pet(char *fields[], PpEvent *event)
{
    event->id = pp_number(fields[0]);
    copy(event->name, fields[1], sizeof(event->name));
    event->type = (fn_u8)pp_number(fields[2]);
    event->stage = (fn_u8)pp_number(fields[3]);
    event->age = (fn_u8)pp_number(fields[4]);
}

void pp_parse(char *line, PpEvent *event)
{
    char *f[MAX_FIELDS];
    fn_u8 n = split(line, f, MAX_FIELDS);

    fn_memset(event, 0, sizeof(*event));
    if (same(f[0], "REGOK") && n >= 3) {
        event->kind = PP_REGOK;
        event->id = pp_number(f[1]);
        copy(event->key, f[2], sizeof(event->key));
    } else if (same(f[0], "HI") && n >= 3) {
        event->kind = PP_HI;
        event->id = pp_number(f[1]);
        copy(event->name, f[2], sizeof(event->name));
    } else if (same(f[0], "PET") && n >= 7) {
        event->kind = PP_PET;
        read_pet(f + 1, event);
        event->online = (fn_u8)pp_number(f[6]);
    } else if (same(f[0], "END")) {
        event->kind = PP_END;
    } else if (same(f[0], "ON") && n >= 6) {
        event->kind = PP_ON;
        read_pet(f + 1, event);
        event->online = 1;
    } else if (same(f[0], "OFF") && n >= 2) {
        event->kind = PP_OFF;
        event->id = pp_number(f[1]);
    } else if (same(f[0], "MSG") && n >= 4) {
        event->kind = PP_MSG;
        event->id = pp_number(f[1]);
        copy(event->name, f[2], sizeof(event->name));
        event->code = (fn_u8)pp_number(f[3]);
    } else if (same(f[0], "SENT") && n >= 3) {
        event->kind = PP_SENT;
        event->id = pp_number(f[1]);
        event->code = (fn_u8)pp_number(f[2]);
    } else if (same(f[0], "ERR")) {
        event->kind = PP_ERR;
        copy(event->key, n >= 2 ? f[1] : "", sizeof(event->key));
        event->id = n >= 3 ? pp_number(f[2]) : 0;
    } else if (same(f[0], "PONG")) {
        event->kind = PP_PONG;
    } else if (same(f[0], "OK")) {
        event->kind = PP_OK;
    }
    if (event->code >= PP_MESSAGES)
        event->kind = PP_NONE;
}

PpFriend *pp_friend_find(PpFriends *friends, fn_u16 id)
{
    fn_u8 i;

    for (i = 0; i < friends->count; i++)
        if (friends->list[i].id == id)
            return &friends->list[i];
    return 0;
}

PpFriend *pp_friend_update(PpFriends *friends, fn_u16 id)
{
    PpFriend *f = pp_friend_find(friends, id);

    if (f)
        return f;
    if (friends->count >= PP_MAX_FRIENDS)
        return 0;
    f = &friends->list[friends->count++];
    fn_memset(f, 0, sizeof(*f));
    f->id = id;
    return f;
}

static int before(const PpFriend *a, const PpFriend *b)
{
    const char *x = a->name, *y = b->name;
    char cx, cy;

    if (a->online != b->online)
        return a->online > b->online;
    for (;; x++, y++) {
        cx = (*x >= 'A' && *x <= 'Z') ? (char)(*x - 'A' + 'a') : *x;
        cy = (*y >= 'A' && *y <= 'Z') ? (char)(*y - 'A' + 'a') : *y;
        if (cx != cy || cx == '\0')
            return cx < cy;
    }
}

void pp_friends_sort(PpFriends *friends)
{
    fn_u8 i, j;
    PpFriend t;

    for (i = 1; i < friends->count; i++)
        for (j = i; j > 0 && before(&friends->list[j], &friends->list[j - 1]); j--) {
            t = friends->list[j];
            friends->list[j] = friends->list[j - 1];
            friends->list[j - 1] = t;
        }
}

fn_u8 pp_friends_online(const PpFriends *friends)
{
    fn_u8 i, n = 0;

    for (i = 0; i < friends->count; i++)
        n += friends->list[i].online ? 1 : 0;
    return n;
}

const char *pp_message(fn_u8 code)
{
    const char *m = messages;

    if (code >= PP_MESSAGES)
        return "";
    while (code-- > 0)
        while (*m++)
            ;
    return m;
}

int pp_message_cheers(fn_u8 code)
{
    /* Not "I'm hungry!", "Good night!" or "Bye bye!". */
    return code != 2 && code != 6 && code != 7 && code < PP_MESSAGES;
}
