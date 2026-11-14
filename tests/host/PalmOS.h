/* Just enough of Palm OS for palm/suite.c to build and run on a desktop
 * (tests/host/host.c supplies the link, the clock and the UI). StrPrintF
 * follows Palm OS: %u and %d take an int, %lu a 32-bit value. */
#ifndef HOST_PALMOS_H
#define HOST_PALMOS_H

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef uint8_t UInt8;
typedef uint16_t UInt16;
typedef uint32_t UInt32;
typedef int16_t Int16;
typedef int32_t Int32;
typedef char Char;
typedef unsigned char Boolean;
typedef uint16_t Err;
typedef void *MemPtr;

#define true 1
#define false 0
#define errNone 0
#define appErrorClass 0x8000

#define sysFtrCreator 0x70737973UL
#define sysFtrNumROMVersion 1
#define sysFtrNumProcessorID 2
#define sysFtrNumOEMCompanyID 20
#define sysFtrNumOEMDeviceID 21

int host_StrPrintF(char *out, const char *fmt, ...);
Err FtrGet(UInt32 creator, UInt16 feature, UInt32 *value);

#define StrPrintF host_StrPrintF
#define StrCopy(d, s) strcpy((d), (s))
#define StrCat(d, s) strcat((d), (s))
#define StrLen(s) ((UInt16)strlen(s))
#define StrNCopy(d, s, n) strncpy((d), (s), (n))
/* Palm's n is the size of the whole destination, NUL included. */
#define StrNCat(d, s, n) strncat((d), (s), (n) > strlen(d) + 1 ? (n) - strlen(d) - 1 : 0)
#define StrCompare(a, b) strcmp((a), (b))
#define StrNCompare(a, b, n) strncmp((a), (b), (n))
#define MemSet(p, n, v) memset((p), (v), (n))
#define MemMove(d, s, n) memmove((d), (s), (n))
#define MemPtrNew(n) malloc(n)
#define MemPtrFree(p) free(p)
#define EvtResetAutoOffTimer() ((void)0)
#define TimGetSeconds() ((UInt32)time(NULL))
UInt32 TimGetTicks(void);
#define SysRandom(seed) (srand(seed), (Int16)(rand() & 0x7FFF))

#endif
