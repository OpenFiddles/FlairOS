/* SPDX-License-Identifier: AGPL-3.0-only */
/* i386 CPU and legacy PC I/O backend. */
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
void hal_cpu_irq_enable(void) { __asm__ volatile ("sti" : : : "memory"); }
void hal_cpu_irq_disable(void) { __asm__ volatile ("cli" : : : "memory"); }
void hal_cpu_halt(void) { __asm__ volatile ("hlt"); }
