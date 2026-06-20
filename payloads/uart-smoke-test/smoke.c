typedef unsigned char u8;
typedef unsigned int u32;

#define UART_BASE 0x70100000u
#define UART_THR  0u
#define UART_LSR  20u
#define UART_LSR_THRE 0x20u

static volatile u32 * const uart = (volatile u32 *)UART_BASE;

static __attribute__((always_inline)) inline void uart_putc(char value)
{
    if (value == '\n') {
        while ((uart[UART_LSR / 4] & UART_LSR_THRE) == 0u) {}
        uart[UART_THR / 4] = (u32)(u8)'\r';
    }
    while ((uart[UART_LSR / 4] & UART_LSR_THRE) == 0u) {}
    uart[UART_THR / 4] = (u32)(u8)value;
}

static __attribute__((always_inline)) inline void uart_puts(const char *text)
{
    while (*text != '\0') uart_putc(*text++);
}

__attribute__((section(".text.start"), noinline, used))
void _start(void)
{
    uart_puts("PMOSRAM SMOKE OK\n");
    __asm__ __volatile__("sync\n\tehb" ::: "memory");
}
