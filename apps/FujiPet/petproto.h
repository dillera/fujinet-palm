/* The FujiPet friend server's line protocol, as the Palm reads it: portable
 * C, tested by tests/fujipet_test.c. The protocol itself is described in
 * tools/fujipet-server/fujipet_server.py.
 */
#ifndef PETPROTO_H
#define PETPROTO_H

#include "fn_types.h"
#include "pet.h"

#define PP_LINE_MAX 160
#define PP_KEY_LEN 9 /* 8 hex digits and a NUL */
#define PP_MAX_FRIENDS 16
#define PP_MESSAGES 8

/* What a line from the server says (PpEvent.kind). */
enum {
    PP_NONE, PP_REGOK, PP_HI, PP_PET, PP_END, PP_ON, PP_OFF, PP_MSG, PP_SENT,
    PP_ERR, PP_PONG, PP_OK
};

typedef struct {
    fn_u8 kind;
    fn_u16 id;
    fn_u8 type, stage, age, online, code;
    char name[PET_NAME_LEN];
    char key[PP_KEY_LEN];   /* REGOK's key; ERR's reason */
} PpEvent;

/* Gathers stream bytes into lines; a line too long is dropped whole. */
typedef struct {
    char line[PP_LINE_MAX + 1];
    fn_u16 len;
    fn_u8 overflow;
} PpReader;

typedef struct {
    fn_u16 id;
    char name[PET_NAME_LEN];
    fn_u8 type, stage, age, online;
} PpFriend;

typedef struct {
    PpFriend list[PP_MAX_FRIENDS];
    fn_u8 count;
} PpFriends;

void pp_reader_reset(PpReader *reader);
/* Adds a byte; returns 1 when reader->line holds a whole line. */
int pp_reader_push(PpReader *reader, fn_u8 byte);

/* Reads one line (changed in place); kind is PP_NONE if it means nothing. */
void pp_parse(char *line, PpEvent *event);

fn_u16 pp_number(const char *text);
/* Keeps letters, digits, spaces and - ' . ; at most PET_NAME_LEN - 1. */
void pp_clean_name(char *name);

PpFriend *pp_friend_find(PpFriends *friends, fn_u16 id);
/* The friend with this id, added if new; NULL when the list is full. */
PpFriend *pp_friend_update(PpFriends *friends, fn_u16 id);
/* Online friends first, then by name. */
void pp_friends_sort(PpFriends *friends);
fn_u8 pp_friends_online(const PpFriends *friends);

/* The canned messages, by code; the server has the same list. */
const char *pp_message(fn_u8 code);
/* Whether a message is a kind one that cheers the pet up. */
int pp_message_cheers(fn_u8 code);

#endif /* PETPROTO_H */
