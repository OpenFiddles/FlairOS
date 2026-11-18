/* SPDX-License-Identifier: AGPL-3.0-only */
/*
 * AArch64 CPU-control backend scaffold.
 * Platform interrupt-controller, timer, UART, MMU and device-tree support
 * must be implemented by the board/platform layer before ARM64 is bootable.
 */
#include "../../include/hal.h"

int hal_early_init(void) { return 0; }
const char *hal_arch_name(void) { return "aarch64"; }

/* AArch64 has no x86-style I/O port space. */
int hal_port_read8(u16 port, u8 *value) {
    (void)port; (void)value; return -1;
}
int hal_port_read16(u16 port, u16 *value) {
    (void)port; (void)value; return -1;
}
int hal_port_read32(u16 port, u32 *value) {
    (void)port; (void)value; return -1;
}
int hal_port_write8(u16 port, u8 value) {
    (void)port; (void)value; return -1;
}
int hal_port_write16(u16 port, u16 value) {
    (void)port; (void)value; return -1;
}
int hal_port_write32(u16 port, u32 value) {
    (void)port; (void)value; return -1;
}
void hal_io_wait(void) { }
void hal_cpu_irq_enable(void) { __asm__ volatile ("msr daifclr, #2" : : : "memory"); }
void hal_cpu_irq_disable(void) { __asm__ volatile ("msr daifset, #2" : : : "memory"); }
void hal_cpu_halt(void) { __asm__ volatile ("wfi"); }
