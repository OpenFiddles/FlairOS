#include "kernel.h"
#include "../include/vbe.h"
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
void cpu_fault(void) { kernel_panic("CPU exception"); }
