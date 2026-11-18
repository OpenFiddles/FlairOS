/* FlareOS kernel: VGA console, COM1 mirror, legacy PC drivers and syscall entry. */

#include "textile.h"
#include "../include/vbe.h"
#include "../include/airgraph.h"
#include "../include/pmm.h"
#include "../include/drv_net.h"
#include "../include/boot.h"
#include "../include/setup.h"
#include "../include/desktop_gfx.h"
#include "../include/hwinfo.h"
#include "../include/io.h"
#include "../include/hal.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int i32;

#define VGA_COLS 80
#define VGA_ROWS 25
#define VGA_MEM ((volatile u16 *)0xB8000)
#define COM1 0x3F8
#define ATTR_TEXT 0x07
#define ATTR_OK 0x0A
#define ATTR_ERR 0x0C
#define ATTR_SQR 0x0B
#define PCI_ADDR 0xCF8
#define PCI_DATA 0xCFC
#define RTL_VENDOR 0x10EC
#define RTL_DEVICE 0x8139
#define ANIM_SLOTS 16
#define PCI_SLOT_MAX 32

/* Legacy PC I/O compatibility helpers; implementation lives in arch/<arch>. */
static inline void outb(u16 port, u8 value) { (void)hal_port_write8(port, value); }
static inline u8 inb(u16 port) {
    u8 value = 0xFFu; (void)hal_port_read8(port, &value); return value;
}
static inline void outl(u16 port, u32 value) { (void)hal_port_write32(port, value); }
static inline u32 inl(u16 port) {
    u32 value = 0xFFFFFFFFu; (void)hal_port_read32(port, &value); return value;
}
static inline void io_wait(void) { hal_io_wait(); }
static inline void irq_on(void) { hal_cpu_irq_enable(); }
static inline void irq_off(void) { hal_cpu_irq_disable(); }
static inline void halt(void) { hal_cpu_halt(); }
