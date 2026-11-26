#include "../../kernel/kernel.h"
#include "../../include/drv_pic.h"

#define POINTER_MAX 65535
#define PS2_TRIES 100000

volatile u8 mouse_buttons;
volatile u8 mouse_click_latch;
volatile u32 mouse_press_x, mouse_press_y;   /* normalized pointer position of the latest left press */

u8 mouse_take_clicks(void) {
    u8 v = mouse_click_latch;
    mouse_click_latch = 0;
    return v;
}
static void pointer_sync_cells_fwd(void);
static u8 packet[3];
static u8 phase;
static u32 step_x = 82u, step_y = 109u;   /* normalized units per mouse count (1 count ~ 1 pixel) */
static int vmmouse_active;

/* Called once the display mode is known so relative motion is about one pixel per count. */
void pointer_set_display(int width, int height) {
    if (width > 0) step_x = 65536u / (u32)width;
    if (height > 0) step_y = 65536u / (u32)height;
}

static i32 pointer_accel(i32 d) {
    i32 a = d < 0 ? -d : d;
    if (a > 12) a = 12 + (a - 12) * 2;
    return d < 0 ? -a : a;
}

/* --- VMware backdoor absolute pointer (emulated by QEMU, VirtualBox, VMware) --- */
#define VMW_MAGIC 0x564D5868u
#define VMW_PORT 0x5658u
#define VMW_CMD_GETVERSION 10u
#define VMW_CMD_DATA 39u
#define VMW_CMD_STATUS 40u
#define VMW_CMD_COMMAND 41u
#define VMW_ENABLE 0x45414552u
#define VMW_REQUEST_ABSOLUTE 0x53424152u
#define VMW_VERSION_ID 0x3442554Au

static void vmw_call(u32 cmd, u32 arg, u32 out[4]) {
    u32 a = VMW_MAGIC, b = arg, c = cmd, d = VMW_PORT, si = 0, di = 0;
    __asm__ volatile ("inl %%dx, %%eax"
                      : "+a"(a), "+b"(b), "+c"(c), "+d"(d), "+S"(si), "+D"(di)
                      :
                      : "memory");
    out[0] = a; out[1] = b; out[2] = c; out[3] = d;
}

static int vmmouse_init(void) {
    u32 o[4];
    vmw_call(VMW_CMD_GETVERSION, 0, o);
    if (o[1] != VMW_MAGIC || o[0] == 0xFFFFFFFFu) return 0;
    vmw_call(VMW_CMD_COMMAND, VMW_ENABLE, o);
    vmw_call(VMW_CMD_STATUS, 0, o);
    if ((o[0] & 0xFFFF0000u) == 0xFFFF0000u) return 0;
    vmw_call(VMW_CMD_DATA, 1, o);
    if (o[0] != VMW_VERSION_ID) return 0;
    vmw_call(VMW_CMD_COMMAND, VMW_REQUEST_ABSOLUTE, o);
    return 1;
}

static void vmmouse_poll(void) {
    u32 o[4];
    for (int guard = 0; guard < 64; guard++) {
        u8 btn;
        vmw_call(VMW_CMD_STATUS, 0, o);
        if ((o[0] & 0xFFFF0000u) == 0xFFFF0000u) { vmmouse_active = 0; return; }
        if ((o[0] & 0xFFFFu) < 4u) return;
        vmw_call(VMW_CMD_DATA, 4, o);
        btn = (u8)(((o[0] & 0x20u) ? 1 : 0) | ((o[0] & 0x10u) ? 2 : 0) | ((o[0] & 0x08u) ? 4 : 0));
        cursor_norm_x = o[1] > 65535u ? 65535u : o[1];
        cursor_norm_y = o[2] > 65535u ? 65535u : o[2];
        if ((btn & 1) && !(mouse_buttons & 1)) { mouse_press_x = cursor_norm_x; mouse_press_y = cursor_norm_y; }
        mouse_click_latch |= (u8)(btn & (u8)~mouse_buttons);
        mouse_buttons = btn;
        pointer_sync_cells_fwd();
    }
}

static void pointer_sync_cells(void) {
    cursor_x = (cursor_norm_x * VGA_COLS) >> 16;
    cursor_y = (cursor_norm_y * VGA_ROWS) >> 16;
    if (cursor_x >= VGA_COLS) cursor_x = VGA_COLS - 1;
    if (cursor_y >= VGA_ROWS) cursor_y = VGA_ROWS - 1;
}

static void pointer_sync_cells_fwd(void) { pointer_sync_cells(); }

static void pointer_apply_delta(i32 dx, i32 dy) {
    i32 x = (i32)cursor_norm_x + pointer_accel(dx) * (i32)step_x;
    i32 y = (i32)cursor_norm_y + pointer_accel(dy) * (i32)step_y;

    if (x < 0) x = 0;
    if (x > POINTER_MAX) x = POINTER_MAX;
    if (y < 0) y = 0;
    if (y > POINTER_MAX) y = POINTER_MAX;
    cursor_norm_x = (u32)x;
    cursor_norm_y = (u32)y;
    pointer_sync_cells();
}

static int ctl_wait_write(void) {
    for (u32 i = 0; i < PS2_TRIES; i++)
        if (!(inb(0x64) & 0x02))
            return 1;
    return 0;
}

static int ctl_wait_read(void) {
    for (u32 i = 0; i < PS2_TRIES; i++)
        if (inb(0x64) & 0x01)
            return 1;
    return 0;
}

static int mouse_send(u8 cmd) {
    if (!ctl_wait_write())
        return 0;
    outb(0x64, 0xD4);
    if (!ctl_wait_write())
        return 0;
    outb(0x60, cmd);
    if (!ctl_wait_read())
        return 0;
    return inb(0x60) == 0xFA;
}

int mouse_init(void) {
    u8 cfg;
    if (!ctl_wait_write())
        return 0;
    outb(0x64, 0xA8);
    if (!ctl_wait_write())
        return 0;
    outb(0x64, 0x20);
    if (!ctl_wait_read())
        return 0;
    cfg = inb(0x60);
    cfg |= 0x02;
    cfg &= (u8)~0x20;
    if (!ctl_wait_write())
        return 0;
    outb(0x64, 0x60);
    if (!ctl_wait_write())
        return 0;
    outb(0x60, cfg);
    if (!mouse_send(0xF6))
        return 0;
    if (!mouse_send(0xF4))
        return 0;
    phase = 0;
    pointer_sync_cells();
    vmmouse_active = vmmouse_init();
    serial_str(vmmouse_active ? "mouse: vmmouse absolute pointer\n" : "mouse: ps/2 relative pointer\n");

    /* IRQ12 is masked by the early PIC setup; enable it only after the
       controller and mouse have been configured successfully. */
    drv_pic_unmask(12);
    return 1;
}

void mouse_irq_handler(void) {
    u8 status = inb(0x64);
    u8 b;
    if (!(status & 0x20))
        return;
    b = inb(0x60);
    if (phase == 0 && !(b & 0x08))
        return;
    packet[phase++] = b;
    if (phase < 3)
        return;
    phase = 0;
    if (vmmouse_active) { vmmouse_poll(); return; }
    i32 dx = (i32)packet[1] - (i32)((packet[0] << 4) & 0x100);
    i32 dy = (i32)packet[2] - (i32)((packet[0] << 3) & 0x100);
    pointer_apply_delta(dx, -dy);
    {
        u8 new_btn = packet[0] & 0x07;
        if ((new_btn & 1) && !(mouse_buttons & 1)) { mouse_press_x = cursor_norm_x; mouse_press_y = cursor_norm_y; }
        mouse_click_latch |= (u8)(new_btn & (u8)~mouse_buttons);
        mouse_buttons = new_btn;
    }
}

void cursor_nudge(i32 dx, i32 dy) {
    i32 x = (i32)cursor_x + dx;
    i32 y = (i32)cursor_y + dy;

    if (x < 0) x = 0;
    if (x >= VGA_COLS) x = VGA_COLS - 1;
    if (y < 0) y = 0;
    if (y >= VGA_ROWS) y = VGA_ROWS - 1;

    cursor_x = (u32)x;
    cursor_y = (u32)y;
    cursor_norm_x = ((u32)(2 * x + 1) * 65536u) / (VGA_COLS * 2u);
    cursor_norm_y = ((u32)(2 * y + 1) * 65536u) / (VGA_ROWS * 2u);
}
