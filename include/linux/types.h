#ifndef VCOREIII_STANDALONE_LINUX_TYPES_H
#define VCOREIII_STANDALONE_LINUX_TYPES_H

typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef signed short int16_t;
typedef unsigned short uint16_t;
typedef signed int int32_t;
typedef unsigned int uint32_t;
typedef signed long long int64_t;
typedef unsigned long long uint64_t;

typedef unsigned char u_int8_t;
typedef unsigned short u_int16_t;
typedef unsigned int u_int32_t;
typedef unsigned long ulong;
typedef _Bool bool;

#ifndef true
#define true 1
#endif
#ifndef false
#define false 0
#endif
#ifndef NULL
#define NULL ((void *)0)
#endif

#endif
