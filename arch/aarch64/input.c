/* SPDX-License-Identifier: AGPL-3.0-only */
/* AArch64 VirtIO-MMIO Input Driver (virtio-tablet-device + virtio-keyboard-device) */
#include "../../include/input64.h"
#include "../../include/hal.h"

#define VIRTIO_MMIO_BASE   0x0A000000UL
#define VIRTIO_MMIO_STRIDE 0x200UL
#define VIRTIO_MMIO_SLOTS  32

#define VIO_MAGIC          0x000
#define VIO_VERSION        0x004
#define VIO_DEVICE_ID      0x008
#define VIO_STATUS         0x070
#define VIO_GUEST_PAGE_SZ  0x028
#define VIO_QUEUE_SEL      0x030
#define VIO_QUEUE_NUM_MAX  0x034
#define VIO_QUEUE_NUM      0x038
#define VIO_QUEUE_ALIGN    0x03C
#define VIO_QUEUE_PFN      0x040
#define VIO_QUEUE_NOTIFY   0x050
#define VIO_QUEUE_READY    0x044
#define VIO_DESC_LOW       0x080
#define VIO_DESC_HIGH      0x084
#define VIO_AVAIL_LOW      0x090
#define VIO_AVAIL_HIGH     0x094
#define VIO_USED_LOW       0x0A0
#define VIO_USED_HIGH      0x0A4

#define EV_SYN 0x00u
#define EV_KEY 0x01u
#define EV_REL 0x02u
#define EV_ABS 0x03u

#define ABS_X  0x00u
#define ABS_Y  0x01u
#define REL_X  0x00u
#define REL_Y  0x01u
#define BTN_LEFT 0x110u

#define QSZ 32

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[QSZ];
} __attribute__((packed));

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[QSZ];
} __attribute__((packed));

struct virtio_input_event {
    uint16_t type;
    uint16_t code;
    uint32_t value;
} __attribute__((packed));

struct vio_input_dev {
    uint8_t vring_mem[8192] __attribute__((aligned(4096)));
    int active;
    unsigned long base;
    uint32_t version;
    uint16_t last_used;
    struct virtq_desc  *desc;
    struct virtq_avail *avail;
    struct virtq_used  *used;
    struct virtio_input_event events[QSZ] __attribute__((aligned(16)));
};

static struct vio_input_dev devs[4] __attribute__((aligned(4096)));
static int dev_count = 0;
static int32_t scr_w = 1024, scr_h = 768;
static int32_t cur_x = 512, cur_y = 384;
static uint8_t cur_btn = 0, prev_btn = 0;

static int32_t linux_key_to_char(uint16_t code) {
    static const char map[60] = {
        0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
        'q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
        'a','s','d','f','g','h','j','k','l',';','\'','`', 0,'\\',
        'z','x','c','v','b','n','m',',','.','/', 0,'*', 0,' ', 0, 0
    };
    if (code == 103u) return INPUT64_KEY_UP;
    if (code == 108u) return INPUT64_KEY_DOWN;
    if (code == 105u) return INPUT64_KEY_LEFT;
    if (code == 106u) return INPUT64_KEY_RIGHT;
    return (code < sizeof(map)) ? (int32_t)map[code] : 0;
}

static void init_vio_dev(struct vio_input_dev *d, unsigned long base) {
    d->active = 1;
    d->base = base;
    d->version = hal_mmio_read32(base + VIO_VERSION);
    d->last_used = 0;
    for (int i = 0; i < 8192; i++) d->vring_mem[i] = 0;
    d->desc  = (struct virtq_desc  *)(d->vring_mem + 0);
    d->avail = (struct virtq_avail *)(d->vring_mem + sizeof(struct virtq_desc) * QSZ);
    d->used  = (struct virtq_used  *)(d->vring_mem + 4096);

    hal_mmio_write32(base + VIO_STATUS, 0u);
    hal_mmio_write32(base + VIO_STATUS, 1u | 2u); /* ACK | DRIVER */

    if (d->version == 1u) {
        hal_mmio_write32(base + VIO_GUEST_PAGE_SZ, 4096u);
    } else {
        hal_mmio_write32(base + 0x024, 0u);
        hal_mmio_write32(base + 0x020, 0u);
        hal_mmio_write32(base + 0x024, 1u);
        hal_mmio_write32(base + 0x020, 1u); /* VIRTIO_F_VERSION_1 */
        hal_mmio_write32(base + VIO_STATUS, 1u | 2u | 8u); /* FEATURES_OK */
    }

    hal_mmio_write32(base + VIO_QUEUE_SEL, 0u);
    hal_mmio_write32(base + VIO_QUEUE_NUM, QSZ);

    for (int i = 0; i < QSZ; i++) {
        d->desc[i].addr = (uint64_t)(uintptr_t)&d->events[i];
        d->desc[i].len = sizeof(struct virtio_input_event);
        d->desc[i].flags = 2u; /* VRING_DESC_F_WRITE */
        d->desc[i].next = 0;
        d->avail->ring[i] = (uint16_t)i;
    }
    d->avail->flags = 0;
    d->avail->idx = QSZ;
    d->used->flags = 0;
    d->used->idx = 0;

    if (d->version == 1u) {
        hal_mmio_write32(base + VIO_QUEUE_ALIGN, 4096u);
        hal_mmio_write32(base + VIO_QUEUE_PFN, (uint32_t)((uintptr_t)d->vring_mem >> 12));
    } else {
        uint64_t da = (uint64_t)(uintptr_t)d->desc;
        uint64_t aa = (uint64_t)(uintptr_t)d->avail;
        uint64_t ua = (uint64_t)(uintptr_t)d->used;
        hal_mmio_write32(base + VIO_DESC_LOW, (u32)da);
        hal_mmio_write32(base + VIO_DESC_HIGH, (u32)(da >> 32));
        hal_mmio_write32(base + VIO_AVAIL_LOW, (u32)aa);
        hal_mmio_write32(base + VIO_AVAIL_HIGH, (u32)(aa >> 32));
        hal_mmio_write32(base + VIO_USED_LOW, (u32)ua);
        hal_mmio_write32(base + VIO_USED_HIGH, (u32)(ua >> 32));
        hal_mmio_write32(base + VIO_QUEUE_READY, 1u);
    }

    hal_mmio_write32(base + VIO_STATUS, (d->version == 1u ? 7u : 15u)); /* DRIVER_OK */
    hal_mmio_write32(base + VIO_QUEUE_NOTIFY, 0u);
    hal_serial_puts(d->version == 1u ? "virtio-input v1 ready\n" : "virtio-input v2 ready\n");
}

void input64_init(int32_t screen_w, int32_t screen_h) {
    scr_w = screen_w > 0 ? screen_w : 1024;
    scr_h = screen_h > 0 ? screen_h : 768;
    cur_x = scr_w / 2;
    cur_y = scr_h / 2;
    cur_btn = prev_btn = 0;
    dev_count = 0;

    for (int i = 0; i < VIRTIO_MMIO_SLOTS && dev_count < 4; i++) {
        unsigned long base = VIRTIO_MMIO_BASE + (unsigned long)i * VIRTIO_MMIO_STRIDE;
        u32 magic = hal_mmio_read32(base + VIO_MAGIC);
        u32 did = hal_mmio_read32(base + VIO_DEVICE_ID);
        if (magic == 0x74726976u && did == 18u) {
            init_vio_dev(&devs[dev_count++], base);
        }
    }
}

void input64_poll(struct input64_state *out) {
    int32_t key = 0;
    for (int d = 0; d < dev_count; d++) {
        struct vio_input_dev *dev = &devs[d];
        __asm__ volatile ("dmb sy" : : : "memory");
        uint16_t uidx = dev->used->idx;
        int refilled = 0;
        while (dev->last_used != uidx) {
            uint16_t slot = (uint16_t)(dev->last_used % QSZ);
            uint32_t id = dev->used->ring[slot].id % QSZ;
            struct virtio_input_event ev = dev->events[id];
            if (ev.type == EV_ABS) {
                if (ev.code == ABS_X)
                    cur_x = (int32_t)(((uint64_t)ev.value * (uint64_t)scr_w) / 32768u);
                else if (ev.code == ABS_Y)
                    cur_y = (int32_t)(((uint64_t)ev.value * (uint64_t)scr_h) / 32768u);
            } else if (ev.type == EV_REL) {
                if (ev.code == REL_X) cur_x += (int32_t)ev.value;
                else if (ev.code == REL_Y) cur_y += (int32_t)ev.value;
            } else if (ev.type == EV_KEY) {
                if (ev.code == BTN_LEFT) {
                    if (ev.value) cur_btn |= 1u;
                    else cur_btn &= (uint8_t)~1u;
                } else if (ev.value == 1u) {
                    int32_t k = linux_key_to_char(ev.code);
                    if (k) key = k;
                }
            }
            dev->avail->ring[dev->avail->idx % QSZ] = (uint16_t)id;
            __asm__ volatile ("dmb st" : : : "memory");
            dev->avail->idx++;
            dev->last_used++;
            refilled = 1;
        }
        if (refilled) hal_mmio_write32(dev->base + VIO_QUEUE_NOTIFY, 0u);
    }

    if (cur_x < 0) cur_x = 0;
    if (cur_y < 0) cur_y = 0;
    if (cur_x >= scr_w) cur_x = scr_w - 1;
    if (cur_y >= scr_h) cur_y = scr_h - 1;

    if (out) {
        out->mouse_x = cur_x;
        out->mouse_y = cur_y;
        out->buttons = cur_btn;
        out->clicked = (uint8_t)((cur_btn & 1u) && !(prev_btn & 1u));
        out->key = key;
    }
    prev_btn = cur_btn;
}
