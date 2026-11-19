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
#define DISPI_ID_MASK 0xFFF0
#define DISPI_ENABLED 0x01
#define DISPI_LFB 0x40
#define DISPI_NOCLEAR 0x80

#define MIN_W 320
#define MIN_H 200
#define HARD_MAX_W 3840
#define HARD_MAX_H 2160

#define PCI_ADDR 0x0CF8
#define PCI_DATA 0x0CFC
#define PCI_COMMAND 0x04
#define PCI_COMMAND_IO 0x0001
#define PCI_COMMAND_MEMORY 0x0002
#define PCI_COMMAND_DISPLAY (PCI_COMMAND_IO | PCI_COMMAND_MEMORY)
#define DISPLAY_VENDOR 0x1234
#define DISPLAY_DEVICE 0x1111

/* Legacy ISA Bochs VGA and v86's generic VGA expose the BGA LFB here.
   PCI variants MUST use BAR0 instead; never substitute this for a bad BAR. */
#define DISPI_ISA_LFB 0xE0000000u

static const char *last_error = "not initialized";

static void dispi_write(uint16_t index, uint16_t value) {
    hw_outw(DISPI_INDEX_PORT, index);
    hw_outw(DISPI_DATA_PORT, value);
}

static uint16_t dispi_read(uint16_t index) {
    hw_outw(DISPI_INDEX_PORT, index);
    return hw_inw(DISPI_DATA_PORT);
}

static uint32_t pci_read32(uint8_t bus, uint8_t device,
                           uint8_t function, uint32_t off) {
    uint32_t address = 0x80000000u | ((uint32_t)bus << 16) |
                       ((uint32_t)device << 11) |
                       ((uint32_t)function << 8) | (off & 0xFCu);
    hw_outl(PCI_ADDR, address);
    return hw_inl(PCI_DATA);
}

static void pci_write32(uint8_t bus, uint8_t device,
                        uint8_t function, uint32_t off, uint32_t value) {
    uint32_t address = 0x80000000u | ((uint32_t)bus << 16) |
                       ((uint32_t)device << 11) |
                       ((uint32_t)function << 8) | (off & 0xFCu);
    hw_outl(PCI_ADDR, address);
    hw_outl(PCI_DATA, value);
}

/* Search every PCI bus and advertised function: a display adapter can sit
   behind a bridge or be a non-zero function on multifunction hardware. */
static int find_display(uint8_t *bus_out, uint8_t *device_out,
                        uint8_t *function_out, uint32_t *lfb) {
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t device = 0; device < 32; ++device) {
            uint32_t id0 = pci_read32((uint8_t)bus, device, 0, 0x00);
            uint8_t functions;
            if ((id0 & 0xFFFFu) == 0xFFFFu)
                continue;
            functions = (pci_read32((uint8_t)bus, device, 0, 0x0C) &
                         (1u << 23)) ? 8 : 1;
            for (uint8_t function = 0; function < functions; ++function) {
                uint32_t id = function ? pci_read32((uint8_t)bus, device,
                                                     function, 0x00) : id0;
                uint32_t bar;
                if ((id & 0xFFFFu) != DISPLAY_VENDOR ||
                    (id >> 16) != DISPLAY_DEVICE)
                    continue;

                bar = pci_read32((uint8_t)bus, device, function, 0x10);
                /* BAR0 must be an assigned 32-bit memory BAR. */
                if ((bar & 1u) || ((bar >> 1) & 3u) == 2u ||
                    !(bar & 0xFFFFFFF0u)) {
                    last_error = "Bochs PCI display has an invalid BAR0";
                    return -1;
                }
                *bus_out = (uint8_t)bus;
                *device_out = device;
                *function_out = function;
                *lfb = bar & 0xFFFFFFF0u;
                return 1;
            }
        }
    }
    return 0;
}

int vbe_init(struct fb *f, int32_t width, int32_t height) {
    uint8_t bus = 0, device = 0, function = 0;
    uint32_t lfb = 0, command;
    uint16_t video_memory, actual_w, actual_h, actual_bpp;
    uint16_t virtual_w, virtual_h;
    uint32_t required_bytes;
    int pci_display;

    last_error = "invalid framebuffer request";
    if (!f || width < MIN_W || height < MIN_H)
        return 0;

    if (width > HARD_MAX_W) width = HARD_MAX_W;
    if (height > HARD_MAX_H) height = HARD_MAX_H;

    pci_display = find_display(&bus, &device, &function, &lfb);
    if (pci_display < 0)
        return 0;

    if (pci_display) {
        /*
         * PCI Bochs/QEMU VGA uses BAR0 for the framebuffer and legacy I/O
         * ports for DISPI. Enable both decoders before probing the ports.
         * Write only the command halfword; status bits are W1C.
         */
        command = pci_read32(bus, device, function, PCI_COMMAND) & 0xFFFFu;
        if ((command & PCI_COMMAND_DISPLAY) != PCI_COMMAND_DISPLAY)
            pci_write32(bus, device, function, PCI_COMMAND,
                        command | PCI_COMMAND_DISPLAY);
    } else {
        /*
         * ISA-style Bochs VGA (also used by v86's generic VGA device) has no
         * PCI BAR. Its documented legacy LFB aperture is 0xE0000000. Accept
         * it only after the DISPI identity register confirms the adapter.
         */
        if ((dispi_read(DISPI_ID) & DISPI_ID_MASK) != DISPI_ID_MIN) {
            last_error = "no Bochs DISPI adapter found";
            return 0;
        }
        lfb = DISPI_ISA_LFB;
    }

    if ((dispi_read(DISPI_ID) & DISPI_ID_MASK) != DISPI_ID_MIN) {
        last_error = "Bochs DISPI identity check failed";
        return 0;
    }

    video_memory = dispi_read(DISPI_VIDEO_MEMORY_64K);
    if (!video_memory) {
        last_error = "display reports no video memory";
        return 0;
    }
    required_bytes = (uint32_t)width * (uint32_t)height * 4u;
    if (required_bytes > (uint32_t)video_memory * 65536u) {
        last_error = "requested display mode exceeds video memory";
        return 0;
    }

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

    if (actual_w != (uint16_t)width || actual_h != (uint16_t)height ||
        actual_bpp != 32 || virtual_w < actual_w || virtual_h < actual_h ||
        (uint32_t)virtual_w > 0x3FFFFFFFu / 4u ||
        (uint32_t)virtual_w * (uint32_t)virtual_h * 4u >
            (uint32_t)video_memory * 65536u) {
        dispi_write(DISPI_ENABLE, 0);
        last_error = "display mode readback or framebuffer size validation failed";
        return 0;
    }

    fb_init(f, (void *)(uintptr_t)lfb, (int32_t)actual_w,
            (int32_t)actual_h, (uint32_t)virtual_w * 4u);
    if (!fb_is_valid(f)) {
        dispi_write(DISPI_ENABLE, 0);
        last_error = "framebuffer descriptor validation failed";
        return 0;
    }

    last_error = pci_display ? "PCI Bochs/QEMU DISPI" : "ISA Bochs DISPI";
    return 1;
}

const char *vbe_last_error(void) {
    return last_error;
}

void vbe_text_mode(void) {
    dispi_write(DISPI_ENABLE, 0);
}
