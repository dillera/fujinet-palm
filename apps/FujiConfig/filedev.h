/* FujiNet NIO's FileDevice (0xFE, docs/file_device_protocol.md in
 * fujinet-nio), called through fujinet-nio-lib's fn_raw_call.
 *
 * Every call names its file or folder by a full URI, such as
 * tnfs://apps.irata.online/PALM/ or persist:///hotsync/install/App.prc, and
 * none keeps state on FujiNet between calls. So a call whose reply was lost
 * on the cradle is simply sent again: the same list page, the same read, or
 * the same bytes written at the same offset. Each returns FN_OK or an FN_ERR_*
 * code, from the cradle line or from FileDevice's status (the same codes).
 */
#ifndef FILEDEV_H
#define FILEDEV_H

#include <PalmOS.h>

#include "fujinet-nio.h"

/* A name with room for the '/' that marks a folder, and its NUL. */
#define FD_NAME_LEN 64
/* Largest ReadFile or WriteFile; with its header and a URI of FD_URI_LEN it
 * fits the library's packet: 256 bytes on the cradle (512-byte packets), 512
 * on a Springboard module (1024). */
#define FD_CHUNK (FN_MAX_PACKET_SIZE / 2)
#define FD_URI_LEN 240

typedef struct {
    char name[FD_NAME_LEN]; /* folders end in '/' */
    UInt32 size;
} FdEntry;

/* Replies lost on the cradle and asked for again, for the info box. */
extern UInt16 gFdRetries;

/* Lists one page of a folder, sorted by name, from entry start, leaving out
 * "." and "..": *count entries, then *next is where the next page starts. */
uint8_t FdList(const char *uri, UInt16 start, FdEntry *entries, UInt16 max, UInt16 *count,
               UInt16 *next, Boolean *more);
/* Reads up to FD_CHUNK bytes at offset; *eof is set at the end of the file. */
uint8_t FdRead(const char *uri, UInt32 offset, UInt8 *data, UInt16 max, UInt16 *got,
               Boolean *eof);
/* Writes len bytes at offset. Offset 0 creates the file or empties it first. */
uint8_t FdWrite(const char *uri, UInt32 offset, const UInt8 *data, UInt16 len);
/* Creates a folder and any missing parents; one already there is fine. */
uint8_t FdMakeFolder(const char *uri);

#endif /* FILEDEV_H */
