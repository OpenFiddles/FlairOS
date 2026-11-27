/* SPDX-License-Identifier: AGPL-3.0-only */
/* x86_64 Hardware Abstraction Layer */
#include "../../include/hal.h"

#define COM1 0x3F8u

static inline u8 inb(u16 port) {
    u8 v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outb(u16 port, u8 v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}
static inline u16 inw(u16 port) {
    u16 v;
    __asm__ volatile ("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outw(u16 port, u16 v) {
    __asm__ volatile ("outw %0, %1" : : "a"(v), "Nd"(port));
}
static inline u32 inl(u16 port) {
    u32 v;
    __asm__ volatile ("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outl(u16 port, u32 v) {
    __asm__ volatile ("outl %0, %1" : : "a"(v), "Nd"(port));
}

int hal_early_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
    return 0;
}

const char *hal_arch_name(void) { return "x86_64"; }

int hal_port_read8(u16 port, u8 *value) { if (!value) return -1; *value = inb(port); return 0; }
int hal_port_read16(u16 port, u16 *value) { if (!value) return -1; *value = inw(port); return 0; }
int hal_port_read32(u16 port, u32 *value) { if (!value) return -1; *value = inl(port); return 0; }
int hal_port_write8(u16 port, u8 value) { outb(port, value); return 0; }
int hal_port_write16(u16 port, u16 value) { outw(port, value); return 0; }
int hal_port_write32(u16 port, u32 value) { outl(port, value); return 0; }
void hal_io_wait(void) { outb(0x80, 0); }

u8 hal_mmio_read8(unsigned long addr) {
    u8 v = *(volatile u8 *)addr;
    __asm__ volatile ("" : : : "memory");
    return v;
}
void hal_mmio_write8(unsigned long addr, u8 value) {
    __asm__ volatile ("" : : : "memory");
    *(volatile u8 *)addr = value;
}
u32 hal_mmio_read32(unsigned long addr) {
    u32 v = *(volatile u32 *)addr;
    __asm__ volatile ("" : : : "memory");
    return v;
}
void hal_mmio_write32(unsigned long addr, u32 value) {
    __asm__ volatile ("" : : : "memory");
    *(volatile u32 *)addr = value;
}

void hal_serial_putc(char c) {
    while (!(inb(COM1 + 5) & 0x20u)) {}
    outb(COM1, (u8)c);
}
void hal_serial_puts(const char *s) {
    while (s && *s) {
        if (*s == '\n') hal_serial_putc('\r');
        hal_serial_putc(*s++);
    }
}

/* TSC/1024 counter, reported on the same 1 MHz scale as the counter unit the
   HAL has always exposed. Airnimation converts this to 100 Hz ticks. */
u32 hal_timer_freq_hz(void) { return 1000000u; }
u32 hal_timer_ticks(void) {
    u32 lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return (lo >> 10) | (hi << 22);
}

int hal_is_mobile_form_factor(void) {
#if defined(FLAIROS_DEVICE_PHONE) || defined(FLAIROS_DEVICE_TABLET)
    return 1;
#else
    return 0;
#endif
}

void hal_cpu_irq_enable(void) { __asm__ volatile ("sti" : : : "memory"); }
void hal_cpu_irq_disable(void) { __asm__ volatile ("cli" : : : "memory"); }
void hal_cpu_halt(void) { __asm__ volatile ("hlt"); }

void *memcpy(void *dst, const void *src, unsigned long n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}
void *memset(void *dst, int c, unsigned long n) {
    unsigned char *d = (unsigned char *)dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}
void *memmove(void *dst, const void *src, unsigned long n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else if (d > s) {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

#define COM2 0x2F8u
void hal_bridge_init(void) {
    outb(COM2 + 1, 0x00);
    outb(COM2 + 3, 0x80);
    outb(COM2 + 0, 0x03);
    outb(COM2 + 1, 0x00);
    outb(COM2 + 3, 0x03);
    outb(COM2 + 2, 0xC7);
    outb(COM2 + 4, 0x0B);
}
void hal_bridge_putc(char c) {
    for (int i = 0; i < 4096; i++) {
        if (inb(COM2 + 5) & 0x20u) { outb(COM2, (u8)c); return; }
    }
}
int hal_bridge_getc(void) {
    if (inb(COM2 + 5) & 0x01u) return (int)inb(COM2);
    return -1;
}

void hal_power_shutdown(int forced) {
    hal_serial_puts(forced ? "flairos64: forced hardware poweroff\n" : "flairos64: graceful shutdown\n");
    hal_cpu_irq_disable();
    outw(0x604u, 0x2000u);
    outw(0xB004u, 0x2000u);
    for (;;) hal_cpu_halt();
}
