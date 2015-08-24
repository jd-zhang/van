#ifndef _VAN_TYPES_H
#define _VAN_TYPES_H

#ifdef __CYGWIN__
typedef unsigned char uint8;
typedef char int8;
typedef unsigned short uint16;
typedef short int16;
typedef unsigned int uint32;
typedef int int32;
typedef unsigned long long uint64;
typedef long long int64;
#endif

#if defined(__linux) || defined(__linux__)
typedef unsigned char uint8;
typedef char int8;
typedef unsigned short uint16;
typedef short int16;
typedef unsigned int uint32;
typedef int int32;
typedef unsigned long long uint64;
typedef long long int64;
#endif

typedef uint32 flags_t;
typedef uint64 ticks_t;
#define MAX_TICKS LONG_LONG_MAX

#endif
