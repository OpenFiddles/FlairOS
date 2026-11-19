#include "kernel.h"
#include "../include/vbe.h"
#include "../include/boot.h"

static void panic_text(unsigned int row, unsigned int col, const char *text, u8 attr) {
    volatile u16 *vga = VGA_MEM;
    while (text && *text && row < VGA_ROWS && col < VGA_COLS)
        vga[row * VGA_COLS + col++] = (u16)((u8)*text++ | ((u16)attr << 8));
}

void kernel_panic(const char *reason) {
    static const char heading[] = "FLAIROS PANIC";
    volatile u16 *vga = VGA_MEM;
    unsigned int i;
    irq_off();
    serial_str("\nPANIC: ");
    if (reason) serial_str(reason);
    serial_str("\n");
    vbe_text_mode();
    for (i = 0; i < VGA_COLS * VGA_ROWS; ++i)
        vga[i] = (u16)(' ' | (0x4Fu << 8));
    for (i = 0; heading[i] && i < VGA_COLS; ++i)
        vga[i] = (u16)((u8)heading[i] | (0x4Fu << 8));
    panic_text(2, 2, reason ? reason : "Unknown kernel error", 0x4F);
#if defined(FLAIROS_DEBUG_BUILD)
    panic_text(5, 2, "DEBUG BUILD - detailed boot trace is available on serial", 0x4F);
    panic_text(7, 2, "Entering recovery in:", 0x4F);
    serial_str("panic: debug recovery countdown started (10 seconds)\n");
    /* The PIT and IDT are active before graphics startup. Keep timer IRQs
       running only while the debug countdown advances. */
    irq_on();
    for (i = 10; ; --i) {
        char digits[3];
        digits[0] = (char)('0' + i / 10u);
        digits[1] = (char)('0' + i % 10u);
        digits[2] = 0;
        panic_text(7, 26, digits, 0x4F);
        serial_str("panic: recovery in ");
        serial_str(digits);
        serial_str("\n");
        if (i == 0) break;
        wait_ticks(100);
    }
    irq_off();
    panic_text(9, 2, "Rebooting into Debug Recovery...", 0x4F);
    *(volatile u32 *)FLAIR_RECOVERY_MARKER_ADDR = FLAIR_RECOVERY_MARKER;
    serial_str("panic: recovery marker stored; rebooting into recovery boot mode\n");
    outb(0x64, 0xFE);
#endif
    for (;;) halt();
}

void cpu_fault(void) { kernel_panic("CPU exception"); }
