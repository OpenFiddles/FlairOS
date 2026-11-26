#include "kernel.h"
#include "../../include/vbe.h"
void kernel_panic(const char *reason) {
    static const char heading[] = "FLAIROS PANIC";
    volatile u16 *vga = (volatile u16 *)0xB8000;
    unsigned int i;
    irq_off();
    serial_str("\nPANIC: ");
    if (reason) serial_str(reason);
    serial_str("\n");
    vbe_text_mode(); /* Make the emergency text panic screen visible after a GPU fault. */
    for (i = 0; i < VGA_COLS * VGA_ROWS; ++i) vga[i] = (u16)(' ' | (0x4Fu << 8));
    for (i = 0; heading[i] && i < VGA_COLS; ++i) vga[i] = (u16)((u8)heading[i] | (0x4Fu << 8));
    for (i = 0; reason && reason[i] && i < VGA_COLS; ++i) vga[VGA_COLS * 2 + i] = (u16)((u8)reason[i] | (0x4Fu << 8));
    for (;;) halt();
}
static void serial_hex32(u32 value) {
    static const char digits[] = "0123456789ABCDEF";
    char out[8];
    for (int i = 7; i >= 0; --i) {
        out[i] = digits[value & 0xFu];
        value >>= 4;
    }
    for (int i = 0; i < 8; ++i)
        serial_str((char[]){out[i], 0});
}

/* Every vector has its own stub (kernel/boot/isr.asm), so the frame is always
   [vector][error code][EIP][CS][EFLAGS]. */
void cpu_fault(u32 *frame) {
    serial_str("\nPANIC: CPU exception vector=");
    serial_hex32(frame ? frame[0] : 0);
    serial_str(" error=");
    serial_hex32(frame ? frame[1] : 0);
    serial_str(" eip=");
    serial_hex32(frame ? frame[2] : 0);
    kernel_panic("CPU exception");
}
