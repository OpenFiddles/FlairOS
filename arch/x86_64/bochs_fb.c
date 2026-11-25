/* SPDX-License-Identifier: AGPL-3.0-only */
/* Bochs/QEMU StdVGA linear framebuffer setup for x86_64 */
#include <stdint.h>
#include "../../include/hal.h"

#define VBE_DISPI_IOPORT_INDEX 0x01CEu
#define VBE_DISPI_IOPORT_DATA  0x01CFu
#define VBE_DISPI_INDEX_ID     0x0000u
#define VBE_DISPI_INDEX_XRES   0x0001u
#define VBE_DISPI_INDEX_YRES   0x0002u
#define VBE_DISPI_INDEX_BPP    0x0003u
#define VBE_DISPI_INDEX_ENABLE 0x0004u

static void dispi_write(u16 idx, u16 val) {
    (void)hal_port_write16(VBE_DISPI_IOPORT_INDEX, idx);
    (void)hal_port_write16(VBE_DISPI_IOPORT_DATA, val);
}

static u16 dispi_read(u16 idx) {
    u16 v = 0;
    (void)hal_port_write16(VBE_DISPI_IOPORT_INDEX, idx);
    (void)hal_port_read16(VBE_DISPI_IOPORT_DATA, &v);
    return v;
}

static u32 pci_read32(u8 bus, u8 slot, u8 fn, u8 off) {
    u32 addr = (1u << 31) | ((u32)bus << 16) | ((u32)slot << 11) | ((u32)fn << 8) | (off & 0xFCu);
    u32 val = 0;
    (void)hal_port_write32(0x0CF8u, addr);
    (void)hal_port_read32(0x0CFCu, &val);
    return val;
}

uint64_t x86_64_bochs_fb_init(uint32_t width, uint32_t height) {
    u16 id = dispi_read(VBE_DISPI_INDEX_ID);
    if ((id & 0xFFF0u) != 0xB0C0u) return 0;

    uint64_t lfb = 0;
    for (u8 bus = 0; bus < 4 && !lfb; bus++) {
        for (u8 slot = 0; slot < 32; slot++) {
            u32 vd = pci_read32(bus, slot, 0, 0x00);
            if ((vd & 0xFFFFu) == 0x1234u && (vd >> 16) == 0x1111u) {
                u32 bar0 = pci_read32(bus, slot, 0, 0x10);
                lfb = (uint64_t)(bar0 & 0xFFFFFFF0u);
                break;
            }
        }
    }
    if (!lfb) return 0;

    dispi_write(VBE_DISPI_INDEX_ENABLE, 0x0000u);
    dispi_write(VBE_DISPI_INDEX_XRES, (u16)width);
    dispi_write(VBE_DISPI_INDEX_YRES, (u16)height);
    dispi_write(VBE_DISPI_INDEX_BPP, 32u);
    dispi_write(VBE_DISPI_INDEX_ENABLE, 0x0041u); /* ENABLE | LFB */
    return lfb;
}
