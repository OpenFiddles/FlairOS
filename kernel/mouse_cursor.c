#include "../kernel/kernel.h"

u32 cursor_x = 10;
u32 cursor_y = 10;
u32 cursor_norm_x = ((10u * 2u + 1u) * 65536u) / (VGA_COLS * 2u);
u32 cursor_norm_y = ((10u * 2u + 1u) * 65536u) / (VGA_ROWS * 2u);
