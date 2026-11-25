/* SPDX-License-Identifier: AGPL-3.0-only */
/* x86_64 PS/2 Mouse + VMMouse Absolute Pointer + PS/2 Keyboard */
#include "../../include/input64.h"
#include "../../include/hal.h"

#define KBD_DATA   0x60u
#define KBD_STATUS 0x64u
#define KBD_CMD    0x64u

#define VMMOUSE_MAGIC   0x564D5868u
#define VMMOUSE_PORT    0x5658u
#define VMMOUSE_CMD_ABS 39u
#define VMMOUSE_CMD_STA 40u
#define VMMOUSE_CMD_DAT 41u

static int32_t scr_w = 1024, scr_h = 768;
static int32_t cur_x = 512, cur_y = 384;
static uint8_t cur_btn = 0, prev_btn = 0;
static int vmmouse_ok = 0;
static uint8_t ps2_pkt[3];
static int ps2_idx = 0;

static void vmmouse_rpc(uint32_t cmd, uint32_t arg, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    uint32_t a = VMMOUSE_MAGIC, b = arg, c = cmd, d = VMMOUSE_PORT, si = 0, di = 0;
    __asm__ volatile ("inl %%dx, %%eax"
        : "+a"(a), "+b"(b), "+c"(c), "+d"(d), "+S"(si), "+D"(di)
        : : "memory");
    if (eax) *eax = a;
    if (ebx) *ebx = b;
    if (ecx) *ecx = c;
    if (edx) *edx = d;
}

static void wait_ibf(void) {
    for (int i = 0; i < 50000; i++) {
        u8 st = 0;
        hal_port_read8(KBD_STATUS, &st);
        if (!(st & 0x02u)) return;
    }
}

static void wait_obf(void) {
    for (int i = 0; i < 50000; i++) {
        u8 st = 0;
        hal_port_read8(KBD_STATUS, &st);
        if (st & 0x01u) return;
    }
}

static void mouse_write(u8 val) {
    wait_ibf();
    hal_port_write8(KBD_CMD, 0xD4u);
    wait_ibf();
    hal_port_write8(KBD_DATA, val);
    wait_obf();
    u8 ack = 0;
    hal_port_read8(KBD_DATA, &ack);
}

static int32_t decode_scancode(u8 sc) {
    static const char map[128] = {
        0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
        'q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
        'a','s','d','f','g','h','j','k','l',';','\'','`', 0,'\\',
        'z','x','c','v','b','n','m',',','.','/', 0,'*', 0,' '
    };
    if (sc & 0x80u) return 0;
    if (sc == 0x48u) return INPUT64_KEY_UP;
    if (sc == 0x50u) return INPUT64_KEY_DOWN;
    if (sc == 0x4Bu) return INPUT64_KEY_LEFT;
    if (sc == 0x4Du) return INPUT64_KEY_RIGHT;
    return (sc < sizeof(map)) ? (int32_t)map[sc] : 0;
}

void input64_init(int32_t screen_w, int32_t screen_h) {
    scr_w = screen_w > 0 ? screen_w : 1024;
    scr_h = screen_h > 0 ? screen_h : 768;
    cur_x = scr_w / 2;
    cur_y = scr_h / 2;
    cur_btn = prev_btn = 0;
    ps2_idx = 0;

    /* Flush pending bytes */
    for (int i = 0; i < 32; i++) {
        u8 st = 0, tmp = 0;
        hal_port_read8(KBD_STATUS, &st);
        if (!(st & 0x01u)) break;
        hal_port_read8(KBD_DATA, &tmp);
    }

    /* Enable PS/2 auxiliary mouse + aux clock in i8042 config byte */
    wait_ibf();
    hal_port_write8(KBD_CMD, 0xA8u);
    wait_ibf();
    hal_port_write8(KBD_CMD, 0x20u);
    wait_obf();
    {
        u8 cfg = 0;
        hal_port_read8(KBD_DATA, &cfg);
        cfg |= 0x02u;
        cfg &= (u8)~0x20u;
        wait_ibf();
        hal_port_write8(KBD_CMD, 0x60u);
        wait_ibf();
        hal_port_write8(KBD_DATA, cfg);
    }
    mouse_write(0xF6u); /* Set defaults */
    mouse_write(0xF4u); /* Enable streaming */

    /* Probe QEMU VMMouse absolute pointer (exact VMware backdoor sequence) */
    uint32_t a = 0, b = 0, c = 0, d = 0;
    vmmouse_ok = 0;
    vmmouse_rpc(10u /* GETVERSION */, 0u, &a, &b, &c, &d);
    if (b == VMMOUSE_MAGIC && a != 0xFFFFFFFFu) {
        vmmouse_rpc(41u /* COMMAND */, 0x45414552u /* ENABLE */, &a, &b, &c, &d);
        vmmouse_rpc(40u /* STATUS */, 0u, &a, &b, &c, &d);
        if ((a & 0xFFFF0000u) != 0xFFFF0000u) {
            vmmouse_rpc(39u /* DATA */, 1u, &a, &b, &c, &d);
            if (a == 0x3442554Au /* VMW_VERSION_ID */) {
                vmmouse_rpc(41u /* COMMAND */, 0x53424152u /* REQUEST_ABSOLUTE */, &a, &b, &c, &d);
                vmmouse_ok = 1;
            }
        }
    }
    hal_serial_puts(vmmouse_ok ? "x86_64: vmmouse active\n" : "x86_64: ps2 mouse\n");
}

void input64_poll(struct input64_state *out) {
    int32_t key = 0;
    int32_t ps2_dx = 0, ps2_dy = 0;
    int vmmouse_event = 0;
    for (int iter = 0; iter < 32; iter++) {
        u8 st = 0, data = 0;
        hal_port_read8(KBD_STATUS, &st);
        if (!(st & 0x01u)) break;
        hal_port_read8(KBD_DATA, &data);
        if (st & 0x20u) {
            if (ps2_idx == 0 && !(data & 0x08u)) continue;
            ps2_pkt[ps2_idx++] = data;
            if (ps2_idx == 3) {
                ps2_idx = 0;
                cur_btn = (uint8_t)(ps2_pkt[0] & 0x07u);
                {
                    int32_t dx = (ps2_pkt[0] & 0x10u) ? (int32_t)(ps2_pkt[1] | ~0xFF) : (int32_t)ps2_pkt[1];
                    int32_t dy = (ps2_pkt[0] & 0x20u) ? (int32_t)(ps2_pkt[2] | ~0xFF) : (int32_t)ps2_pkt[2];
                    ps2_dx += dx;
                    ps2_dy += dy;
                    if (!vmmouse_ok) { cur_x += dx; cur_y -= dy; }
                }
            }
        } else {
            int32_t k = decode_scancode(data);
            if (k) key = k;
        }
    }

    if (vmmouse_ok) {
        for (int p = 0; p < 8; p++) {
            uint32_t a = 0, b = 0, c = 0, d = 0;
            vmmouse_rpc(VMMOUSE_CMD_STA, 0u, &a, &b, &c, &d);
            uint32_t words = a & 0xFFFFu;
            if (words < 4u || (a & 0xFFFF0000u) == 0xFFFF0000u) break;
            vmmouse_rpc(VMMOUSE_CMD_DAT, 4u, &a, &b, &c, &d);
            vmmouse_event = 1;
            cur_btn = 0;
            if (a & 0x20u) cur_btn |= 1u;
            if (a & 0x10u) cur_btn |= 2u;
            cur_x = (int32_t)(((uint64_t)(b & 0xFFFFu) * (uint64_t)scr_w) >> 16);
            cur_y = (int32_t)(((uint64_t)(c & 0xFFFFu) * (uint64_t)scr_h) >> 16);
        }
    }
    /* QEMU VNC often feeds the emulated PS/2 stream, not VMware's backdoor
       queue. Fall back to PS/2 deltas whenever the VMMouse queue is empty. */
    if (vmmouse_ok && !vmmouse_event) {
        cur_x += ps2_dx;
        cur_y -= ps2_dy;
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
