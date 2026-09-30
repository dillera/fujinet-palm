/* Fixed-width types for code shared by the Palm apps and desktop tests. */
#ifndef FN_TYPES_H
#define FN_TYPES_H

#ifdef __palmos__
#include <PalmOS.h>
typedef UInt8 fn_u8;
typedef UInt16 fn_u16;
typedef UInt32 fn_u32;
#define fn_memcpy(dst, src, n) MemMove((dst), (src), (n))
#else
#include <stdint.h>
#include <string.h>
typedef uint8_t fn_u8;
typedef uint16_t fn_u16;
typedef uint32_t fn_u32;
#define fn_memcpy(dst, src, n) memmove((dst), (src), (n))
#endif

#endif /* FN_TYPES_H */
