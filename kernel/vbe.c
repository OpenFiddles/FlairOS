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
#define DISPI_X_OFFSET 0x8
#define DISPI_Y_OFFSET 0x9
#define DISPI_VIDEO_MEMORY_64K 0xA

#define DISPI_ID_MIN 0xB0C0
#define DISPI_ENABLED 0x01
#define DISPI_LFB 0x40
#define DISPI_NOCLEAR 0x80

#define MIN_W 320
#define MIN_H 200
#define HARD_MAX_W 2048
#define HARD_MAX_H 1536

#define PCI_ADDR 0xCF8
#define PCI_DATA 0xCFC
#define PCI_COMMAND 0x04
#define PCI_COMMAND_MEMORY 0x0002
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

static void pci_write32(uint32_t slot, uint32_t off, uint32_t value) {
    hw_outl(PCI_ADDR, 0x80000000u | (slot << 11) | (off & 0xFCu));
    hw_outl(PCI_DATA, value);
}

static int find_display(uint32_t *slot_out, uint32_t *lfb) {
    for (uint32_t slot = 0; slot < 32; ++slot) {
        uint32_t id = pci_read32(slot, 0x00);
        if ((id & 0xFFFFu) == DISPLAY_VENDOR &&
            (id >> 16) == DISPLAY_DEVICE) {
            uint32_t bar = pci_read32(slot, 0x10);
            if ((bar & 1u) || !(bar & 0xFFFFFFF0u))
                return 0;
            *slot_out = slot;
            *lfb = bar & 0xFFFFFFF0u;
            return 1;
        }
    }
    return 0;
}

int vbe_init(struct fb *f, int32_t width, int32_t height) {
    uint32_t slot, lfb, command;
    uint16_t video_memory, actual_w, actual_h, actual_bpp;
    uint16_t virtual_w, virtual_h;
    uint32_t required_bytes;

    if (!f || width < MIN_W || height < MIN_H ||
        !find_display(&slot, &lfb) ||
        dispi_read(DISPI_ID) < DISPI_ID_MIN)
        return 0;

    /* Bound the request before multiplying dimensions or programming hardware. */
    if (width > HARD_MAX_W) width = HARD_MAX_W;
    if (height > HARD_MAX_H) height = HARD_MAX_H;

    /* Register 0xA reports framebuffer memory in 64 KiB units. */
    video_memory = dispi_read(DISPI_VIDEO_MEMORY_64K);
    if (!video_memory)
        return 0;
    required_bytes = (uint32_t)width * (uint32_t)height * 4u;
    if (required_bytes > (uint32_t)video_memory * 65536u)
        return 0;

    /* Write only the PCI command halfword; status bits are write-one-to-clear. */
    command = pci_read32(slot, PCI_COMMAND) & 0xFFFFu;
    if ((command & PCI_COMMAND_MEMORY) == 0)
        pci_write32(slot, PCI_COMMAND, command | PCI_COMMAND_MEMORY);

    dispi_write(DISPI_ENABLE, 0);
    dispi_write(DISPI_XRES, (uint16_t)width);
    dispi_write(DISPI_YRES, (uint16_t)height);
    dispi_write(DISPI_BPP, 32);
    dispi_write(DISPI_VIRT_WIDTH, (uint16_t)width);
    dispi_write(DISPI_X_OFFSET, 0);
    dispi_write(DISPI_Y_OFFSET, 0);
    dispi_write(DISPI_VIRT_HEIGHT, (uint16_t)height);
    dispi_write(DISPI_ENABLE, DISPI_ENABLED | DISPI_LFB | DISPI_NOCLEAR);

    actual_w = dispi_read(DISPI_XRES);
    actual_h = dispi_read(DISPI_YRES);
    actual_bpp = dispi_read(DISPI_BPP);
    virtual_w = dispi_read(DISPI_VIRT_WIDTH);
    virtual_h = dispi_read(DISPI_VIRT_HEIGHT);

    /* Reject silent mode changes, undersized virtual surfaces, and bad pitch. */
    if (actual_w != (uint16_t)width || actual_h != (uint16_t)height ||
        actual_bpp != 32 || virtual_w < actual_w || virtual_h < actual_h ||
        (uint32_t)virtual_w > 0x3FFFFFFFu / 4u ||
        (uint32_t)virtual_w * (uint32_t)virtual_h * 4u >
            (uint32_t)video_memory * 65536u) {
        dispi_write(DISPI_ENABLE, 0);
        return 0;
    }

    fb_init(f, (void *)(uintptr_t)lfb, (int32_t)actual_w,
            (int32_t)actual_h, (uint32_t)virtual_w * 4u);
    if (!fb_is_valid(f)) {
        dispi_write(DISPI_ENABLE, 0);
        return 0;
    }
    return 1;
}

void vbe_text_mode(void) {
    dispi_write(DISPI_ENABLE, 0);
}
