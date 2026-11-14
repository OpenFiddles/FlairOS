#include "../../../include/drv_net.h"
#include "../../../include/drv.h"
#include "../../../include/hw.h"

/* NE2000 / RTL8029AS backend. v86 exposes this as PCI 10EC:8029 at I/O 0x300. */
#define PCI_ADDR 0xCF8
#define PCI_DATA 0xCFC
#define NE_VENDOR 0x10EC
#define NE_DEVICE 0x8029
#define NE_CMD 0x00
#define NE_PSTART 0x01
#define NE_PSTOP 0x02
#define NE_BNRY 0x03
#define NE_TPSR 0x04
#define NE_TBCR0 0x05
#define NE_TBCR1 0x06
#define NE_ISR 0x07
#define NE_RSAR0 0x08
#define NE_RSAR1 0x09
#define NE_RBCR0 0x0A
#define NE_RBCR1 0x0B
#define NE_RCR 0x0C
#define NE_TCR 0x0D
#define NE_DCR 0x0E
#define NE_IMR 0x0F
#define NE_DATA 0x10
#define NE_RESET 0x1F
#define NE_RX_START 0x4C
#define NE_RX_STOP 0x80
#define NE_TX_PAGE 0x40
#define NE_ISR_PTX 0x02
#define NE_ISR_TXE 0x08
#define NE_ISR_RDC 0x40
#define NE_MAX_PACKET 1518

struct ne2k_state {
    u16 io;
    u8 current;
    u8 mac[6];
};
static struct ne2k_state ne2k;
static struct net_device ne2k_device;

static u32 ne_pci_read(u8 bus, u8 slot, u8 fn, u8 offset) {
    u32 address = 0x80000000u | ((u32)bus << 16) |
                  ((u32)slot << 11) | ((u32)fn << 8) | (offset & 0xFCu);
    hw_outl(PCI_ADDR, address);
    return hw_inl(PCI_DATA);
}
static void ne_pci_write(u8 bus, u8 slot, u8 fn, u8 offset, u32 value) {
    u32 address = 0x80000000u | ((u32)bus << 16) |
                  ((u32)slot << 11) | ((u32)fn << 8) | (offset & 0xFCu);
    hw_outl(PCI_ADDR, address);
    hw_outl(PCI_DATA, value);
}
static int ne_find(u8 *bus_out, u8 *slot_out, u8 *fn_out) {
    for (u32 bus = 0; bus < 256; ++bus) {
        for (u32 slot = 0; slot < 32; ++slot) {
            for (u32 fn = 0; fn < 8; ++fn) {
                u32 id = ne_pci_read((u8)bus, (u8)slot, (u8)fn, 0);
                if (id == 0xFFFFFFFFu || (id & 0xFFFFu) == 0xFFFFu)
                    break;
                if ((id & 0xFFFFu) == NE_VENDOR && (id >> 16) == NE_DEVICE) {
                    *bus_out = (u8)bus;
                    *slot_out = (u8)slot;
                    *fn_out = (u8)fn;
                    return 0;
                }
                if (fn == 0 && !(ne_pci_read((u8)bus, (u8)slot, 0, 0x0C) & 0x00800000u))
                    break;
            }
        }
    }
    return -1;
}
static void ne_page0(void) { hw_outb(ne2k.io + NE_CMD, 0x22); }
static int ne_wait_rdc(void) {
    for (u32 i = 0; i < 100000u; ++i) {
        if (hw_inb(ne2k.io + NE_ISR) & NE_ISR_RDC) {
            hw_outb(ne2k.io + NE_ISR, NE_ISR_RDC);
            return 0;
        }
    }
    return -1;
}
static int ne_dma_read(u16 address, void *buffer, u16 length) {
    u8 *dst = (u8 *)buffer;
    u16 i = 0;
    if (!length) return 0;
    ne_page0();
    hw_outb(ne2k.io + NE_RSAR0, (u8)address);
    hw_outb(ne2k.io + NE_RSAR1, (u8)(address >> 8));
    hw_outb(ne2k.io + NE_RBCR0, (u8)length);
    hw_outb(ne2k.io + NE_RBCR1, (u8)(length >> 8));
    hw_outb(ne2k.io + NE_CMD, 0x0A);
    while (i < length) {
        u16 word = hw_inw(ne2k.io + NE_DATA);
        dst[i++] = (u8)word;
        if (i < length) dst[i++] = (u8)(word >> 8);
    }
    ne_page0();
    return ne_wait_rdc();
}
static int ne_dma_write(u16 address, const void *buffer, u16 length) {
    const u8 *src = (const u8 *)buffer;
    u16 i = 0;
    if (!length) return 0;
    ne_page0();
    hw_outb(ne2k.io + NE_RSAR0, (u8)address);
    hw_outb(ne2k.io + NE_RSAR1, (u8)(address >> 8));
    hw_outb(ne2k.io + NE_RBCR0, (u8)length);
    hw_outb(ne2k.io + NE_RBCR1, (u8)(length >> 8));
    hw_outb(ne2k.io + NE_CMD, 0x12);
    while (i < length) {
        u16 word = src[i++];
        if (i < length) word |= (u16)src[i++] << 8;
        hw_outw(ne2k.io + NE_DATA, word);
    }
    ne_page0();
    return ne_wait_rdc();
}
static int ne_ring_read(u16 address, void *buffer, u16 length) {
    u8 *dst = (u8 *)buffer;
    while (length) {
        u16 until_end = (u16)(0x8000u - address);
        u16 chunk = length < until_end ? length : until_end;
        if (ne_dma_read(address, dst, chunk) != 0) return -1;
        address = (u16)(address + chunk);
        dst += chunk;
        length = (u16)(length - chunk);
        if (address >= 0x8000u) address = (u16)(NE_RX_START << 8);
    }
    return 0;
}
static int ne_link(struct net_device *dev) {
    struct ne2k_state *s = (struct ne2k_state *)dev->private_data;
    return s->io != 0;
}
static int ne_send(struct net_device *dev, const void *frame, u16 length) {
    struct ne2k_state *s = (struct ne2k_state *)dev->private_data;
    u16 wire_length = length < 60 ? 60 : length;
    if (!s->io || length < 14 || length > NET_FRAME_MAX) return -1;
    if (ne_dma_write((u16)(NE_TX_PAGE << 8), frame, length) != 0) return -1;
    if (wire_length > length) {
        u8 zero[60] = {0};
        if (ne_dma_write((u16)((NE_TX_PAGE << 8) + length), zero,
                         (u16)(wire_length - length)) != 0) return -1;
    }
    ne_page0();
    hw_outb(s->io + NE_TPSR, NE_TX_PAGE);
    hw_outb(s->io + NE_TBCR0, (u8)wire_length);
    hw_outb(s->io + NE_TBCR1, (u8)(wire_length >> 8));
    hw_outb(s->io + NE_CMD, 0x26);
    for (u32 i = 0; i < 500000u; ++i) {
        u8 isr = hw_inb(s->io + NE_ISR);
        if (isr & (NE_ISR_PTX | NE_ISR_TXE)) {
            hw_outb(s->io + NE_ISR, (u8)(isr & (NE_ISR_PTX | NE_ISR_TXE)));
            return (isr & NE_ISR_PTX) ? 0 : -1;
        }
    }
    return -1;
}
static int ne_receive(struct net_device *dev, void *frame, u16 capacity) {
    struct ne2k_state *s = (struct ne2k_state *)dev->private_data;
    u8 header[4], next, boundary, current, status, start_page;
    u16 packet_size, frame_size, address;
    if (!s->io) return -1;
    ne_page0();
    boundary = hw_inb(s->io + NE_BNRY);
    hw_outb(s->io + NE_CMD, 0x62);
    current = hw_inb(s->io + 0x07);
    ne_page0();
    next = (u8)(boundary + 1u);
    if (next >= NE_RX_STOP) next = NE_RX_START;
    if (next == current) return 0;
    start_page = next;
    address = (u16)(start_page << 8);
    if (ne_ring_read(address, header, sizeof(header)) != 0) return -1;
    status = header[0];
    next = header[1];
    packet_size = (u16)header[2] | ((u16)header[3] << 8);
    if (next < NE_RX_START || next >= NE_RX_STOP ||
        packet_size < 18 || packet_size > NE_MAX_PACKET) {
        /* Recover the ring rather than trusting a corrupt hardware header. */
        ne_page0();
        hw_outb(s->io + NE_BNRY, NE_RX_STOP - 1u);
        return -1;
    }
    frame_size = (u16)(packet_size - 4u); /* strip Ethernet CRC */
    if (!(status & 0x01u) || frame_size > capacity) {
        ne_page0();
        hw_outb(s->io + NE_BNRY, (u8)(next == NE_RX_START ? NE_RX_STOP - 1u : next - 1u));
        return -1;
    }
    /* The packet starts immediately after its four-byte ring header. */
    address = (u16)((start_page << 8) + 4u);
    if (ne_ring_read(address, frame, frame_size) != 0) return -1;
    ne_page0();
    hw_outb(s->io + NE_BNRY, (u8)(next == NE_RX_START ? NE_RX_STOP - 1u : next - 1u));
    return (int)frame_size;
}
static const struct net_device_ops ne_ops = { ne_link, ne_send, ne_receive };

static int ne_init(void) {
    u8 bus, slot, fn;
    u32 bar, command;
    u8 prom[32];
    if (ne_find(&bus, &slot, &fn) != 0) return -1;
    bar = ne_pci_read(bus, slot, fn, 0x10);
    if (!(bar & 1u)) return -1;
    ne2k.io = (u16)(bar & 0xFFFCu);
    if (!ne2k.io || ne2k.io > 0xFFE0u) return -1;
    command = ne_pci_read(bus, slot, fn, 0x04);
    ne_pci_write(bus, slot, fn, 0x04, (command & 0xFFFFu) | 0x0005u);

    (void)hw_inb(ne2k.io + NE_RESET);
    hw_outb(ne2k.io + NE_RESET, 0xFF);
    for (u32 i = 0; i < 100000u; ++i) {
        if (hw_inb(ne2k.io + NE_ISR) & 0x80u) break;
    }
    hw_outb(ne2k.io + NE_CMD, 0x21);
    hw_outb(ne2k.io + NE_DCR, 0x49); /* 16-bit transfers, 8-byte FIFO */
    hw_outb(ne2k.io + NE_RBCR0, 0);
    hw_outb(ne2k.io + NE_RBCR1, 0);
    hw_outb(ne2k.io + NE_RCR, 0x20); /* monitor while PROM is read */
    hw_outb(ne2k.io + NE_TCR, 0x02); /* loopback during setup */
    hw_outb(ne2k.io + NE_PSTART, NE_RX_START);
    hw_outb(ne2k.io + NE_PSTOP, NE_RX_STOP);
    hw_outb(ne2k.io + NE_BNRY, NE_RX_START);
    hw_outb(ne2k.io + NE_TPSR, NE_TX_PAGE);
    hw_outb(ne2k.io + NE_ISR, 0xFF);
    hw_outb(ne2k.io + NE_IMR, 0);

    if (ne_dma_read(0, prom, sizeof(prom)) != 0) { ne2k.io = 0; return -1; }
    for (int i = 0; i < 6; ++i) ne2k.mac[i] = prom[i * 2];

    hw_outb(ne2k.io + NE_CMD, 0x61); /* page 1 */
    for (int i = 0; i < 6; ++i) hw_outb(ne2k.io + 1 + i, ne2k.mac[i]);
    hw_outb(ne2k.io + 7, NE_RX_START + 1u);
    for (int i = 0; i < 8; ++i) hw_outb(ne2k.io + 8 + i, 0);
    hw_outb(ne2k.io + NE_CMD, 0x22);
    hw_outb(ne2k.io + NE_RCR, 0x04); /* accept broadcast and directed packets */
    hw_outb(ne2k.io + NE_TCR, 0x00);
    hw_outb(ne2k.io + NE_ISR, 0xFF);
    hw_outb(ne2k.io + NE_IMR, 0); /* polling backend; no IRQ required */

    ne2k_device.name = "ne2000";
    for (int i = 0; i < 6; ++i) ne2k_device.mac[i] = ne2k.mac[i];
    ne2k_device.mtu = NET_MTU;
    ne2k_device.private_data = &ne2k;
    ne2k_device.ops = &ne_ops;
    return net_register(&ne2k_device);
}
static void ne_shutdown(void) {
    if (ne2k.io) {
        hw_outb(ne2k.io + NE_IMR, 0);
        hw_outb(ne2k.io + NE_CMD, 0x21);
    }
    ne2k.io = 0;
    net_unregister(&ne2k_device);
}
static const struct drv_ops ne_driver_ops = { ne_init, 0, ne_shutdown };
struct drv drv_ne2000 = {
    "ne2000", DRV_CLASS_NET, &ne_driver_ops, DRV_NO_IRQ, 0
};
