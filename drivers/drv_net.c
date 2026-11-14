#include "../include/drv_net.h"
#include "../include/hw.h"

/* RTL8139 PCI Ethernet controller, polling mode for the first driver layer. */
#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC
#define PCI_VENDOR_REALTEK 0x10EC
#define PCI_DEVICE_RTL8139 0x8139
#define PCI_SLOTS 32

#define RTL_MAC0 0x00
#define RTL_RBSTART 0x30
#define RTL_TSD0 0x10
#define RTL_TSAD0 0x20
#define RTL_CR 0x37
#define RTL_CAPR 0x38
#define RTL_CBR 0x3A
#define RTL_IMR 0x3C
#define RTL_ISR 0x3E
#define RTL_TCR 0x40
#define RTL_RCR 0x44
#define RTL_CONFIG1 0x52
#define RTL_MSR 0x58

#define RX_RING_SIZE 8192u
#define RX_EXTRA 2048u
#define TX_SLOTS 4
#define TX_BUFFER_SIZE 1536u
#define ETH_MAX_FRAME 1514u
#define RX_STATUS_OK 0x0001u
#define RX_COMMAND_EMPTY 0x01u
#define TX_STATUS_OK 0x00008000u
#define TX_STATUS_ERROR 0x00004000u

static u16 net_io;
static u8 net_mac[6];
static int net_rtl;
static u32 rx_offset;
static u32 tx_slot;
static u8 rx_ring[RX_RING_SIZE + RX_EXTRA + 16] __attribute__((aligned(16)));
static u8 tx_buffers[TX_SLOTS][TX_BUFFER_SIZE] __attribute__((aligned(16)));

static u32 pci_read(u8 slot, u8 offset) {
    hw_outl(PCI_CONFIG_ADDR, 0x80000000u | ((u32)slot << 11) | (offset & 0xFCu));
    return hw_inl(PCI_CONFIG_DATA);
}

static int rtl_find(void) {
    for (u8 slot = 0; slot < PCI_SLOTS; ++slot) {
        u32 id = pci_read(slot, 0x00);
        if ((id & 0xFFFFu) == PCI_VENDOR_REALTEK &&
            (id >> 16) == PCI_DEVICE_RTL8139)
            return (int)slot;
    }
    return -1;
}

static int rtl_wait_reset(void) {
    for (u32 i = 0; i < 1000000u; ++i) {
        if (!(hw_inb(net_io + RTL_CR) & 0x10u))
            return 0;
        hw_io_wait();
    }
    return -1;
}

static void rtl_stop(void) {
    if (!net_io) return;
    hw_outb(net_io + RTL_IMR, 0);
    hw_outw(net_io + RTL_IMR, 0);
    hw_outb(net_io + RTL_CR, 0);
}

int drv_net_init(void) {
    int slot = rtl_find();
    u32 bar, command, mac_low;
    u16 mac_high;

    net_io = 0;
    net_rtl = 0;
    if (slot < 0)
        return -1;

    bar = pci_read((u8)slot, 0x10);
    if (!(bar & 1u))
        return -1;
    net_io = (u16)(bar & 0xFFFCu);
    if (!net_io || net_io > 0xFFF0u)
        return -1;

    command = pci_read((u8)slot, 0x04);
    hw_outl(PCI_CONFIG_ADDR, 0x80000000u | ((u32)slot << 11) | 0x04u);
    hw_outl(PCI_CONFIG_DATA, (command & 0xFFFFu) | 0x0005u);

    hw_outb(net_io + RTL_CONFIG1, 0x00);
    hw_outb(net_io + RTL_CR, 0x10);
    if (rtl_wait_reset() != 0) {
        net_io = 0;
        return -1;
    }

    mac_low = hw_inl(net_io + RTL_MAC0);
    mac_high = hw_inw(net_io + RTL_MAC0 + 4);
    net_mac[0] = (u8)mac_low;
    net_mac[1] = (u8)(mac_low >> 8);
    net_mac[2] = (u8)(mac_low >> 16);
    net_mac[3] = (u8)(mac_low >> 24);
    net_mac[4] = (u8)mac_high;
    net_mac[5] = (u8)(mac_high >> 8);

    for (u32 i = 0; i < sizeof(rx_ring); ++i)
        rx_ring[i] = 0;
    for (u32 i = 0; i < sizeof(tx_buffers); ++i)
        ((u8 *)tx_buffers)[i] = 0;

    rx_offset = 0;
    tx_slot = 0;
    hw_outl(net_io + RTL_RBSTART, (u32)(unsigned long)rx_ring);
    hw_outw(net_io + RTL_CAPR, 0xFFF0u);
    hw_outw(net_io + RTL_ISR, 0xFFFFu);
    hw_outw(net_io + RTL_IMR, 0); /* polling; IRQs are intentionally disabled */
    hw_outl(net_io + RTL_TCR, 0x03000700u);
    hw_outl(net_io + RTL_RCR, 0x0000008Fu);
    hw_outb(net_io + RTL_CR, 0x0Cu); /* enable receiver and transmitter */

    net_rtl = 1;
    return 0;
}

int drv_net_link_up(void) {
    if (!net_rtl || !net_io)
        return 0;
    return (hw_inb(net_io + RTL_MSR) & 0x04u) == 0;
}

const u8 *drv_net_mac(void) { return net_mac; }
u16 drv_net_io(void) { return net_io; }
int drv_net_is_rtl(void) { return net_rtl; }

int drv_net_send(const void *frame, u16 length) {
    const u8 *src = (const u8 *)frame;
    u8 *dst;
    u32 status, wait;
    if (!net_rtl || !frame || length < 14 || length > ETH_MAX_FRAME)
        return -1;

    dst = tx_buffers[tx_slot];
    for (u32 i = 0; i < length; ++i)
        dst[i] = src[i];
    for (u32 i = length; i < 60; ++i)
        dst[i] = 0;

    hw_outl(net_io + RTL_TSAD0 + tx_slot * 4u,
            (u32)(unsigned long)dst);
    hw_outl(net_io + RTL_TSD0 + tx_slot * 4u, length < 60 ? 60u : length);

    for (wait = 0; wait < 1000000u; ++wait) {
        status = hw_inl(net_io + RTL_TSD0 + tx_slot * 4u);
        if (status & TX_STATUS_OK) {
            tx_slot = (tx_slot + 1u) % TX_SLOTS;
            return 0;
        }
        if (status & TX_STATUS_ERROR)
            return -1;
    }
    return -1;
}

/* Read one Ethernet frame. Returns frame bytes, 0 if no packet, or -1 on error.
   The NIC appends a four-byte CRC which is deliberately not copied to callers. */
int drv_net_receive(void *frame, u16 capacity) {
    u8 *dst = (u8 *)frame;
    u16 status, packet_size;
    u32 i, frame_size, next;

    if (!net_rtl || !frame || capacity == 0)
        return -1;
    if (hw_inb(net_io + RTL_CR) & RX_COMMAND_EMPTY)
        return 0;

    status = (u16)rx_ring[rx_offset] | ((u16)rx_ring[rx_offset + 1] << 8);
    packet_size = (u16)rx_ring[rx_offset + 2] | ((u16)rx_ring[rx_offset + 3] << 8);
    if (packet_size < 18 || packet_size > 1522) {
        rx_offset = 0;
        hw_outw(net_io + RTL_CAPR, 0xFFF0u);
        hw_outb(net_io + RTL_CR, 0x0Cu);
        return -1;
    }

    frame_size = (u32)packet_size - 4u;
    next = (rx_offset + 4u + packet_size + 3u) & ~3u;
    if (status & RX_STATUS_OK) {
        if (frame_size > capacity)
            return -1;
        for (i = 0; i < frame_size; ++i)
            dst[i] = rx_ring[rx_offset + 4u + i];
    }
    rx_offset = next % RX_RING_SIZE;
    hw_outw(net_io + RTL_CAPR, (u16)(rx_offset - 16u));
    return (status & RX_STATUS_OK) ? (int)frame_size : -1;
}

static void net_shutdown(void) {
    rtl_stop();
    net_rtl = 0;
    net_io = 0;
}

static const struct drv_ops drv_net_ops = { drv_net_init, 0, net_shutdown };
struct drv drv_net_device = { "net", DRV_CLASS_CHAR, &drv_net_ops, DRV_NO_IRQ, 0 };
