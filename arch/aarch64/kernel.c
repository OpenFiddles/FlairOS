/* Native AArch64 bring-up: PL011 UART on QEMU virt. */
typedef unsigned int u32;
#define UART0_BASE 0x09000000UL
#define UART_DR   (*(volatile u32 *)(UART0_BASE + 0x00))
#define UART_FR   (*(volatile u32 *)(UART0_BASE + 0x18))
#define UART_IBRD (*(volatile u32 *)(UART0_BASE + 0x24))
#define UART_FBRD (*(volatile u32 *)(UART0_BASE + 0x28))
#define UART_LCRH (*(volatile u32 *)(UART0_BASE + 0x2C))
#define UART_CR   (*(volatile u32 *)(UART0_BASE + 0x30))
#define UART_IFLS (*(volatile u32 *)(UART0_BASE + 0x34))
#define UART_IMSC (*(volatile u32 *)(UART0_BASE + 0x38))
#define UART_ICR  (*(volatile u32 *)(UART0_BASE + 0x44))

static void uart_putc(char c) {
    while (UART_FR & (1u << 5)) { }
    UART_DR = (u32)c;
}

static void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}

static void uart_init(void) {
    UART_CR = 0;
    UART_IMSC = 0;
    UART_ICR = 0x7ffu;
    UART_IBRD = 13u;
    UART_FBRD = 1u;
    UART_LCRH = (3u << 5) | (1u << 4);
    UART_IFLS = 0;
    UART_CR = (1u << 0) | (1u << 8) | (1u << 9);
}

void aarch64_main(void) {
    uart_init();
    uart_puts("\nFlairOS AArch64: native kernel entered.\n");
    uart_puts("QEMU virt / Cortex-A72 / PL011 UART.\n");
    uart_puts("Build target: aarch64_kernel.\n");
    uart_puts("Next: exception vectors, MMU, GIC, timer, framebuffer.\n");
    for (;;) __asm__ volatile("wfe");
}
