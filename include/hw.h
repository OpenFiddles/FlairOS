#ifndef AIROS_HW_H
#define AIROS_HW_H

#include "airos_types.h"
#include "hal.h"

/* Compatibility wrappers for legacy PC drivers; new code should use HAL. */
static inline void hw_outb(u16 port, u8 value) { (void)hal_port_write8(port, value); }
static inline u8 hw_inb(u16 port) {
    u8 value = 0xFFu;
    (void)hal_port_read8(port, &value);
    return value;
}
static inline void hw_outw(u16 port, u16 value) { (void)hal_port_write16(port, value); }
static inline u16 hw_inw(u16 port) {
    u16 value = 0xFFFFu;
    (void)hal_port_read16(port, &value);
    return value;
}
static inline void hw_outl(u16 port, u32 value) { (void)hal_port_write32(port, value); }
static inline u32 hw_inl(u16 port) {
    u32 value = 0xFFFFFFFFu;
    (void)hal_port_read32(port, &value);
    return value;
}
static inline void hw_io_wait(void) { hal_io_wait(); }
static inline void hw_irq_enable(void) { hal_cpu_irq_enable(); }
static inline void hw_irq_disable(void) { hal_cpu_irq_disable(); }
static inline void hw_halt(void) { hal_cpu_halt(); }

#endif
