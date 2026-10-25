/* FujiNet NIO's FileDevice over the cradle; see filedev.h.
 *
 * Every request starts with the protocol version and the URI, and every
 * reply with the version, flags and two reserved bytes. FileDevice's status
 * comes apart from its reply bytes, as the transport's first parameter. All
 * numbers are little-endian.
 */
#include <PalmOS.h>

#include "fn_raw.h"
#include "filedev.h"

#define FD_DEVICE 0xFE
#define FD_VERSION 1
#define FD_LIST 0x02
#define FD_READ 0x03
#define FD_WRITE 0x04
#define FD_MAKE_FOLDER 0x06

/* A request's bytes, less FujiBus's 6-byte header. */
#define FD_REQUEST_MAX (FN_MAX_PACKET_SIZE - 6)
/* The entries in one list page; with the reply's header they fit a packet. */
#define FD_LIST_BYTES 400
#define FD_LIST_SORTED 0x02
/* Per entry: flags, name length, then the name, a u64 size and a u64 time. */
#define FD_ENTRY_FIXED 18
#define FD_ENTRY_FOLDER 0x01
#define FD_MAKE_PARENTS 0x01
#define FD_EXIST_OK 0x02

/* A lost reply is asked for again this many times in all. */
#define FD_TRIES 3
/* FileDevice busy: wait a moment and ask again, this many times. */
#define FD_WAITS 8
#define FD_WAIT_TICKS (SysTicksPerSecond() / 4)

UInt16 gFdRetries;

static UInt8 gRequest[FN_MAX_PACKET_SIZE];
static UInt8 gReply[FN_MAX_PACKET_SIZE];

static void PutLE16(UInt8 *p, UInt16 v)
{
    p[0] = (UInt8)v;
    p[1] = (UInt8)(v >> 8);
}

static void PutLE32(UInt8 *p, UInt32 v)
{
    PutLE16(p, (UInt16)v);
    PutLE16(p + 2, (UInt16)(v >> 16));
}

static UInt16 GetLE16(const UInt8 *p)
{
    return (UInt16)(p[0] | ((UInt16)p[1] << 8));
}

static UInt32 GetLE32(const UInt8 *p)
{
    return GetLE16(p) | ((UInt32)GetLE16(p + 2) << 16);
}

/* Starts a request with the version and URI; returns where the rest goes,
 * or 0 if the URI is too long. */
static UInt16 PutUri(const char *uri)
{
    UInt16 len = StrLen(uri);

    if (len == 0 || len > FD_URI_LEN)
        return 0;
    gRequest[0] = FD_VERSION;
    PutLE16(gRequest + 1, len);
    MemMove(gRequest + 3, uri, len);
    return 3 + len;
}

/* A reply lost on the cradle line, or cut short on it. */
static Boolean LostReply(uint8_t result)
{
    return result == FN_ERR_TIMEOUT || result == FN_ERR_TRANSPORT || result == FN_ERR_IO;
}

/* Sends the request in gRequest and leaves the reply in gReply. The reply
 * must be at least minReply bytes and, when echoBytes is not 0, echo the
 * request's offset or index at byte 4; one that does not answered some
 * earlier request, whose reply came late, so it counts as lost. */
static uint8_t Call(UInt8 command, UInt16 length, UInt16 minReply, UInt32 echo,
                    UInt8 echoBytes, UInt16 *replyLen)
{
    fn_raw_response_t response;
    uint8_t result;
    UInt8 tries = 0, waits = 0;
    UInt32 echoed;

    for (;;) {
        result = fn_raw_call(FD_DEVICE, command, gRequest, length, gReply, sizeof(gReply),
                             &response);
        if (result == FN_OK) {
            if ((response.status == FN_ERR_BUSY || response.status == FN_ERR_NOT_READY) &&
                ++waits < FD_WAITS) {
                SysTaskDelay(FD_WAIT_TICKS);
                continue;
            }
            if (response.status != FN_OK)
                return response.status;
            echoed = echoBytes == 4 ? GetLE32(gReply + 4) : GetLE16(gReply + 4);
            if (response.payload_length >= minReply && gReply[0] == FD_VERSION &&
                (echoBytes == 0 || echoed == echo)) {
                *replyLen = response.payload_length;
                return FN_OK;
            }
            result = FN_ERR_IO;
        }
        if (!LostReply(result) || ++tries >= FD_TRIES)
            return result;
        gFdRetries++;
    }
}

uint8_t FdList(const char *uri, UInt16 start, FdEntry *entries, UInt16 max, UInt16 *count,
               UInt16 *next, Boolean *more)
{
    UInt16 at = PutUri(uri), replyLen, listed, end, pos, len, keep, i;
    uint8_t result;
    FdEntry *entry;

    *count = 0;
    *next = start;
    *more = false;
    if (at == 0)
        return FN_ERR_URL_TOO_LONG;
    PutLE16(gRequest + at, start);
    PutLE16(gRequest + at + 2, FD_LIST_BYTES);
    gRequest[at + 4] = FD_LIST_SORTED;
    gRequest[at + 5] = FD_NAME_LEN - 2; /* longest name to send */
    result = Call(FD_LIST, at + 6, 10, start, 2, &replyLen);
    if (result != FN_OK)
        return result;

    listed = GetLE16(gReply + 6);
    end = 10 + GetLE16(gReply + 8);
    if (end > replyLen)
        return FN_ERR_IO;
    pos = 10;
    for (i = 0; i < listed && *count < max; i++) {
        if (pos + 2 > end || pos + FD_ENTRY_FIXED + gReply[pos + 1] > end)
            return FN_ERR_IO;
        len = gReply[pos + 1];
        keep = len < FD_NAME_LEN - 2 ? len : FD_NAME_LEN - 2;
        entry = &entries[*count];
        MemMove(entry->name, gReply + pos + 2, keep);
        if (gReply[pos] & FD_ENTRY_FOLDER)
            entry->name[keep++] = '/';
        entry->name[keep] = '\0';
        /* The low half of the u64 size; HotSync takes nothing near 4 GB. */
        entry->size = GetLE32(gReply + pos + 2 + len);
        pos += FD_ENTRY_FIXED + len;
        if (StrCompare(entry->name, "./") != 0 && StrCompare(entry->name, "../") != 0)
            (*count)++;
    }
    *next = start + i;
    *more = (gReply[1] & 0x01) != 0 || i < listed;
    return FN_OK;
}

uint8_t FdRead(const char *uri, UInt32 offset, UInt8 *data, UInt16 max, UInt16 *got,
               Boolean *eof)
{
    UInt16 at = PutUri(uri), replyLen, len;
    uint8_t result;

    *got = 0;
    *eof = false;
    if (at == 0)
        return FN_ERR_URL_TOO_LONG;
    if (max > FD_CHUNK)
        max = FD_CHUNK;
    PutLE32(gRequest + at, offset);
    PutLE16(gRequest + at + 4, max);
    result = Call(FD_READ, at + 6, 10, offset, 4, &replyLen);
    if (result != FN_OK)
        return result;
    len = GetLE16(gReply + 8);
    if (len > max || 10 + len > replyLen)
        return FN_ERR_IO;
    MemMove(data, gReply + 10, len);
    *got = len;
    *eof = (gReply[1] & 0x01) != 0;
    return FN_OK;
}

uint8_t FdWrite(const char *uri, UInt32 offset, const UInt8 *data, UInt16 len)
{
    UInt16 at = PutUri(uri), replyLen;
    uint8_t result;

    if (at == 0 || at + 6 + len > FD_REQUEST_MAX)
        return FN_ERR_URL_TOO_LONG;
    PutLE32(gRequest + at, offset);
    PutLE16(gRequest + at + 4, len);
    MemMove(gRequest + at + 6, data, len);
    result = Call(FD_WRITE, at + 6 + len, 10, offset, 4, &replyLen);
    if (result == FN_OK && GetLE16(gReply + 8) != len)
        result = FN_ERR_IO; /* the filesystem took only part of it */
    return result;
}

uint8_t FdMakeFolder(const char *uri)
{
    UInt16 at = PutUri(uri), replyLen;

    if (at == 0)
        return FN_ERR_URL_TOO_LONG;
    gRequest[at] = FD_MAKE_PARENTS | FD_EXIST_OK;
    return Call(FD_MAKE_FOLDER, at + 1, 4, 0, 0, &replyLen);
}
