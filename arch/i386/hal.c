/* SPDX-License-Identifier: AGPL-3.0-only */
/* i386 CPU and legacy PC I/O + MMIO backend. */
#include "../../include/hal.h"

int hal_early_init(void) { return 0; }
const char *hal_arch_name(void) { return "i386"; }

int hal_port_read8(u16 port, u8 *value) {
    if (!value) return -1;
    __asm__ volatile ("inb %1, %0" : "=a"(*value) : "Nd"(port));
    return 0;
}
int hal_port_read16(u16 port, u16 *value) {
    if (!value) return -1;
    __asm__ volatile ("inw %1, %0" : "=a"(*value) : "Nd"(port));
    return 0;
}
int hal_port_read32(u16 port, u32 *value) {
    if (!value) return -1;
    __asm__ volatile ("inl %1, %0" : "=a"(*value) : "Nd"(port));
    return 0;
}
int hal_port_write8(u16 port, u8 value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
    return 0;
}
int hal_port_write16(u16 port, u16 value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
    return 0;
}
int hal_port_write32(u16 port, u32 value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
    return 0;
}
void hal_io_wait(void) { (void)hal_port_write8(0x80u, 0); }

u8 hal_mmio_read8(unsigned long addr) { return *(volatile u8 *)addr; }
void hal_mmio_write8(unsigned long addr, u8 value) { *(volatile u8 *)addr = value; }
u32 hal_mmio_read32(unsigned long addr) { return *(volatile u32 *)addr; }
void hal_mmio_write32(unsigned long addr, u32 value) { *(volatile u32 *)addr = value; }

void hal_serial_putc(char c) {
    u8 lsr = 0;
    for (int i = 0; i < 10000; i++) {
        (void)hal_port_read8(0x3F8u + 5u, &lsr);
        if (lsr & 0x20u) break;
    }
    (void)hal_port_write8(0x3F8u, (u8)c);
}
void hal_serial_puts(const char *s) {
    while (s && *s) {
        if (*s == '\n') hal_serial_putc('\r');
        hal_serial_putc(*s++);
    }
}
u32 hal_timer_freq_hz(void) { return 100u; }
u32 hal_timer_ticks(void) { return 0u; }
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
