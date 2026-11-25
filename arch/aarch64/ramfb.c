/* SPDX-License-Identifier: AGPL-3.0-only */
/* QEMU virt fw_cfg + ramfb linear framebuffer setup for AArch64 */
#include <stdint.h>
#include "../../include/hal.h"

#define FW_CFG_BASE      0x09020000UL
#define FW_CFG_DATA8     (FW_CFG_BASE + 0x00UL)
#define FW_CFG_CTL16     (FW_CFG_BASE + 0x08UL)
#define FW_CFG_DMA64     (FW_CFG_BASE + 0x10UL)

#define FW_CFG_FILE_DIR  0x0019u
#define FW_CFG_DMA_ERROR (1u << 0)
#define FW_CFG_DMA_READ  (1u << 1)
#define FW_CFG_DMA_SKIP  (1u << 2)
#define FW_CFG_DMA_SEL   (1u << 3)
#define FW_CFG_DMA_WRITE (1u << 4)

#define DRM_FORMAT_XRGB8888 0x34325258u /* 'XR24' */

struct fw_cfg_dma_access {
    uint32_t control;
    uint32_t length;
    uint64_t address;
} __attribute__((packed));

struct fw_cfg_file {
    uint32_t size;
    uint16_t select;
    uint16_t reserved;
    char name[56];
} __attribute__((packed));

struct ramfb_cfg {
    uint64_t addr;
    uint32_t fourcc;
    uint32_t flags;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
} __attribute__((packed));

static inline uint16_t bswap16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
static inline uint32_t bswap32(uint32_t v) {
    return ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) |
           ((v & 0x00FF0000u) >> 8)  | ((v & 0xFF000000u) >> 24);
}
static inline uint64_t bswap64(uint64_t v) {
    return ((uint64_t)bswap32((uint32_t)v) << 32) | (uint64_t)bswap32((uint32_t)(v >> 32));
}

static int str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a++ != *b++) return 0; }
    return *a == *b;
}

static void fw_cfg_dma_xfer(uint16_t select, int do_sel, uint32_t dir, void *buf, uint32_t len) {
    static volatile struct fw_cfg_dma_access acc;
    uint32_t ctl = dir | (do_sel ? (((uint32_t)select << 16) | FW_CFG_DMA_SEL) : 0u);
    acc.control = bswap32(ctl);
    acc.length = bswap32(len);
    acc.address = bswap64((uint64_t)(uintptr_t)buf);
    __asm__ volatile ("dsb sy" : : : "memory");
    *(volatile uint64_t *)FW_CFG_DMA64 = bswap64((uint64_t)(uintptr_t)&acc);
    __asm__ volatile ("dsb sy" : : : "memory");
    while (bswap32(acc.control) & ~FW_CFG_DMA_ERROR) {}
}

int aarch64_ramfb_init(uint64_t fb_phys, uint32_t width, uint32_t height) {
    uint32_t count_be = 0;
    struct fw_cfg_file file;
    struct ramfb_cfg cfg;

    fw_cfg_dma_xfer(FW_CFG_FILE_DIR, 1, FW_CFG_DMA_READ, &count_be, sizeof(count_be));
    uint32_t count = bswap32(count_be);
    if (count > 256u) count = 256u;

    for (uint32_t i = 0; i < count; i++) {
        fw_cfg_dma_xfer(0, 0, FW_CFG_DMA_READ, &file, sizeof(file));
        if (str_eq(file.name, "etc/ramfb")) {
            uint16_t sel = bswap16(file.select);
            cfg.addr = bswap64(fb_phys);
            cfg.fourcc = bswap32(DRM_FORMAT_XRGB8888);
            cfg.flags = 0;
            cfg.width = bswap32(width);
            cfg.height = bswap32(height);
            cfg.stride = bswap32(width * 4u);
            fw_cfg_dma_xfer(sel, 1, FW_CFG_DMA_WRITE, &cfg, sizeof(cfg));
            return 0;
        }
    }
    return -1;
}
