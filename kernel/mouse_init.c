#include "../kernel/kernel.h"
#include "../include/drv_pic.h"

#define POINTER_MAX 65535
#define POINTER_STEP_X ((65536u + (VGA_COLS * 2u)) / (VGA_COLS * 4u))
#define POINTER_STEP_Y ((65536u + (VGA_ROWS * 2u)) / (VGA_ROWS * 4u))
#define PS2_TRIES 100000

volatile u8 mouse_buttons;
static u8 packet[3];
static u8 phase;

static void pointer_sync_cells(void) {
    cursor_x = (cursor_norm_x * VGA_COLS) >> 16;
    cursor_y = (cursor_norm_y * VGA_ROWS) >> 16;
    if (cursor_x >= VGA_COLS) cursor_x = VGA_COLS - 1;
    if (cursor_y >= VGA_ROWS) cursor_y = VGA_ROWS - 1;
}

static void pointer_apply_delta(i32 dx, i32 dy) {
    i32 x = (i32)cursor_norm_x + dx * (i32)POINTER_STEP_X;
    i32 y = (i32)cursor_norm_y + dy * (i32)POINTER_STEP_Y;

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
    i32 dx = (i32)packet[1] - (i32)((packet[0] << 4) & 0x100);
    i32 dy = (i32)packet[2] - (i32)((packet[0] << 3) & 0x100);
    mouse_buttons = packet[0] & 0x07;
    pointer_apply_delta(dx, -dy);
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
