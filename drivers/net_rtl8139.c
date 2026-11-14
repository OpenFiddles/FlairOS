#include "../include/drv_net.h"
#include "../include/drv.h"
#include "../include/hw.h"

/* RTL8139 hardware backend. Protocols never depend on these registers. */
#define PCI_ADDR 0xCF8
#define PCI_DATA 0xCFC
#define PCI_VENDOR_REALTEK 0x10EC
#define PCI_DEVICE_RTL8139 0x8139
#define RTL_MAC0 0x00
#define RTL_RBSTART 0x30
#define RTL_TSD0 0x10
#define RTL_TSAD0 0x20
#define RTL_CR 0x37
#define RTL_CAPR 0x38
#define RTL_IMR 0x3C
#define RTL_ISR 0x3E
#define RTL_TCR 0x40
#define RTL_RCR 0x44
#define RTL_CONFIG1 0x52
#define RTL_MSR 0x58
#define RX_RING_SIZE 8192u
#define RX_EXTRA 2048u
#define TX_SLOTS 4u
#define TX_BUFFER_SIZE 1536u
#define RX_OK 0x0001u
#define RX_EMPTY 0x01u
#define TX_OK 0x00008000u
#define TX_ERROR 0x00004000u

struct rtl8139_state {
    u16 io;
    u32 rx_offset;
    u32 tx_slot;
    u8 rx_ring[RX_RING_SIZE + RX_EXTRA + 16] __attribute__((aligned(16)));
    u8 tx_buffers[TX_SLOTS][TX_BUFFER_SIZE] __attribute__((aligned(16)));
};

static struct rtl8139_state rtl;
static struct net_device rtl_device;

static u32 pci_read(u8 bus, u8 slot, u8 function, u8 offset) {
    u32 address = 0x80000000u | ((u32)bus << 16) |
                  ((u32)slot << 11) | ((u32)function << 8) |
                  (offset & 0xFCu);
    hw_outl(PCI_ADDR, address);
    return hw_inl(PCI_DATA);
}

static int rtl_find(u8 *found_bus, u8 *found_slot, u8 *found_fn) {
    for (u32 bus = 0; bus < 256; ++bus) {
        for (u32 slot = 0; slot < 32; ++slot) {
            u32 id = pci_read((u8)bus, (u8)slot, 0, 0);
            if (id == 0xFFFFFFFFu || (id & 0xFFFFu) == 0xFFFFu)
                continue;
            if ((id & 0xFFFFu) == PCI_VENDOR_REALTEK &&
                (id >> 16) == PCI_DEVICE_RTL8139) {
                *found_bus = (u8)bus;
                *found_slot = (u8)slot;
                *found_fn = 0;
                return 0;
            }
            if (!(pci_read((u8)bus, (u8)slot, 0, 0x0C) & 0x00800000u))
                continue;
            for (u32 fn = 1; fn < 8; ++fn) {
                id = pci_read((u8)bus, (u8)slot, (u8)fn, 0);
                if ((id & 0xFFFFu) == PCI_VENDOR_REALTEK &&
                    (id >> 16) == PCI_DEVICE_RTL8139) {
                    *found_bus = (u8)bus;
                    *found_slot = (u8)slot;
                    *found_fn = (u8)fn;
                    return 0;
                }
            }
        }
    }
    return -1;
}

static int rtl_reset(void) {
    for (u32 i = 0; i < 1000000u; ++i) {
        if (!(hw_inb(rtl.io + RTL_CR) & 0x10u))
            return 0;
        hw_io_wait();
    }
    return -1;
}

static int rtl_link(struct net_device *dev) {
    struct rtl8139_state *s = (struct rtl8139_state *)dev->private_data;
    return s->io && ((hw_inb(s->io + RTL_MSR) & 0x04u) == 0);
}

static int rtl_send(struct net_device *dev, const void *frame, u16 length) {
    struct rtl8139_state *s = (struct rtl8139_state *)dev->private_data;
    const u8 *src = (const u8 *)frame;
    u8 *dst;
    u32 status;
    if (!s->io || length < 14 || length > NET_FRAME_MAX)
        return -1;
    dst = s->tx_buffers[s->tx_slot];
    for (u32 i = 0; i < length; ++i) dst[i] = src[i];
    for (u32 i = length; i < 60; ++i) dst[i] = 0;
    hw_outl(s->io + RTL_TSAD0 + s->tx_slot * 4u, (u32)(unsigned long)dst);
    hw_outl(s->io + RTL_TSD0 + s->tx_slot * 4u, length < 60 ? 60u : length);
    for (u32 wait = 0; wait < 1000000u; ++wait) {
        status = hw_inl(s->io + RTL_TSD0 + s->tx_slot * 4u);
        if (status & TX_OK) {
            s->tx_slot = (s->tx_slot + 1u) % TX_SLOTS;
            return 0;
        }
        if (status & TX_ERROR) return -1;
    }
    return -1;
}

static int rtl_receive(struct net_device *dev, void *frame, u16 capacity) {
    struct rtl8139_state *s = (struct rtl8139_state *)dev->private_data;
    u8 *dst = (u8 *)frame;
    u32 off, i, frame_size, next;
    u16 status, packet_size;
    if (!s->io) return -1;
    if (hw_inb(s->io + RTL_CR) & RX_EMPTY) return 0;
    off = s->rx_offset;
    status = (u16)s->rx_ring[off] | ((u16)s->rx_ring[off + 1] << 8);
    packet_size = (u16)s->rx_ring[off + 2] | ((u16)s->rx_ring[off + 3] << 8);
    if (packet_size < 18 || packet_size > 1522) {
        s->rx_offset = 0;
        hw_outw(s->io + RTL_CAPR, 0xFFF0u);
        return -1;
    }
    frame_size = (u32)packet_size - 4u;
    next = (off + 4u + packet_size + 3u) & ~3u;
    if (!(status & RX_OK) || frame_size > capacity) {
        s->rx_offset = next % RX_RING_SIZE;
        hw_outw(s->io + RTL_CAPR, (u16)(s->rx_offset - 16u));
        return -1;
    }
    for (i = 0; i < frame_size; ++i)
        dst[i] = s->rx_ring[(off + 4u + i) % RX_RING_SIZE];
    s->rx_offset = next % RX_RING_SIZE;
    hw_outw(s->io + RTL_CAPR, (u16)(s->rx_offset - 16u));
    return (int)frame_size;
}

static const struct net_device_ops rtl_ops = {
    rtl_link, rtl_send, rtl_receive
};

static int rtl_init(void) {
    u8 bus, slot, fn;
    u32 bar, command, mac_low;
    u16 mac_high;
    if (rtl_find(&bus, &slot, &fn) != 0)
        return -1;
    rtl.io = 0;
    bar = pci_read(bus, slot, fn, 0x10);
    if (!(bar & 1u)) return -1;
    rtl.io = (u16)(bar & 0xFFFCu);
    if (!rtl.io || rtl.io > 0xFFF0u) return -1;
    command = pci_read(bus, slot, fn, 0x04);
    hw_outl(PCI_ADDR, 0x80000000u | ((u32)bus << 16) |
            ((u32)slot << 11) | ((u32)fn << 8) | 0x04u);
    hw_outl(PCI_DATA, (command & 0xFFFFu) | 0x0005u);
    hw_outb(rtl.io + RTL_CONFIG1, 0);
    hw_outb(rtl.io + RTL_CR, 0x10);
    if (rtl_reset() != 0) { rtl.io = 0; return -1; }
    mac_low = hw_inl(rtl.io + RTL_MAC0);
    mac_high = hw_inw(rtl.io + RTL_MAC0 + 4);
    rtl_device.mac[0] = (u8)mac_low;
    rtl_device.mac[1] = (u8)(mac_low >> 8);
    rtl_device.mac[2] = (u8)(mac_low >> 16);
    rtl_device.mac[3] = (u8)(mac_low >> 24);
    rtl_device.mac[4] = (u8)mac_high;
    rtl_device.mac[5] = (u8)(mac_high >> 8);
    for (u32 i = 0; i < sizeof(rtl.rx_ring); ++i) rtl.rx_ring[i] = 0;
    for (u32 i = 0; i < sizeof(rtl.tx_buffers); ++i)
        ((u8 *)rtl.tx_buffers)[i] = 0;
    rtl.rx_offset = rtl.tx_slot = 0;
    hw_outl(rtl.io + RTL_RBSTART, (u32)(unsigned long)rtl.rx_ring);
    hw_outw(rtl.io + RTL_CAPR, 0xFFF0u);
    hw_outw(rtl.io + RTL_ISR, 0xFFFFu);
    hw_outw(rtl.io + RTL_IMR, 0);
    hw_outl(rtl.io + RTL_TCR, 0x03000700u);
    hw_outl(rtl.io + RTL_RCR, 0x0000008Fu);
    hw_outb(rtl.io + RTL_CR, 0x0Cu);
    rtl_device.name = "rtl8139";
    rtl_device.mtu = NET_MTU;
    rtl_device.private_data = &rtl;
    rtl_device.ops = &rtl_ops;
    return net_register(&rtl_device);
}

static void rtl_shutdown(void) {
    if (rtl.io) {
        hw_outw(rtl.io + RTL_IMR, 0);
        hw_outb(rtl.io + RTL_CR, 0);
    }
    rtl.io = 0;
    net_unregister(&rtl_device);
}

static const struct drv_ops rtl_driver_ops = { rtl_init, 0, rtl_shutdown };
struct drv drv_rtl8139 = {
    "rtl8139", DRV_CLASS_NET, &rtl_driver_ops, DRV_NO_IRQ, 0
};
