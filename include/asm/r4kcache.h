#ifndef VCOREIII_STANDALONE_R4KCACHE_H
#define VCOREIII_STANDALONE_R4KCACHE_H
#include <asm/cacheops.h>
#define cache_op(op, addr) \
    __asm__ __volatile__( \
        ".set push\n\t" \
        ".set noreorder\n\t" \
        ".set mips32r2\n\t" \
        "cache %0, 0(%1)\n\t" \
        ".set pop\n\t" \
        : : "i" (op), "r" (addr) : "memory")
#endif
