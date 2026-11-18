/* SPDX-License-Identifier: AGPL-3.0-only */
#ifndef FLAIROS_HAL_H
#define FLAIROS_HAL_H

#include "airos_types.h"

/*
 * Architecture boundary for CPU control and legacy port I/O.
 * Port I/O is optional: non-x86 platforms must return -1 without touching
 * hardware. Device drivers must not assume that these operations exist.
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

void hal_cpu_irq_enable(void);
void hal_cpu_irq_disable(void);
void hal_cpu_halt(void);

#endif
