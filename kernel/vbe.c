#include "../include/vbe.h"
#include "../include/hw.h"

#define DISPI_INDEX_PORT 0x01CE
#define DISPI_DATA_PORT 0x01CF

#define DISPI_ID 0x0
#define DISPI_XRES 0x1
#define DISPI_YRES 0x2
#define DISPI_BPP 0x3
#define DISPI_ENABLE 0x4
#define DISPI_VIRT_WIDTH 0x6
#define DISPI_VIRT_HEIGHT 0x7

#define DISPI_ID_MIN 0xB0C0
#define DISPI_ENABLED 0x01
#define DISPI_LFB 0x40

#define MAX_W 1024
#define MAX_H 768

#define PCI_ADDR 0xCF8
#define PCI_DATA 0xCFC
#define DISPLAY_VENDOR 0x1234
#define DISPLAY_DEVICE 0x1111

static void dispi_write(uint16_t index, uint16_t value) {
    hw_outw(DISPI_INDEX_PORT, index);
    hw_outw(DISPI_DATA_PORT, value);
}

static uint16_t dispi_read(uint16_t index) {
    hw_outw(DISPI_INDEX_PORT, index);
    return hw_inw(DISPI_DATA_PORT);
}

static uint32_t pci_read32(uint32_t slot, uint32_t off) {
    hw_outl(PCI_ADDR, 0x80000000u | (slot << 11) | (off & 0xFCu));
    return hw_inl(PCI_DATA);
}

static int find_display(uint32_t *lfb) {
    for (uint32_t slot = 0; slot < 32; slot++) {
        uint32_t id = pci_read32(slot, 0x00);
        if ((id & 0xFFFFu) == DISPLAY_VENDOR && (id >> 16) == DISPLAY_DEVICE) {
            *lfb = pci_read32(slot, 0x10) & 0xFFFFFFF0u;
            return *lfb != 0;
        }
    }
    return 0;
}

int vbe_init(struct fb *f, int32_t width, int32_t height) {
    uint32_t lfb;
    if (width <= 0 || height <= 0 || width > MAX_W || height > MAX_H)
        return 0;
    if (!find_display(&lfb))
        return 0;
    if (dispi_read(DISPI_ID) < DISPI_ID_MIN)
        return 0;
    dispi_write(DISPI_ENABLE, 0);
    dispi_write(DISPI_XRES, (uint16_t)width);
    dispi_write(DISPI_YRES, (uint16_t)height);
    dispi_write(DISPI_BPP, 32);
    dispi_write(DISPI_VIRT_WIDTH, (uint16_t)width);
    dispi_write(DISPI_VIRT_HEIGHT, (uint16_t)height);
    dispi_write(DISPI_ENABLE, DISPI_ENABLED | DISPI_LFB);
    if (dispi_read(DISPI_XRES) != width || dispi_read(DISPI_YRES) != height || dispi_read(DISPI_BPP) != 32) {
        dispi_write(DISPI_ENABLE, 0);
        return 0;
    }
    fb_init(f, (void *)(uintptr_t)lfb, width, height, (uint32_t)width * 4u);
    return 1;
}

void vbe_text_mode(void) {
    dispi_write(DISPI_ENABLE, 0);
}
