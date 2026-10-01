/* Desktop test for common/lobbyjson.c: feeds it a saved /viewFull reply from
 * lobby.fujinet.online in pieces of several sizes, and some edge cases. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lobbyjson.h"

#define FIXTURE "data/lobby_viewFull.json"
#define CAPACITY 48
#define POOL 8192

static int failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

static LbServer servers[CAPACITY];
static char pool[POOL];

static void Feed(LbParser *p, const char *data, size_t len, size_t piece)
{
    size_t at, n;

    for (at = 0; at < len; at += n) {
        n = len - at < piece ? len - at : piece;
        lb_feed(p, data + at, (fn_u16)n);
    }
}

static char *Slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long size;

    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)size + 1);
    *len = fread(buf, 1, (size_t)size, f);
    fclose(f);
    return buf;
}

static void TestFixture(const char *json, size_t len, size_t piece)
{
    LbParser p;
    LbServer *s;

    lb_init(&p, servers, CAPACITY, pool, POOL);
    Feed(&p, json, len, piece);
    CHECK(lb_finish(&p));
    CHECK(p.count == 32);
    CHECK(p.total == 32);
    CHECK(p.message[0] == '\0');

    s = &servers[0];
    CHECK(strcmp(s->game, "Reversi") == 0);
    CHECK(strcmp(s->server, "Main Room") == 0);
    CHECK(strcmp(s->region, "us") == 0);
    CHECK(strcmp(s->status, "online") == 0);
    CHECK(strcmp(s->url, "TCP://apps.irata.online:1025/") == 0);
    CHECK(s->appkey == 3 && s->curplayers == 2 && s->maxplayers == 2);
    CHECK(strcmp(s->platforms, "atari") == 0);
    CHECK(s->clientCount == 1 && !s->clientsCut);
    CHECK(s->clientsLen == strlen("atari TNFS://apps.irata.online/Atari_8-bit/Games/Reversi.atr\n"));
    CHECK(memcmp(pool + s->clientsAt, "atari TNFS://apps.irata.online/Atari_8-bit/Games/Reversi.atr\n",
                 s->clientsLen) == 0);

    s = &servers[1];
    CHECK(strcmp(s->game, "5 Card Stud") == 0);
    CHECK(strcmp(s->server, "AI Room - 2 bots") == 0);
    CHECK(s->appkey == 1 && s->curplayers == 0 && s->maxplayers == 6);
    CHECK(strcmp(s->platforms, "atari,apple2,adam,msdos,coco,ti99,intv") == 0);
    CHECK(s->clientCount == 7);

    s = &servers[27];
    CHECK(strcmp(s->game, "Texas Hold'em") == 0);
}

/* Fewer slots and a small pool: extra servers are counted but not kept, and
 * client lines that do not fit are marked as cut. */
static void TestLimits(const char *json, size_t len)
{
    LbParser p;

    lb_init(&p, servers, 4, pool, 120);
    Feed(&p, json, len, 97);
    CHECK(lb_finish(&p));
    CHECK(p.count == 4);
    CHECK(p.total == 32);
    CHECK(p.poolUsed <= 120);
    CHECK(servers[0].clientsLen > 0 && !servers[0].clientsCut);
    CHECK(servers[1].clientsCut);
    CHECK(strcmp(servers[1].platforms, "atari,apple2,adam,msdos,coco,ti99,intv") == 0);

    lb_init(&p, servers, 4, NULL, 0);
    Feed(&p, json, len, 256);
    CHECK(lb_finish(&p));
    CHECK(p.count == 4 && servers[0].clientsLen == 0 && servers[0].clientsCut);
}

static void TestString(const char *json, int ok)
{
    LbParser p;

    lb_init(&p, servers, CAPACITY, pool, POOL);
    Feed(&p, json, strlen(json), 1);
    CHECK(lb_finish(&p) == ok);
}

static void TestEdges(void)
{
    LbParser p;
    const char *notFound = "{\"success\":false,\"message\":\"No servers available\"}";
    const char *escapes =
        "[{\"game\":\"Caf\\u00e9 \\\"Q\\\"\",\"server\":\"Na\xc3\xafve \xe2\x98\x85 x\","
        "\"maxplayers\":4.0,\"curplayers\":-1,\"appkey\":70000,\"status\":\"offline\","
        "\"extra\":{\"game\":\"nested\",\"list\":[1,true,null]},"
        "\"clients\":[{\"url\":\"u1\",\"platform\":\"p1\"},{\"platform\":\"p2\"}]}]";
    const char *longPlatforms =
        "[{\"clients\":[{\"platform\":\"aaaaaaaaaaaaaa\"},{\"platform\":\"bbbbbbbbbbbbbb\"},"
        "{\"platform\":\"cccccccccccccc\"},{\"platform\":\"dddddddddddddd\"}]}]";

    lb_init(&p, servers, CAPACITY, pool, POOL);
    Feed(&p, notFound, strlen(notFound), 3);
    CHECK(lb_finish(&p));
    CHECK(p.count == 0);
    CHECK(strcmp(p.message, "No servers available") == 0);

    lb_init(&p, servers, CAPACITY, pool, POOL);
    Feed(&p, escapes, strlen(escapes), 1);
    CHECK(lb_finish(&p));
    CHECK(p.count == 1);
    CHECK(strcmp(servers[0].game, "Caf\xe9 \"Q\"") == 0);
    CHECK(strcmp(servers[0].server, "Na\xefve ? x") == 0);
    CHECK(servers[0].maxplayers == 4 && servers[0].curplayers == 0 &&
          servers[0].appkey == 0xFFFF);
    CHECK(strcmp(servers[0].status, "offline") == 0);
    CHECK(strcmp(servers[0].platforms, "p1,p2") == 0);
    CHECK(servers[0].clientCount == 2);
    CHECK(memcmp(pool + servers[0].clientsAt, "p1 u1\np2 \n", servers[0].clientsLen) == 0);

    lb_init(&p, servers, CAPACITY, pool, POOL);
    Feed(&p, longPlatforms, strlen(longPlatforms), 5);
    CHECK(lb_finish(&p));
    CHECK(strlen(servers[0].platforms) < LB_PLATFORMS_LEN);
    CHECK(servers[0].platforms[strlen(servers[0].platforms) - 1] == '+');

    TestString("[]", 1);
    TestString("  [ ]  \r\n", 1);
    TestString("", 0);
    TestString("[{\"game\":\"x\"}", 0);              /* cut short */
    TestString("[{\"game\":\"x\"]]", 0);             /* mismatched */
    TestString("<html>502 Bad Gateway</html>", 0);   /* not JSON */
    TestString("[] x", 0);                           /* trailing junk */
}

int main(void)
{
    size_t len, pieces[] = { 1, 2, 7, 64, 255, 256, 100000 };
    unsigned i;
    char *json = Slurp(FIXTURE, &len);

    if (!json) {
        printf("cannot read %s\n", FIXTURE);
        return 1;
    }
    for (i = 0; i < sizeof(pieces) / sizeof(pieces[0]); i++)
        TestFixture(json, len, pieces[i]);
    TestLimits(json, len);
    TestEdges();
    free(json);
    printf("lobbyjson_test: %s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
