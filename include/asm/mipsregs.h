#ifndef VCOREIII_STANDALONE_MIPSREGS_H
#define VCOREIII_STANDALONE_MIPSREGS_H

#ifdef __ASSEMBLY__
#define CP0_INDEX    $0
#define CP0_ENTRYLO0 $2
#define CP0_ENTRYLO1 $3
#define CP0_PAGEMASK $5
#define CP0_ENTRYHI  $10
#define CP0_STATUS   $12
#define CP0_CAUSE    $13
#define CP0_CONFIG   $16
#define CP0_WATCHLO  $18
#define CP0_WATCHHI  $19
#define CP0_TAGLO    $28
#else
#include <linux/types.h>

#define __read_c0(reg, sel) ({ \
    uint32_t __v; \
    __asm__ __volatile__("mfc0 %0, $" #reg ", " #sel : "=r"(__v)); \
    __v; \
})
#define __write_c0(reg, sel, value) do { \
    uint32_t __v = (uint32_t)(value); \
    __asm__ __volatile__("mtc0 %0, $" #reg ", " #sel : : "r"(__v)); \
} while (0)

#define read_c0_config()        __read_c0(16, 0)
#define read_c0_config1()       __read_c0(16, 1)
#define write_c0_config(v)      __write_c0(16, 0, (v))
#define write_c0_index(v)       __write_c0(0, 0, (v))
#define write_c0_entrylo0(v)    __write_c0(2, 0, (v))
#define write_c0_entrylo1(v)    __write_c0(3, 0, (v))
#define write_c0_pagemask(v)    __write_c0(5, 0, (v))
#define write_c0_entryhi(v)     __write_c0(10, 0, (v))
#define write_c0_taglo(v)       __write_c0(28, 0, (v))

#define CONF_CM_CMASK                 7
#define CONF_CM_CACHABLE_NONCOHERENT  3

/* These must be emitted at each pre-DDR call site.  Keeping them as statement
 * macros avoids GCC's size-based refusal to inline a normal static-inline
 * function inside the already force-inlined initialization graph.  The emitted
 * instruction sequence and memory barriers are unchanged. */
#define mtc0_tlbw_hazard() do { \
    __asm__ __volatile__("ehb" : : : "memory"); \
} while (0)

#define tlb_write_indexed() do { \
    __asm__ __volatile__( \
        ".set push\n\t" \
        ".set noreorder\n\t" \
        "tlbwi\n\t" \
        ".set pop\n\t" \
        : : : "memory"); \
} while (0)
#endif

#endif
