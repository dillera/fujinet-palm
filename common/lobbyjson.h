/* A streaming reader for the FujiNet Game Lobby's server list.
 *
 * The lobby (github.com/FujiNetWIFI/fujinet-lobby, server/) answers
 * GET /viewFull with a JSON array of game servers:
 *
 *   [ { "game": "...", "appkey": 1, "server": "...", "region": "us",
 *       "serverurl": "...", "status": "online", "maxplayers": 6,
 *       "curplayers": 0,
 *       "clients": [ { "platform": "atari", "url": "tnfs://..." }, ... ] },
 *     ... ]
 *
 * and, when it has none, a 404 with { "success": false, "message": "..." }.
 *
 * The reply is fed in whatever pieces arrive, so the whole body never has to
 * fit in memory. Strings are cut to the field sizes below; UTF-8 is turned
 * into Latin-1 where it can be, '?' otherwise. Portable C89, with no libc,
 * so it builds for the Palm and for the desktop test.
 */
#ifndef LOBBYJSON_H
#define LOBBYJSON_H

#include "fn_types.h"

#define LB_GAME_LEN      24
#define LB_SERVER_LEN    28
#define LB_REGION_LEN    8
#define LB_STATUS_LEN    8
#define LB_URL_LEN       64
#define LB_PLATFORMS_LEN 48
#define LB_MESSAGE_LEN   64
#define LB_KEY_LEN       12
#define LB_STR_LEN       64
#define LB_CLIENT_PLATFORM_LEN 16
#define LB_MAX_DEPTH     8

typedef struct {
    char game[LB_GAME_LEN];
    char server[LB_SERVER_LEN];
    char region[LB_REGION_LEN];
    char status[LB_STATUS_LEN];     /* "online" or "offline" */
    char url[LB_URL_LEN];           /* serverurl */
    char platforms[LB_PLATFORMS_LEN]; /* "atari,apple2,..."; ends in '+' if cut */
    fn_u16 appkey;
    fn_u16 curplayers;
    fn_u16 maxplayers;
    fn_u16 clientsAt;               /* "platform url\n" lines in the pool */
    fn_u16 clientsLen;
    fn_u8 clientCount;
    fn_u8 clientsCut;               /* some lines did not fit in the pool */
} LbServer;

typedef struct {
    /* Results. */
    LbServer *servers;
    fn_u16 capacity;
    fn_u16 count;                   /* servers kept */
    fn_u16 total;                   /* servers seen, kept or not */
    char *pool;
    fn_u16 poolSize;
    fn_u16 poolUsed;
    char message[LB_MESSAGE_LEN];   /* "message" of a top-level object */
    fn_u8 error;                    /* the reply was not JSON as expected */

    /* Reader state. */
    fn_u8 state;
    fn_u8 depth;
    fn_u8 isObject[LB_MAX_DEPTH];
    char keys[LB_MAX_DEPTH][LB_KEY_LEN];
    fn_u8 expectKey;
    fn_u8 started;
    char str[LB_STR_LEN];
    fn_u16 strLen;
    fn_u8 strIsKey;
    fn_u8 hexLeft;
    fn_u16 hexValue;
    fn_u8 utf8Lead;
    fn_u8 utf8Skip;
    fn_u32 number;
    fn_u8 negative;
    fn_u8 fraction;                 /* past the integer part */
    fn_u8 inServer;
    fn_u8 inClient;
    LbServer current;
    char clientPlatform[LB_CLIENT_PLATFORM_LEN];
    char clientUrl[LB_STR_LEN];
} LbParser;

/* servers holds up to capacity entries; pool (may be NULL) keeps each
 * server's client lines. */
void lb_init(LbParser *p, LbServer *servers, fn_u16 capacity, char *pool, fn_u16 poolSize);
void lb_feed(LbParser *p, const char *data, fn_u16 len);
/* After the last byte: nonzero if the reply was complete, well-formed JSON. */
int lb_finish(LbParser *p);

#endif /* LOBBYJSON_H */
