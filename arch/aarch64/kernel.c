/* Native AArch64 bring-up: PL011 UART on QEMU virt. */
typedef unsigned int u32;
#define UART0_BASE 0x09000000UL
#define UART_DR (*(volatile u32 *)(UART0_BASE + 0x00))
#define UART_FR (*(volatile u32 *)(UART0_BASE + 0x18))
#define UART_IBRD (*(volatile u32 *)(UART0_BASE + 0x24))
#define UART_FBRD (*(volatile u32 *)(UART0_BASE + 0x28))
#define UART_LCRH (*(volatile u32 *)(UART0_BASE + 0x2C))
#define UART_CR (*(volatile u32 *)(UART0_BASE + 0x30))
#define UART_IMSC (*(volatile u32 *)(UART0_BASE + 0x38))
static void uart_putc(char c) { while (UART_FR & (1u<<5)) {} UART_DR=(u32)c; }
static void uart_puts(const char *s) { while(*s){if(*s=='\n')uart_putc('\r');uart_putc(*s++);} }
void aarch64_main(void) {
 UART_CR=0; UART_IMSC=0;
 UART_IBRD=UART_IBRD; UART_FBRD=UART_FBRD;
 UART_LCRH=(3u<<5)|(1u<<4); UART_CR=(1u<<0)|(1u<<8)|(1u<<9);
 uart_puts("\nFlairOS AArch64: native kernel entered.\n");
 uart_puts("QEMU virt / Cortex-A72 / PL011 UART.\n");
 uart_puts("Next: vectors, MMU, GICv3, timer, framebuffer.\n");
 for(;;)__asm__ volatile("wfe");
}
