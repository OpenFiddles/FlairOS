/* SPDX-License-Identifier: AGPL-3.0-only */
#ifndef FLAIROS_HAL_H
#define FLAIROS_HAL_H

#include "airos_types.h"

/*
 * Architecture Hardware Abstraction Layer (HAL) for i386 and AArch64.
 * Port I/O is optional: non-x86 platforms return -1 without touching hardware.
 * MMIO, serial console, timer, and CPU control are portable across x86 and ARM64.
 */
int hal_early_init(void);
const char *hal_arch_name(void);

int hal_port_read8(u16 port, u8 *value);
int hal_port_read16(u16 port, u16 *value);
int hal_port_read32(u16 port, u32 *value);
int hal_port_write8(u16 port, u8 value);
int hal_port_write16(u16 port, u16 value);
int hal_port_write32(u16 port, u32 value);
void hal_io_wait(void);

/* Portable MMIO, UART console, and monotonic timer HAL. */
u8 hal_mmio_read8(unsigned long addr);
void hal_mmio_write8(unsigned long addr, u8 value);
u32 hal_mmio_read32(unsigned long addr);
void hal_mmio_write32(unsigned long addr, u32 value);

void hal_serial_putc(char c);
void hal_serial_puts(const char *s);
u32 hal_timer_freq_hz(void);
u32 hal_timer_ticks(void);
int hal_is_mobile_form_factor(void);

void hal_cpu_irq_enable(void);
void hal_cpu_irq_disable(void);
void hal_cpu_halt(void);

void hal_bridge_init(void);
void hal_bridge_putc(char c);
int hal_bridge_getc(void);

void hal_power_shutdown(int forced);

#endif
