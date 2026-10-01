#include "lobbyjson.h"

enum { ST_VALUE, ST_STRING, ST_ESCAPE, ST_HEX, ST_NUMBER, ST_LITERAL, ST_DONE };

/* Depths: the top array is 1, a server object 2, its "clients" array 3 and
 * each client object 4. */
#define SERVER_DEPTH 2
#define CLIENT_DEPTH 4

static void Zero(void *dst, fn_u16 len)
{
    char *d = (char *)dst;
    while (len--)
        *d++ = 0;
}

static int Equal(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static fn_u16 Length(const char *s)
{
    fn_u16 n = 0;
    while (s[n])
        n++;
    return n;
}

/* Copies src into dst of the given size, cutting it to fit. */
static void Copy(char *dst, const char *src, fn_u16 size)
{
    fn_u16 i;
    for (i = 0; i + 1 < size && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

void lb_init(LbParser *p, LbServer *servers, fn_u16 capacity, char *pool, fn_u16 poolSize)
{
    Zero(p, sizeof(*p));
    p->servers = servers;
    p->capacity = capacity;
    p->pool = pool;
    p->poolSize = pool ? poolSize : 0;
    p->state = ST_VALUE;
}

static void AddChar(LbParser *p, char c)
{
    if (p->strLen + 1 < LB_STR_LEN)
        p->str[p->strLen++] = c;
}

/* A code point from \u or UTF-8, as Latin-1 when it has one. */
static void AddCode(LbParser *p, fn_u16 code)
{
    if (code < 0x20)
        AddChar(p, ' ');
    else if (code < 0x80 || (code >= 0xA0 && code <= 0xFF))
        AddChar(p, (char)code);
    else
        AddChar(p, '?');
}

static void Append(char *dst, fn_u16 size, const char *src)
{
    fn_u16 at = Length(dst), n = Length(src);
    fn_u16 need = n + (at ? 1 : 0);

    if (at + need + 1 <= size) {
        if (at)
            dst[at++] = ',';
        Copy(dst + at, src, size - at);
    } else if (at > 0 && dst[at - 1] != '+') {
        if (at + 2 > size)
            at = size - 2;
        dst[at] = '+';
        dst[at + 1] = '\0';
    }
}

static void BeginServer(LbParser *p)
{
    Zero(&p->current, sizeof(p->current));
    p->current.clientsAt = p->poolUsed;
    p->inServer = 1;
}

static void EndServer(LbParser *p)
{
    p->inServer = 0;
    p->total++;
    if (p->count < p->capacity) {
        p->current.clientsLen = p->poolUsed - p->current.clientsAt;
        p->servers[p->count++] = p->current;
    } else {
        p->poolUsed = p->current.clientsAt; /* drop its client lines too */
    }
}

static void BeginClient(LbParser *p)
{
    p->clientPlatform[0] = '\0';
    p->clientUrl[0] = '\0';
    p->inClient = 1;
}

/* Keeps "platform url\n" in the pool and the platform in the list. */
static void EndClient(LbParser *p)
{
    fn_u16 plen = Length(p->clientPlatform), ulen = Length(p->clientUrl);
    fn_u16 need = plen + 1 + ulen + 1, i;
    char *out;

    p->inClient = 0;
    p->current.clientCount++;
    if (plen)
        Append(p->current.platforms, LB_PLATFORMS_LEN, p->clientPlatform);
    if (p->poolUsed + need > p->poolSize) {
        p->current.clientsCut = 1;
        return;
    }
    out = p->pool + p->poolUsed;
    for (i = 0; i < plen; i++)
        *out++ = p->clientPlatform[i];
    *out++ = ' ';
    for (i = 0; i < ulen; i++)
        *out++ = p->clientUrl[i];
    *out++ = '\n';
    p->poolUsed += need;
}

static const char *CurrentKey(LbParser *p)
{
    return p->depth > 0 && p->isObject[p->depth - 1] ? p->keys[p->depth - 1] : "";
}

static void StringValue(LbParser *p)
{
    const char *key = CurrentKey(p);
    LbServer *s = &p->current;

    if (p->depth == SERVER_DEPTH && p->inServer) {
        if (Equal(key, "game"))
            Copy(s->game, p->str, LB_GAME_LEN);
        else if (Equal(key, "server"))
            Copy(s->server, p->str, LB_SERVER_LEN);
        else if (Equal(key, "region"))
            Copy(s->region, p->str, LB_REGION_LEN);
        else if (Equal(key, "status"))
            Copy(s->status, p->str, LB_STATUS_LEN);
        else if (Equal(key, "serverurl"))
            Copy(s->url, p->str, LB_URL_LEN);
    } else if (p->depth == CLIENT_DEPTH && p->inClient) {
        if (Equal(key, "platform"))
            Copy(p->clientPlatform, p->str, LB_CLIENT_PLATFORM_LEN);
        else if (Equal(key, "url"))
            Copy(p->clientUrl, p->str, LB_STR_LEN);
    } else if (p->depth == 1 && p->isObject[0] && Equal(key, "message")) {
        Copy(p->message, p->str, LB_MESSAGE_LEN);
    }
}

static void NumberValue(LbParser *p)
{
    const char *key = CurrentKey(p);
    fn_u16 n = p->negative ? 0 : (p->number > 0xFFFF ? 0xFFFF : (fn_u16)p->number);

    if (p->depth != SERVER_DEPTH || !p->inServer)
        return;
    if (Equal(key, "appkey"))
        p->current.appkey = n;
    else if (Equal(key, "curplayers"))
        p->current.curplayers = n;
    else if (Equal(key, "maxplayers"))
        p->current.maxplayers = n;
}

static void EndString(LbParser *p)
{
    p->str[p->strLen] = '\0';
    if (p->strIsKey)
        Copy(p->keys[p->depth - 1], p->str, LB_KEY_LEN);
    else
        StringValue(p);
    p->state = ST_VALUE;
}

static void Open(LbParser *p, int object)
{
    if (p->depth >= LB_MAX_DEPTH) {
        p->error = 1;
        return;
    }
    p->isObject[p->depth] = (fn_u8)object;
    p->keys[p->depth][0] = '\0';
    p->depth++;
    p->started = 1;
    p->expectKey = (fn_u8)object;
    if (!object)
        return;
    if (p->depth == SERVER_DEPTH && !p->isObject[0])
        BeginServer(p);
    else if (p->depth == CLIENT_DEPTH && p->inServer && !p->isObject[2] &&
             Equal(p->keys[1], "clients"))
        BeginClient(p);
}

static void Close(LbParser *p, int object)
{
    if (p->depth == 0 || p->isObject[p->depth - 1] != (fn_u8)object) {
        p->error = 1;
        return;
    }
    if (object && p->depth == CLIENT_DEPTH && p->inClient)
        EndClient(p);
    else if (object && p->depth == SERVER_DEPTH && p->inServer)
        EndServer(p);
    p->depth--;
    p->expectKey = 0;
    if (p->depth == 0)
        p->state = ST_DONE;
}

static int IsBlank(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static void Value(LbParser *p, char c)
{
    if (IsBlank(c))
        return;
    switch (c) {
    case '{': Open(p, 1); break;
    case '[': Open(p, 0); break;
    case '}': Close(p, 1); break;
    case ']': Close(p, 0); break;
    case ',': p->expectKey = p->depth > 0 && p->isObject[p->depth - 1]; break;
    case ':': p->expectKey = 0; break;
    case '"':
        if (p->depth == 0) {
            p->error = 1;
            break;
        }
        p->strIsKey = p->expectKey;
        p->strLen = 0;
        p->utf8Skip = 0;
        p->utf8Lead = 0;
        p->state = ST_STRING;
        break;
    default:
        if (p->depth == 0) {
            p->error = 1;
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            p->negative = c == '-';
            p->fraction = 0;
            p->number = c == '-' ? 0 : (fn_u32)(c - '0');
            p->state = ST_NUMBER;
        } else if (c >= 'a' && c <= 'z') {
            p->state = ST_LITERAL; /* true, false, null: nothing kept uses them */
        } else {
            p->error = 1;
        }
        break;
    }
}

static void StringChar(LbParser *p, unsigned char c)
{
    if (c == '"') {
        EndString(p);
    } else if (c == '\\') {
        p->state = ST_ESCAPE;
    } else if (c < 0x80) {
        p->utf8Skip = 0;
        AddChar(p, (char)(c < 0x20 ? ' ' : c));
    } else if (c >= 0xC0) {
        /* A lead byte: C2 and C3 start Latin-1 characters, others do not. */
        p->utf8Lead = c;
        p->utf8Skip = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
        if (c != 0xC2 && c != 0xC3)
            AddChar(p, '?');
    } else if (p->utf8Skip) {
        if (p->utf8Lead == 0xC2 || p->utf8Lead == 0xC3)
            AddCode(p, (fn_u16)(((p->utf8Lead & 0x1F) << 6) | (c & 0x3F)));
        p->utf8Lead = 0;
        p->utf8Skip--;
    } else {
        AddChar(p, '?'); /* a stray continuation byte */
    }
}

static void EscapeChar(LbParser *p, char c)
{
    p->state = ST_STRING;
    switch (c) {
    case 'n': case 't': case 'r': case 'b': case 'f': AddChar(p, ' '); break;
    case 'u':
        p->hexLeft = 4;
        p->hexValue = 0;
        p->state = ST_HEX;
        break;
    default: AddChar(p, c); break; /* \" \\ \/ */
    }
}

static void HexChar(LbParser *p, char c)
{
    fn_u16 digit;

    if (c >= '0' && c <= '9')
        digit = c - '0';
    else if (c >= 'a' && c <= 'f')
        digit = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
        digit = c - 'A' + 10;
    else {
        p->error = 1;
        return;
    }
    p->hexValue = (fn_u16)((p->hexValue << 4) | digit);
    if (--p->hexLeft == 0) {
        AddCode(p, p->hexValue);
        p->state = ST_STRING;
    }
}

void lb_feed(LbParser *p, const char *data, fn_u16 len)
{
    fn_u16 i;
    char c;

    for (i = 0; i < len && !p->error; i++) {
        c = data[i];
        switch (p->state) {
        case ST_VALUE:
            Value(p, c);
            break;
        case ST_STRING:
            StringChar(p, (unsigned char)c);
            break;
        case ST_ESCAPE:
            EscapeChar(p, c);
            break;
        case ST_HEX:
            HexChar(p, c);
            break;
        case ST_NUMBER:
            if (c >= '0' && c <= '9') {
                if (!p->fraction && p->number < 100000L)
                    p->number = p->number * 10 + (fn_u32)(c - '0');
            } else if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
                p->fraction = 1; /* the integer part is enough */
            } else {
                NumberValue(p);
                p->state = ST_VALUE;
                Value(p, c);
            }
            break;
        case ST_LITERAL:
            if (c < 'a' || c > 'z') {
                p->state = ST_VALUE;
                Value(p, c);
            }
            break;
        case ST_DONE:
            if (!IsBlank(c))
                p->error = 1;
            break;
        }
    }
}

int lb_finish(LbParser *p)
{
    return !p->error && p->started && p->state == ST_DONE;
}
