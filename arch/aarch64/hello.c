/* arch/aarch64/hello.c - PL011 serial "hello" for the QEMU virt board */
#define UART_DR ((volatile unsigned int *)0x09000000UL)
#define UART_FR ((volatile unsigned int *)0x09000018UL)
#define UART_TXFF (1u << 5)

static void putc(char c)
{
    while (*UART_FR & UART_TXFF)
        ;
    *UART_DR = (unsigned char)c;
}

static void puts(const char *s)
{
    while (*s) {
        if (*s == '\n')
            putc('\r');
        putc(*s++);
    }
}

void kmain(void)
{
    puts("AirOS ARM: hello from aarch64 serial\n");
    for (;;)
        __asm__ volatile("wfe");
}
