/* SPDX-License-Identifier: AGPL-3.0-only */
/* AArch64 Hardware Abstraction Layer (PL011 UART, ARM Generic Timer, MMIO, CPU). */
#include "../../include/hal.h"

#define PL011_BASE   0x09000000UL
#define PL011_DR     (PL011_BASE + 0x00UL)
#define PL011_FR     (PL011_BASE + 0x18UL)
#define PL011_CR     (PL011_BASE + 0x30UL)
#define PL011_FR_RXFE (1u << 4)
#define PL011_FR_TXFF (1u << 5)

int hal_early_init(void) {
    /* Enable FP/SIMD (CPACR_EL1.FPEN = 0b11) so freestanding C never traps on NEON. */
    unsigned long cpacr = (3UL << 20);
    __asm__ volatile ("msr cpacr_el1, %0; isb" : : "r"(cpacr));
    return 0;
}

const char *hal_arch_name(void) { return "aarch64"; }

int hal_port_read8(u16 port, u8 *value) { (void)port; (void)value; return -1; }
int hal_port_read16(u16 port, u16 *value) { (void)port; (void)value; return -1; }
int hal_port_read32(u16 port, u32 *value) { (void)port; (void)value; return -1; }
int hal_port_write8(u16 port, u8 value) { (void)port; (void)value; return -1; }
int hal_port_write16(u16 port, u16 value) { (void)port; (void)value; return -1; }
int hal_port_write32(u16 port, u32 value) { (void)port; (void)value; return -1; }
void hal_io_wait(void) { __asm__ volatile ("yield"); }

u8 hal_mmio_read8(unsigned long addr) {
    u8 v = *(volatile u8 *)addr;
    __asm__ volatile ("dmb ld" : : : "memory");
    return v;
}
void hal_mmio_write8(unsigned long addr, u8 value) {
    __asm__ volatile ("dmb st" : : : "memory");
    *(volatile u8 *)addr = value;
}
u32 hal_mmio_read32(unsigned long addr) {
    u32 v = *(volatile u32 *)addr;
    __asm__ volatile ("dmb ld" : : : "memory");
    return v;
}
void hal_mmio_write32(unsigned long addr, u32 value) {
    __asm__ volatile ("dmb st" : : : "memory");
    *(volatile u32 *)addr = value;
}

void hal_serial_putc(char c) {
    while (hal_mmio_read32(PL011_FR) & PL011_FR_TXFF) {}
    hal_mmio_write32(PL011_DR, (u32)(u8)c);
}
void hal_serial_puts(const char *s) {
    while (s && *s) {
        if (*s == '\n') hal_serial_putc('\r');
        hal_serial_putc(*s++);
    }
}

u32 hal_timer_freq_hz(void) {
    unsigned long f = 0;
    __asm__ volatile ("mrs %0, cntfrq_el0" : "=r"(f));
    return (u32)f;
}
u32 hal_timer_ticks(void) {
    unsigned long c = 0;
    __asm__ volatile ("mrs %0, cntpct_el0" : "=r"(c));
    return (u32)c;
}

int hal_is_mobile_form_factor(void) {
#if defined(FLAIROS_DEVICE_PHONE) || defined(FLAIROS_DEVICE_TABLET)
    return 1;
#else
    return 0;
#endif
}

void hal_cpu_irq_enable(void) { __asm__ volatile ("msr daifclr, #2" : : : "memory"); }
void hal_cpu_irq_disable(void) { __asm__ volatile ("msr daifset, #2" : : : "memory"); }
void hal_cpu_halt(void) { __asm__ volatile ("wfi"); }

void *memcpy(void *dst, const void *src, unsigned long n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *p = (const unsigned char *)src;
    while (n--) *d++ = *p++;
    return dst;
}

void *memset(void *dst, int c, unsigned long n) {
    unsigned char *d = (unsigned char *)dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}
void *memmove(void *dst, const void *src, unsigned long n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *p = (const unsigned char *)src;
    if (d < p) {
        while (n--) *d++ = *p++;
    } else if (d > p) {
        d += n; p += n;
        while (n--) *--d = *--p;
    }
    return dst;
}

void hal_bridge_init(void) {
    u32 cr = hal_mmio_read32(PL011_CR);
    if ((cr & 0x301u) != 0x301u) {
        hal_mmio_write32(PL011_CR, cr | 0x301u); /* UARTEN | TXE | RXE */
    }
}
void hal_bridge_putc(char c) {
    for (int i = 0; i < 4096; i++) {
        if (!(hal_mmio_read32(PL011_FR) & PL011_FR_TXFF)) {
            hal_mmio_write32(PL011_DR, (u32)(u8)c);
            return;
        }
    }
}
int hal_bridge_getc(void) {
    if (!(hal_mmio_read32(PL011_FR) & PL011_FR_RXFE)) {
        return (int)(hal_mmio_read32(PL011_DR) & 0xFFu);
    }
    return -1;
}

void hal_power_shutdown(int forced) {
    hal_serial_puts(forced ? "flairos64: forced hardware poweroff\n" : "flairos64: graceful shutdown\n");
    hal_cpu_irq_disable();
    register unsigned long x0 __asm__("x0") = 0x84000008UL; /* PSCI SYSTEM_OFF */
    __asm__ volatile ("hvc #0" : "+r"(x0) : : "memory");
    for (;;) hal_cpu_halt();
}
