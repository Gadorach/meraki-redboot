/*
 *
 * VCore-III ROM Loader common initialization
 *
 * Copyright (C) 2011 Meraki, Inc.
 * Author: Kevin Paul Herbert <kph@meraki.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <linux/types.h>
#include <linux/irqflags.h>
#include <linux/smp.h>
#include <linux/serial_reg.h>
#include <asm/mipsregs.h>
#include <asm/page.h>
#include <asm/addrspace.h>
#include <asm/r4kcache.h>
#include <asm/cacheops.h>

#ifndef LOADER_ALWAYS_INLINE
#define LOADER_ALWAYS_INLINE static inline __attribute__((always_inline))
#endif
#ifndef LOADER_SMALL_DATA
#define LOADER_SMALL_DATA __attribute__((section(".sdata")))
#endif
#ifndef LOADER_STAGE_ENTRY
#define LOADER_STAGE_ENTRY __attribute__((noinline, used, externally_visible, nomips16))
#endif

#ifndef MAX
#define MAX(x,y) ((x) > (y) ? (x) : (y))
#endif

enum {
    DDR_TRAIN_OK,
    DDR_TRAIN_CONTINUE,
    DDR_TRAIN_ERROR,
};

#define FALSE 0
#define TRUE 1

#ifdef MIPS_VCOREIII_MEMORY_16BIT
#define VC3_MPAR_BURST_LENGTH 4 // in DDR2 16-bit mode, use burstlen 4
#define VC3_MPAR_BURST_SIZE   0 // Could be 1 for DDR3
#else // 8-bit IF
#define VC3_MPAR_BURST_LENGTH 8 // For 8-bit IF we must run burst-8
#define VC3_MPAR_BURST_SIZE   0 // Always 0 for 8-bit if
#endif


#define VTSS_MEMPARM_MEMCFG                                             \
    (VC3_MPAR_BURST_SIZE ? VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_CFG_BURST_SIZE : 0) | \
    (VC3_MPAR_BURST_LENGTH == 8 ? VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_CFG_BURST_LEN : 0) | \
    (VC3_MPAR_bank_addr_cnt == 3 ? VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_CFG_BANK_CNT : 0) | \
    VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_CFG_MSB_ROW_ADDR(VC3_MPAR_row_addr_cnt-1) | \
    VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_CFG_MSB_COL_ADDR(VC3_MPAR_col_addr_cnt-1)

#define VTSS_MEMPARM_PERIOD                                             \
    VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_REF_PERIOD_MAX_PEND_REF(1) |        \
    VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_REF_PERIOD_REF_PERIOD(VC3_MPAR_tREFI)

#define VTSS_MEMPARM_TIMING0                                            \
    VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_TIMING0_RAS_TO_PRECH_DLY(VC3_MPAR_tRAS_min-1) | \
    VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_TIMING0_WR_TO_PRECH_DLY(VC3_MPAR_CL + \
                                                            (VC3_MPAR_BURST_LENGTH == 8 ? 2 : 0) + \
                                                            VC3_MPAR_tWR) | \
    VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_TIMING0_RD_TO_PRECH_DLY((VC3_MPAR_BURST_LENGTH == 8 ? 3 : 1)) | \
    VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_TIMING0_WR_DATA_XFR_DLY(VC3_MPAR_CL-3) | \
    VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_TIMING0_RD_DATA_XFR_DLY(VC3_MPAR_CL-3)

#define VTSS_MEMPARM_TIMING1                                            \
    VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_TIMING1_RAS_TO_RAS_SAME_BANK_DLY(VC3_MPAR_tRC-1) | \
VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_TIMING1_BANK8_FAW_DLY(MAX(0,VC3_MPAR_tFAW-1)) | \
VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_TIMING1_PRECH_TO_RAS_DLY(VC3_MPAR_tRP-1) | \
VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_TIMING1_RAS_TO_RAS_DLY(VC3_MPAR_tRRD-1) | \
VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_TIMING1_RAS_TO_CAS_DLY(VC3_MPAR_tRCD-1) | \
VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_TIMING1_WR_TO_RD_DLY(VC3_MPAR_CL + \
                                                     (VC3_MPAR_BURST_LENGTH == 8 ? 2 : 0) + \
                                                     MAX(2,VC3_MPAR_tWTR))
#if (VC3_MPAR_tRPA > 0)
#define VTSS_MEMPARM_TIMING2                                            \
    VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_TIMING2_PRECH_ALL_DLY(VC3_MPAR_tRPA-1) | \
    VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_TIMING2_MDSET_DLY(VC3_MPAR_tMRD-1) | \
    VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_TIMING2_REF_DLY(VC3_MPAR_tRFC-1) | \
    VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_TIMING2_FOUR_HUNDRED_NS_DLY(VC3_MPAR__400_ns_dly)
#else
#define VTSS_MEMPARM_TIMING2                                            \
    VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_TIMING2_PRECH_ALL_DLY(VC3_MPAR_tRP-1) | \
    VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_TIMING2_MDSET_DLY(VC3_MPAR_tMRD-1) | \
    VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_TIMING2_REF_DLY(VC3_MPAR_tRFC-1) |    \
    VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_TIMING2_FOUR_HUNDRED_NS_DLY(VC3_MPAR__400_ns_dly)
#endif

#define VTSS_MEMPARM_TIMING3 \
    VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_TIMING3_WR_TO_RD_CS_CHANGE_DLY(MAX(3,VC3_MPAR_CL-1)) | \
    VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_TIMING3_ODT_WR_DLY(VC3_MPAR_CL-1) | \
    VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_TIMING3_LOCAL_ODT_RD_DLY(VC3_MPAR_CL-1)

#define VTSS_MEMPARM_MR0 \
	/* Assumes DDR2 operation */ \
	(VC3_MPAR_BURST_LENGTH == 8 ? 3 : 2) | (VC3_MPAR_CL << 4) | ((VC3_MPAR_tWR-1) << 9)

// Additional (target) offsets

#define VTSS_DDR_TO     0x20000000  /* DDR RAM base offset */
#define VTSS_MEMCTL1_TO 0x40000000  /* SPI/PI base offset */
#define VTSS_MEMCTL2_TO 0x50000000  /* SPI/PI base offset */
#define VTSS_FLASH_TO   VTSS_MEMCTL1_TO /* Flash base offset */
#define VTSS_PI_CS3_TO	(VTSS_MEMCTL2_TO | 0x0c000000) /* PI CS3 base offset */

#define TLB_HI_MASK      0xffffe000
#define TLB_LO_MASK      0x3fffffff /* Masks off Fill bits */
#define TLB_LO_SHIFT     6          /* PFN Start bit */

#define PAGEMASK_SHIFT   13

#define MMU_PAGE_CACHED   (3 << 3) /* C(5:3) Cache Coherency Attributes */
#define MMU_PAGE_UNCACHED (2 << 3) /* C(5:3) Cache Coherency Attributes */
#define MMU_PAGE_ACCELERATED (7 << 3) /* C(5:3) Uncached accelerated */
#define MMU_PAGE_DIRTY    VTSS_BIT(2) /* = Writeable */
#define MMU_PAGE_VALID    VTSS_BIT(1)
#define MMU_PAGE_GLOBAL   VTSS_BIT(0)
#define MMU_REGIO_RO_C    (MMU_PAGE_CACHED|MMU_PAGE_VALID|MMU_PAGE_GLOBAL)
#define MMU_REGIO_RO      (MMU_PAGE_UNCACHED|MMU_PAGE_VALID|MMU_PAGE_GLOBAL)
#define MMU_REGIO_RW      (MMU_PAGE_DIRTY|MMU_REGIO_RO)
#define MMU_REGIO_RW_C	  (MMU_PAGE_DIRTY|MMU_REGIO_RO_C)
#define MMU_REGIO_INVAL   (MMU_PAGE_GLOBAL)
#define MMU_REGIO_WRITE_ACC (MMU_PAGE_DIRTY|MMU_PAGE_ACCELERATED|MMU_PAGE_VALID|MMU_PAGE_GLOBAL)

struct _uart_ptr {
	volatile uint32_t rbr_thr;
	volatile uint32_t ier;
	volatile uint32_t iir_fcr;
	volatile uint32_t lcr;
	volatile uint32_t mcr;
	volatile uint32_t lsr;
	volatile uint32_t msr;
	volatile uint32_t scr;
	volatile uint32_t usr;
};

#define UART_BASE ((struct _uart_ptr *)(0x70100000))
#define BAUD_RATE (115200)

/* GCC 10 early-init rule: C stages must not allocate or address any data.
 * The flash dispatcher owns all diagnostic strings and emits them through its
 * runtime-base-relative assembly UART routine.  This immediate selector is the
 * data-free equivalent of the original 16-entry clock table. */
LOADER_ALWAYS_INLINE u_int32_t
clock_speed_khz(u_int32_t divider)
{
    u_int32_t speed;
    u_int32_t candidate;
    u_int32_t compare;
    u_int32_t selected = divider & 0xfU;

    /*
     * Keep this selector completely local to the instruction stream.  GCC
     * 4.7 and GCC 10 both lower a C switch/table here into absolute jumps or
     * read-only data, neither of which is valid while the flash loader is
     * executing from an arbitrary active/fallback copy.  movz performs the
     * same 16-way selection using only immediate values and registers.
     */
    asm volatile(
        "li     %[speed], 166600\n\t"

        "move   %[compare], %[selected]\n\t"
        "li     %[candidate], 625000\n\t"
        "movz   %[speed], %[candidate], %[compare]\n\t"

        "li     %[compare], 1\n\t"
        "xor    %[compare], %[selected], %[compare]\n\t"
        "li     %[candidate], 312500\n\t"
        "movz   %[speed], %[candidate], %[compare]\n\t"

        "li     %[compare], 2\n\t"
        "xor    %[compare], %[selected], %[compare]\n\t"
        "li     %[candidate], 500000\n\t"
        "movz   %[speed], %[candidate], %[compare]\n\t"

        "li     %[compare], 3\n\t"
        "xor    %[compare], %[selected], %[compare]\n\t"
        "li     %[candidate], 277770\n\t"
        "movz   %[speed], %[candidate], %[compare]\n\t"

        "li     %[compare], 4\n\t"
        "xor    %[compare], %[selected], %[compare]\n\t"
        "li     %[candidate], 500000\n\t"
        "movz   %[speed], %[candidate], %[compare]\n\t"

        "li     %[compare], 5\n\t"
        "xor    %[compare], %[selected], %[compare]\n\t"
        "li     %[candidate], 250000\n\t"
        "movz   %[speed], %[candidate], %[compare]\n\t"

        "li     %[compare], 6\n\t"
        "xor    %[compare], %[selected], %[compare]\n\t"
        "li     %[candidate], 416660\n\t"
        "movz   %[speed], %[candidate], %[compare]\n\t"

        "li     %[compare], 7\n\t"
        "xor    %[compare], %[selected], %[compare]\n\t"
        "li     %[candidate], 227270\n\t"
        "movz   %[speed], %[candidate], %[compare]\n\t"

        "li     %[compare], 8\n\t"
        "xor    %[compare], %[selected], %[compare]\n\t"
        "li     %[candidate], 416660\n\t"
        "movz   %[speed], %[candidate], %[compare]\n\t"

        "li     %[compare], 9\n\t"
        "xor    %[compare], %[selected], %[compare]\n\t"
        "li     %[candidate], 208330\n\t"
        "movz   %[speed], %[candidate], %[compare]\n\t"

        "li     %[compare], 10\n\t"
        "xor    %[compare], %[selected], %[compare]\n\t"
        "li     %[candidate], 357140\n\t"
        "movz   %[speed], %[candidate], %[compare]\n\t"

        "li     %[compare], 11\n\t"
        "xor    %[compare], %[selected], %[compare]\n\t"
        "li     %[candidate], 192300\n\t"
        "movz   %[speed], %[candidate], %[compare]\n\t"

        "li     %[compare], 12\n\t"
        "xor    %[compare], %[selected], %[compare]\n\t"
        "li     %[candidate], 357140\n\t"
        "movz   %[speed], %[candidate], %[compare]\n\t"

        "li     %[compare], 13\n\t"
        "xor    %[compare], %[selected], %[compare]\n\t"
        "li     %[candidate], 178570\n\t"
        "movz   %[speed], %[candidate], %[compare]\n\t"

        "li     %[compare], 14\n\t"
        "xor    %[compare], %[selected], %[compare]\n\t"
        "li     %[candidate], 312500\n\t"
        "movz   %[speed], %[candidate], %[compare]\n\t"
        : [speed] "=&r" (speed),
          [candidate] "=&r" (candidate),
          [compare] "=&r" (compare)
        : [selected] "r" (selected));

    return speed;
}

/* Diagnostics are emitted by head.S between leaf-stage calls. */
#define ANNOUNCE_LITERAL(str) do { } while (0)
#define ANNOUNCE_PROGRESS(name) do { } while (0)
#define ANNOUNCE_OK() do { } while (0)

LOADER_ALWAYS_INLINE u_int32_t init_board(void);

/*
 * check_chip_id - Verify we are on the right hardware
 */

LOADER_ALWAYS_INLINE bool
check_chip_id(void)
{
  return (VTSS_X_DEVCPU_GCB_CHIP_REGS_CHIP_ID_PART_ID
	  (VTSS_DEVCPU_GCB_CHIP_REGS_CHIP_ID) & 0xfff0) ==
    (EXPECTED_CHIP_ID & 0xfff0);
}

LOADER_ALWAYS_INLINE uint16_t
get_chip_id(void)
{
    return (uint16_t)(VTSS_X_DEVCPU_GCB_CHIP_REGS_CHIP_ID_PART_ID
                      (VTSS_DEVCPU_GCB_CHIP_REGS_CHIP_ID));
}

/*
 * Define a pipeline (8 instructions) worth of NOPs.
 */

#define PAUSE() asm volatile("nop; nop; nop; nop; nop; nop; nop; nop")

/*
 * init_uart - Initialize the console UART
 */

LOADER_ALWAYS_INLINE void
init_uart(void)
{
    int cpu_clock;
    uint16_t divisor;

    cpu_clock = clock_speed_khz(
      VTSS_X_MACRO_CTRL_PLL5G_CFG_PLL5G_CFG0_CPU_CLK_DIV
       (VTSS_MACRO_CTRL_PLL5G_CFG_PLL5G_CFG0));

    divisor = ((cpu_clock*1000)/2)/(BAUD_RATE*16);

    UART_BASE->lcr = UART_LCR_DLAB; /* Enable divisor latch */

    UART_BASE->rbr_thr = divisor & 0xff; /* Set baud rate LSB */
    UART_BASE->ier = divisor >> 8;	 /* Set baud rate MSB */

    UART_BASE->lcr = UART_LCR_WLEN8;	/* 8 bits, no parity, 1 stop bit */

    PAUSE();			/* Let the BRG settle */
    PAUSE();
    PAUSE();
}

/*
 * uart_putc - Put a character to the console
 *
 * This routine is used to put a character to the console.
 *
 * TO DO: calculate how long we should loop, instead of forever, to deal
 * with a stuck UART robustly.
 */

LOADER_ALWAYS_INLINE void
raw_uart_putc(char c)
{
    while ((UART_BASE->lsr & UART_LSR_THRE) == 0) ;

    UART_BASE->rbr_thr = c;
}

LOADER_ALWAYS_INLINE void
uart_putc(char c)
{
    if (c == '\n')
	raw_uart_putc('\r');	/* CR before LF, I mean handle newline */
    raw_uart_putc(c);
}

/*
 * uart_puts - Put a string to the console
 */

LOADER_ALWAYS_INLINE void
uart_puts(const char *str)
{
    while (*str)
	uart_putc(*str++);
}

/*
 * init_memctl - Initialize the memory controller
 */

LOADER_ALWAYS_INLINE void
init_memctl(void)
{
    ANNOUNCE_PROGRESS("init_memctl");

    /* Drop sys ctl memory controller is forced reset */
    VTSS_ICPU_CFG_CPU_SYSTEM_CTRL_RESET &= ~VTSS_F_ICPU_CFG_CPU_SYSTEM_CTRL_RESET_MEM_RST_FORCE;
    PAUSE();

    /* Drop Reset, enable SSTL */
    VTSS_ICPU_CFG_MEMCTRL_MEMPHY_CFG = VTSS_F_ICPU_CFG_MEMCTRL_MEMPHY_CFG_PHY_SSTL_ENA;
    PAUSE();

    VTSS_ICPU_CFG_MEMCTRL_MEMPHY_ZCAL =
        VTSS_F_ICPU_CFG_MEMCTRL_MEMPHY_ZCAL_ZCAL_PROG_ODT(7) | /* 60 ohms ODT */
        VTSS_F_ICPU_CFG_MEMCTRL_MEMPHY_ZCAL_ZCAL_PROG(7) | /* 60 ohms output impedance */
        VTSS_F_ICPU_CFG_MEMCTRL_MEMPHY_ZCAL_ZCAL_ENA;

    while(VTSS_ICPU_CFG_MEMCTRL_MEMPHY_ZCAL & VTSS_F_ICPU_CFG_MEMCTRL_MEMPHY_ZCAL_ZCAL_ENA)
        ;                       /* Wait for ZCAL to clear */

    /* Drive CL, CK, ODT */
    VTSS_ICPU_CFG_MEMCTRL_MEMPHY_CFG |=
        VTSS_F_ICPU_CFG_MEMCTRL_MEMPHY_CFG_PHY_ODT_OE |
        VTSS_F_ICPU_CFG_MEMCTRL_MEMPHY_CFG_PHY_CK_OE |
        VTSS_F_ICPU_CFG_MEMCTRL_MEMPHY_CFG_PHY_CL_OE;

    /* Initialize memory controller */
    VTSS_ICPU_CFG_MEMCTRL_MEMCTRL_CFG = VTSS_MEMPARM_MEMCFG;
    VTSS_ICPU_CFG_MEMCTRL_MEMCTRL_REF_PERIOD = VTSS_MEMPARM_PERIOD;

    VTSS_ICPU_CFG_MEMCTRL_MEMCTRL_TIMING0 =
        /* Keep upper 12 bits */
        (VTSS_ICPU_CFG_MEMCTRL_MEMCTRL_TIMING0 & ~VTSS_BITMASK(20)) |
        VTSS_MEMPARM_TIMING0;
    VTSS_ICPU_CFG_MEMCTRL_MEMCTRL_TIMING1 = VTSS_MEMPARM_TIMING1;
    VTSS_ICPU_CFG_MEMCTRL_MEMCTRL_TIMING2 = VTSS_MEMPARM_TIMING2;

#define ODT 1 /* 1 = Setup On-die termination, 0 = disabled */
#if ODT
    VTSS_ICPU_CFG_MEMCTRL_MEMCTRL_TIMING3 =
        VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_TIMING3_WR_TO_RD_CS_CHANGE_DLY(MAX(3,VC3_MPAR_CL-1)) |
        VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_TIMING3_ODT_WR_DLY(VC3_MPAR_CL-3) |
        VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_TIMING3_LOCAL_ODT_RD_DLY(VC3_MPAR_CL-1);
#else
    VTSS_ICPU_CFG_MEMCTRL_MEMCTRL_TIMING3 = VTSS_MEMPARM_TIMING3;
#endif

    VTSS_ICPU_CFG_MEMCTRL_MEMCTRL_MR0_VAL = VTSS_MEMPARM_MR0;

#if ODT
#define ODTRTT 2  /* 1= 75ohm, 2=150ohm, 3=50ohm  */
    /* DLL-on, Full-OD, AL=0, RTT=ODTRTT, nDQS-on, RDQS-off, out-en */
    VTSS_ICPU_CFG_MEMCTRL_MEMCTRL_MR1_VAL = 0x00000382 | ( (ODTRTT&2) << 5) | ( (ODTRTT&1) << 2) ;
#else
    /* DLL-on, Full-OD, AL=0, RTT=off, nDQS-on, RDQS-off, out-en */
    VTSS_ICPU_CFG_MEMCTRL_MEMCTRL_MR1_VAL = 0x00000382;
#endif

    /* DDR2 */
    VTSS_ICPU_CFG_MEMCTRL_MEMCTRL_MR2_VAL = 0;
    VTSS_ICPU_CFG_MEMCTRL_MEMCTRL_MR3_VAL = 0;

#if ODT
    /* Termination setup - enable ODT */
    VTSS_ICPU_CFG_MEMCTRL_MEMCTRL_TERMRES_CTRL = VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_TERMRES_CTRL_ODT_WR_ENA;
#else
    /* Termination setup - disable ODT */
    VTSS_ICPU_CFG_MEMCTRL_MEMCTRL_TERMRES_CTRL = 0;
#endif

    ANNOUNCE_OK();
}

/*
 * wait_memctl - Wait for the memory controller to be ready
 */

LOADER_ALWAYS_INLINE void
wait_memctl(void)
{
    ANNOUNCE_PROGRESS("wait_memctl");

    /* Now, rip it! */
    VTSS_ICPU_CFG_MEMCTRL_MEMCTRL_CTRL = VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_CTRL_INITIALIZE;

    while(!(VTSS_ICPU_CFG_MEMCTRL_MEMCTRL_STAT & VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_STAT_INIT_DONE))
        ;

    /* The training below must now be called for each bytelane - see below */

    ANNOUNCE_OK();
}

#ifdef MIPS_VCOREIII_MEMORY_16BIT
  #define ADDR_DQS_DLY(__byte_lane__) &VTSS_ICPU_CFG_MEMCTRL_MEMCTRL_DQS_DLY(__byte_lane__)
#else
  #define ADDR_DQS_DLY(__byte_lane__) &VTSS_ICPU_CFG_MEMCTRL_MEMCTRL_DQS_DLY
#endif

LOADER_ALWAYS_INLINE void
set_dly(u_int8_t byte_lane, u_int32_t dly)
{
    volatile unsigned long *reg = ADDR_DQS_DLY(byte_lane);
    u_int32_t r = *reg;
    r &= ~VTSS_M_ICPU_CFG_MEMCTRL_MEMCTRL_DQS_DLY_DQS_DLY;
    *reg = r | VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_DQS_DLY_DQS_DLY(dly);
}

LOADER_ALWAYS_INLINE bool
adjust_dly(u_int8_t byte_lane, int adjust)
{
    volatile unsigned long *reg = ADDR_DQS_DLY(byte_lane);
    u_int32_t r   = *reg;
    u_int32_t dly = VTSS_X_ICPU_CFG_MEMCTRL_MEMCTRL_DQS_DLY_DQS_DLY(r);
    dly += adjust;
    if(dly < 31) {
        r &= ~VTSS_M_ICPU_CFG_MEMCTRL_MEMCTRL_DQS_DLY_DQS_DLY;
        *reg = r | VTSS_F_ICPU_CFG_MEMCTRL_MEMCTRL_DQS_DLY_DQS_DLY(dly);
        return true;
    }
    return false;
}

LOADER_ALWAYS_INLINE int
lookfor_and_incr(u_int8_t byte)
{
    volatile u_int8_t *ram = (volatile u_int8_t *) VTSS_DDR_TO;
#ifdef MIPS_VCOREIII_MEMORY_16BIT
    u_int8_t b0 = ram[0], b1 = ram[1];

    if(b0 != byte &&
       !adjust_dly(0, 1))
        return DDR_TRAIN_ERROR;
    if(b1 != byte &&
       !adjust_dly(1, 1))
        return DDR_TRAIN_ERROR;
    return ((b0 == byte) && (b1 == byte)) ? DDR_TRAIN_OK : DDR_TRAIN_CONTINUE;
#else
    u_int8_t b0 = ram[0];

    if(b0 != byte &&
       !adjust_dly(0, 1))
        return DDR_TRAIN_ERROR;
    return (b0 == byte) ? DDR_TRAIN_OK : DDR_TRAIN_CONTINUE;
#endif
}

LOADER_ALWAYS_INLINE int
train_bytelane(void)
{
    int res;
#ifdef MIPS_VCOREIII_MEMORY_16BIT
    ((volatile u_int32_t *)VTSS_DDR_TO)[0] = 0x0000FFFF;
    ((volatile u_int32_t *)VTSS_DDR_TO)[1] = 0x00000000;
#else
    ((volatile u_int32_t *)VTSS_DDR_TO)[0] = 0x000000FF;
#endif

    set_dly(0, 0);
#ifdef MIPS_VCOREIII_MEMORY_16BIT
    set_dly(1, 0);
#endif
    while ((res = lookfor_and_incr(0xff)) == DDR_TRAIN_CONTINUE)
        ;
    if(res != DDR_TRAIN_OK)
        return res;
    while ((res = lookfor_and_incr(0x00)) == DDR_TRAIN_CONTINUE)
        ;
    if(res != DDR_TRAIN_OK)
        return res;

    adjust_dly(0, -3);
#ifdef MIPS_VCOREIII_MEMORY_16BIT
    adjust_dly(1, -3);
#endif
    return DDR_TRAIN_OK;
}

LOADER_ALWAYS_INLINE void
reset_memory_fifo(void)
{
    VTSS_ICPU_CFG_MEMCTRL_MEMPHY_CFG |=
      VTSS_F_ICPU_CFG_MEMCTRL_MEMPHY_CFG_PHY_FIFO_RST; /* Set FIFO reset */
    VTSS_ICPU_CFG_MEMCTRL_MEMPHY_CFG &=
      ~VTSS_F_ICPU_CFG_MEMCTRL_MEMPHY_CFG_PHY_FIFO_RST; /* Clear FIFO reset */
}

LOADER_ALWAYS_INLINE bool
test_memory(void)
{
    volatile u_int32_t *ram = (volatile u_int32_t *)VTSS_DDR_TO;
    int count;

    ram[0] = 0;
    ram[1] = ~0;

    for (count = 0; count < 5; count++)
        if (ram[0] != 0)
	    return FALSE;

    for (count = 0; count < 5; count++)
        if (ram[1] != ~0)
	    return FALSE;

    return TRUE;
}

LOADER_ALWAYS_INLINE bool
memory_boot_mode_enabled(void)
{
    return (VTSS_ICPU_CFG_CPU_SYSTEM_CTRL_GENERAL_CTRL &
            VTSS_F_ICPU_CFG_CPU_SYSTEM_CTRL_GENERAL_CTRL_BOOT_MODE_ENA) != 0;
}

LOADER_ALWAYS_INLINE u_int32_t
init_memory_controller_config_stage(void)
{
    if (!memory_boot_mode_enabled())
        return 0U;
    init_memctl();
    return 1U;
}

LOADER_ALWAYS_INLINE u_int32_t
wait_memory_controller_stage(void)
{
    if (!memory_boot_mode_enabled())
        return 0U;
    wait_memctl();
    return 1U;
}

LOADER_ALWAYS_INLINE u_int32_t
train_memory_stage(void)
{
    u_int32_t retries = 0U;
    if (!memory_boot_mode_enabled())
        return 0U;

    /* Preserve the original retry-until-stable DDR training policy. */
    while (TRUE) {
        if ((train_bytelane() == DDR_TRAIN_OK) && test_memory())
            break;
        retries++;
        reset_memory_fifo();
    }
    return retries;
}

/*
 * irq_init - Initialize interrupt controller
 */

LOADER_ALWAYS_INLINE void
init_irq(void)
{
    ANNOUNCE_PROGRESS("init_irq");

    /* Disable all IRQs (to IRQ0) */
    VTSS_ICPU_CFG_INTR_INTR_ENA = 0;

    /* Enable Routing of IRQs to IRQ0 */
    VTSS_ICPU_CFG_INTR_ICPU_IRQ0_ENA =
      VTSS_F_ICPU_CFG_INTR_ICPU_IRQ0_ENA_ICPU_IRQ0_ENA;

    ANNOUNCE_OK();
}

LOADER_ALWAYS_INLINE int
get_tlb_count(void)
{
    return ((read_c0_config1() >> 25) & 0x3f) + 1;
}

LOADER_ALWAYS_INLINE void
set_tlb_entry(u_int32_t index, u_int32_t tlbmask, u_int32_t tlbhi,
	      u_int32_t tlblo0, u_int32_t tlblo1)
{
    write_c0_index(index);
    write_c0_pagemask(tlbmask);
    write_c0_entryhi(tlbhi);
    write_c0_entrylo0(tlblo0);
    write_c0_entrylo1(tlblo1);
    mtc0_tlbw_hazard();
    tlb_write_indexed();
}

LOADER_ALWAYS_INLINE void
create_tlb(int index, u_int32_t offset, u_int32_t size,
           u_int32_t tlb_attrib1, u_int32_t tlb_attrib2)
{
    u_int32_t tlb_mask, tlb_lo0, tlb_lo1;

    tlb_mask  = ((size >> 12) - 1) << PAGEMASK_SHIFT;
    tlb_lo0 = tlb_attrib1 | ( offset              >> TLB_LO_SHIFT);
    tlb_lo1 = tlb_attrib2 | ((offset | size )     >> TLB_LO_SHIFT);

    set_tlb_entry(index, tlb_mask, offset & TLB_HI_MASK, tlb_lo0 & TLB_LO_MASK,
		  tlb_lo1 & TLB_LO_MASK);
}

LOADER_ALWAYS_INLINE void
invalidate_tlbs(void)
{
    int i, max;

    max = get_tlb_count();
    for(i = 0; i < max; i++)
        create_tlb(i, i << 20, 1 << 12, MMU_REGIO_INVAL, MMU_REGIO_INVAL);
}

LOADER_ALWAYS_INLINE void
init_io_mapping(void)
{
    /* 0x60000000 - 0x60ffffff */
    create_tlb(0, VTSS_TO_DEVCPU_ORG, 16 << 20, MMU_REGIO_RW, MMU_REGIO_RW);

    /* 0x70000000 - 0x70ffffff */
    create_tlb(1, VTSS_TO_CFG,        16 << 20, MMU_REGIO_RW, MMU_REGIO_RW);

    /* 0x40000000 - 0x40ffffff - CACHED! BOOT Flash CS0 */
    create_tlb(2, VTSS_FLASH_TO,      16 << 20, MMU_REGIO_RO_C, MMU_REGIO_RO_C);

    /* 0x20000000 - up */
    create_tlb(3, VTSS_DDR_TO,       256 << 20,  MMU_REGIO_RW, MMU_REGIO_RW);

    /* 0x5c00000 - 0x5fffffff - Uncached - PI - second processor */
    create_tlb(4, VTSS_PI_CS3_TO,     64 << 20, MMU_REGIO_RW, MMU_REGIO_RW);
}

LOADER_ALWAYS_INLINE void
init_tlb(void)
{
    uint32_t gp;

    asm ("move %0, $gp" : "=r" (gp));
    if ((gp & 0xf0000000) != 0x40000000) {
	invalidate_tlbs();
	init_io_mapping();
    }
}

LOADER_ALWAYS_INLINE void
init_pll(void)
{
    ANNOUNCE_PROGRESS("init_pll");

    /* Wait for PLL to be locked */

    while (!(VTSS_MACRO_CTRL_PLL5G_STATUS_PLL5G_STATUS0 &
	     VTSS_F_MACRO_CTRL_PLL5G_STATUS_PLL5G_STATUS0_LOCK_STATUS))
      ;

    VTSS_MACRO_CTRL_PLL5G_CFG_PLL5G_CFG2 = 0x610400;
    VTSS_MACRO_CTRL_PLL5G_CFG_PLL5G_CFG2 = 0x610c00;
    VTSS_MACRO_CTRL_PLL5G_CFG_PLL5G_CFG2 = 0x610800;
    VTSS_MACRO_CTRL_PLL5G_CFG_PLL5G_CFG2 = 0x610000;

    while (!(VTSS_MACRO_CTRL_PLL5G_STATUS_PLL5G_STATUS0 &
	     VTSS_F_MACRO_CTRL_PLL5G_STATUS_PLL5G_STATUS0_LOCK_STATUS))
      ;

    /* Set clock to 416Mhz */

    VTSS_MACRO_CTRL_PLL5G_CFG_PLL5G_CFG0 =
      (VTSS_MACRO_CTRL_PLL5G_CFG_PLL5G_CFG0 &
       ~VTSS_M_MACRO_CTRL_PLL5G_CFG_PLL5G_CFG0_CPU_CLK_DIV) |
      VTSS_F_MACRO_CTRL_PLL5G_CFG_PLL5G_CFG0_CPU_CLK_DIV(6);

    /* Wait for PLL to be locked */

    while (!(VTSS_MACRO_CTRL_PLL5G_STATUS_PLL5G_STATUS0 &
	     VTSS_F_MACRO_CTRL_PLL5G_STATUS_PLL5G_STATUS0_LOCK_STATUS))
      ;
}


LOADER_ALWAYS_INLINE void
init_spi(void)
{
    ANNOUNCE_PROGRESS("init_spi");

    /* Set Fast reads, desel 0x19 + div ~ 33-41Mhz */
    VTSS_ICPU_CFG_SPI_MST_SPI_MST_CFG =
      VTSS_F_ICPU_CFG_SPI_MST_SPI_MST_CFG_FAST_READ_ENA +
      VTSS_F_ICPU_CFG_SPI_MST_SPI_MST_CFG_CS_DESELECT_TIME(0x19) +
      VTSS_F_ICPU_CFG_SPI_MST_SPI_MST_CFG_CLK_DIV(8);

    ANNOUNCE_OK();
}

LOADER_ALWAYS_INLINE void
init_dram_uncached(u_int32_t *uncached_ram, u_int32_t cache_size)
{
    u_int32_t *addr;

    ANNOUNCE_PROGRESS("init_dram_uncached");

    for (addr = uncached_ram; addr < uncached_ram + cache_size; addr += 16) {
	addr[0] = 0;
	addr[1] = 0;
	addr[2] = 0;
	addr[3] = 0;
	addr[4] = 0;
	addr[5] = 0;
	addr[6] = 0;
	addr[7] = 0;
	addr[8] = 0;
	addr[9] = 0;
	addr[10] = 0;
	addr[11] = 0;
	addr[12] = 0;
	addr[13] = 0;
	addr[14] = 0;
	addr[15] = 0;
    }

    ANNOUNCE_OK();
}


LOADER_ALWAYS_INLINE void
init_icache(u_int32_t *cached_ram, u_int32_t cache_size, u_int32_t line_size)
{
    u_int32_t *addr;

    ANNOUNCE_PROGRESS("init_icache");

    write_c0_taglo(0);	     /* Ensure valid bit clear and parity consistent */

    for (addr = cached_ram; addr < cached_ram + cache_size; addr += line_size) {
        cache_op(Index_Store_Tag_I, addr); /* Clear tag to invalidate */
	cache_op(Fill, addr);		   /* Fill so parity is correct */
	cache_op(Index_Store_Tag_I, addr); /* Invalidate to be safe */
    }

    ANNOUNCE_OK();
}

LOADER_ALWAYS_INLINE void
init_dcache(u_int32_t *cached_ram, u_int32_t cache_size, u_int32_t line_size)
{
    u_int32_t *addr;
    u_int32_t dummy;

    ANNOUNCE_PROGRESS("init_dcache");

    write_c0_taglo(0);	     /* Ensure valid bit clear and parity consistent */

    for (addr = cached_ram; addr < cached_ram + cache_size; addr += line_size) {
        cache_op(Index_Store_Tag_D, addr); /* Clear tag to invalidate */
    }

    for (addr = cached_ram; addr < cached_ram + cache_size; addr += line_size) {
        dummy = *(volatile u_int32_t *)addr;	/* Read from each cache line */
    }

    for (addr = cached_ram; addr < cached_ram + cache_size; addr += line_size) {
	cache_op(Index_Store_Tag_D, addr); /* Invalidate to be safe */
    }

    (void)dummy;

    ANNOUNCE_OK();
}

LOADER_ALWAYS_INLINE void
enable_caches(void)
{
    u_int32_t config;

    ANNOUNCE_PROGRESS("enable_caches");

    config = read_c0_config();
    config = (config & ~CONF_CM_CMASK) | CONF_CM_CACHABLE_NONCOHERENT;
    write_c0_config(config);

    ANNOUNCE_OK();
}

LOADER_ALWAYS_INLINE void
init_cache_prepare_stage(void)
{
    init_dram_uncached((u_int32_t *)VTSS_DDR_TO, 0x8000);
    create_tlb(3, VTSS_DDR_TO, 256 << 20, MMU_REGIO_RW_C, MMU_REGIO_RW_C);
}

LOADER_ALWAYS_INLINE void
init_icache_stage(void)
{
    init_icache((u_int32_t *)VTSS_DDR_TO, 0x8000,
                32 / (sizeof(u_int32_t)));
}

LOADER_ALWAYS_INLINE void
init_dcache_stage(void)
{
    init_dcache((u_int32_t *)VTSS_DDR_TO, 0x8000,
                32 / (sizeof(u_int32_t)));
}

LOADER_ALWAYS_INLINE void
init_cache_enable_stage(void)
{
    enable_caches();
    create_tlb(3, VTSS_DDR_TO, 256 << 20,
               MMU_REGIO_WRITE_ACC, MMU_REGIO_WRITE_ACC);
}


LOADER_ALWAYS_INLINE int32_t
read_mii(u_int16_t controller, u_int16_t phy_addr, u_int16_t reg_addr)
{
    u_int32_t val;

    /*
     * Issue the read operation to the controller
     */

    VTSS_DEVCPU_GCB_MIIM_MII_CMD(controller) =
	VTSS_F_DEVCPU_GCB_MIIM_MII_CMD_MIIM_CMD_OPR_FIELD(2) |
	VTSS_F_DEVCPU_GCB_MIIM_MII_CMD_MIIM_CMD_VLD |
	VTSS_F_DEVCPU_GCB_MIIM_MII_CMD_MIIM_CMD_REGAD(reg_addr) |
	VTSS_F_DEVCPU_GCB_MIIM_MII_CMD_MIIM_CMD_PHYAD(phy_addr);

    /*
     * Wait for the busy flag to be cleared
     */

    do {
        val = VTSS_DEVCPU_GCB_MIIM_MII_STATUS(controller);
    } while (val & VTSS_F_DEVCPU_GCB_MIIM_MII_STATUS_MIIM_STAT_BUSY);

    /*
     * Read the result register. Check for errors.
     */
    val = VTSS_DEVCPU_GCB_MIIM_MII_DATA(controller);
    if (val & VTSS_F_DEVCPU_GCB_MIIM_MII_DATA_MIIM_DATA_SUCCESS(3))
	return -1;

    /*
     * Success, return the value
     */

    return val & 0xffff;
}

LOADER_ALWAYS_INLINE void
write_mii(u_int16_t controller, u_int16_t phy_addr, u_int16_t reg_addr,
    u_int16_t data)
{
    u_int32_t val;

    /*
     * Issue the write operation to the controller
     */

    VTSS_DEVCPU_GCB_MIIM_MII_CMD(controller) =
	VTSS_F_DEVCPU_GCB_MIIM_MII_CMD_MIIM_CMD_OPR_FIELD(1) |
	VTSS_F_DEVCPU_GCB_MIIM_MII_CMD_MIIM_CMD_WRDATA(data) |
	VTSS_F_DEVCPU_GCB_MIIM_MII_CMD_MIIM_CMD_VLD |
	VTSS_F_DEVCPU_GCB_MIIM_MII_CMD_MIIM_CMD_REGAD(reg_addr) |
	VTSS_F_DEVCPU_GCB_MIIM_MII_CMD_MIIM_CMD_PHYAD(phy_addr);

    /*
     * Wait for the busy flag to be cleared
     */

    do {
        val = VTSS_DEVCPU_GCB_MIIM_MII_STATUS(controller);
    } while (val & VTSS_F_DEVCPU_GCB_MIIM_MII_STATUS_MIIM_STAT_BUSY);
}

/*
 * GCC 10 port: assembly orchestrates small pre-DDR leaf stages.  This limits
 * register pressure and lets the validator prove each translation unit is
 * stackless, call-free, and runtime-base-relative without changing the
 * original hardware-operation order.
 */
#define DEFINE_LOADER_INIT_STAGES(prefix)                                      \
LOADER_STAGE_ENTRY u_int32_t prefix##_stage_probe(void)                         \
{                                                                               \
    init_tlb();                                                                 \
    return check_chip_id() ? 1U : 0U;                                           \
}                                                                               \
LOADER_STAGE_ENTRY void prefix##_stage_console(void)                            \
{                                                                               \
    init_gpio();                                                                \
    init_uart();                                                                \
}                                                                               \
LOADER_STAGE_ENTRY void prefix##_stage_pll(void)                                \
{                                                                               \
    init_pll();                                                                 \
    init_uart();                                                                \
}                                                                               \
LOADER_STAGE_ENTRY void prefix##_stage_spi(void)                                \
{                                                                               \
    init_spi();                                                                 \
}                                                                               \
LOADER_STAGE_ENTRY u_int32_t prefix##_stage_memctl_config(void)                 \
{                                                                               \
    return init_memory_controller_config_stage();                               \
}                                                                               \
LOADER_STAGE_ENTRY u_int32_t prefix##_stage_memctl_wait(void)                   \
{                                                                               \
    return wait_memory_controller_stage();                                      \
}                                                                               \
LOADER_STAGE_ENTRY u_int32_t prefix##_stage_memtrain(void)                      \
{                                                                               \
    return train_memory_stage();                                                \
}                                                                               \
LOADER_STAGE_ENTRY void prefix##_stage_irq(void)                                \
{                                                                               \
    init_irq();                                                                 \
}                                                                               \
LOADER_STAGE_ENTRY void prefix##_stage_cache_prepare(void)                      \
{                                                                               \
    init_cache_prepare_stage();                                                 \
}                                                                               \
LOADER_STAGE_ENTRY void prefix##_stage_icache(void)                             \
{                                                                               \
    init_icache_stage();                                                        \
}                                                                               \
LOADER_STAGE_ENTRY void prefix##_stage_dcache(void)                             \
{                                                                               \
    init_dcache_stage();                                                        \
}                                                                               \
LOADER_STAGE_ENTRY void prefix##_stage_cache_enable(void)                       \
{                                                                               \
    init_cache_enable_stage();                                                  \
}                                                                               \
LOADER_STAGE_ENTRY u_int32_t prefix##_stage_pi(void)                            \
{                                                                               \
    return init_pi();                                                           \
}                                                                               \
LOADER_STAGE_ENTRY u_int32_t prefix##_stage_board(void)                         \
{                                                                               \
    return init_board();                                                        \
}                                                                               \
LOADER_STAGE_ENTRY u_int32_t *prefix##_stage_finish(void)                       \
{                                                                               \
    return (u_int32_t *)&VTSS_ICPU_CFG_CPU_SYSTEM_CTRL_GENERAL_CTRL;            \
}
