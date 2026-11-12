#include "../kernel/textile.h"

typedef unsigned char u8;
typedef unsigned int u32;
typedef int i32;

static u32 sys(u32 n, u32 b, u32 c, u32 d) {
    u32 ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(n), "b"(b), "c"(c), "d"(d) : "memory");
    return ret;
}

void app_main(void);

static void app_entry(void) {
    app_main();
    sys(SYS_EXIT, 0, 0, 0);
    for (;;)
        ;
}

__attribute__((section(".head"), used))
const u32 app_head[2] = { TX_APP_MAGIC, (u32)(unsigned long)app_entry };

static const char title[] = "FlareOS...";

static void put_at(u32 row, u32 col, u8 ch, u32 attr) {
    sys(SYS_PUTAT, row, col, (u32)ch | (attr << 8));
}

static void draw_title(i32 col, u32 attr) {
    for (u32 c = 0; c < 80; c++)
        put_at(12, c, ' ', 0x00);
    for (u32 i = 0; title[i]; i++) {
        i32 x = col + (i32)i;
        if (x >= 0 && x < 80)
            put_at(12, (u32)x, (u8)title[i], attr);
    }
}

#define HEAP_TEST (1024u * 1024u)
#define HEAP_PATTERN 0x5A5A5A5Au

static void brk_check(void) {
    u32 base = sys(SYS_BRK, 0, 0, 0);
    u32 end = base + HEAP_TEST;
    u32 p;
    int ok = (sys(SYS_BRK, end, 0, 0) >= end);

    for (p = base; ok && p < end; p += 4096u) {
        volatile u32 *w = (volatile u32 *)(unsigned long)p;
        w[0] = p;
        w[1023] = p ^ HEAP_PATTERN;
    }
    for (p = base; ok && p < end; p += 4096u) {
        volatile u32 *w = (volatile u32 *)(unsigned long)p;
        if (w[0] != p || w[1023] != (p ^ HEAP_PATTERN))
            ok = 0;
    }
    sys(SYS_PRINT, (u32)(unsigned long)(ok ? "app: brk 1 MB ok\n" : "app: brk FAILED\n"), 0, 0);
}

void app_main(void) {
    sys(SYS_CLEAR, 0, 0, 0);
    brk_check();

    u32 cs;
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
    sys(SYS_PRINT, (u32)((cs & 3) == 3 ? "app: running in ring 3\n" : "app: running in ring 0\n"), 0, 0);

    sys(SYS_ANIM_START, 0u | ((u32)EASE_OUT_CUBIC << 8) | (60u << 16), (u32)(-14), 35u);
    for (u32 f = 0; f < 90; f++) {
        draw_title((i32)sys(SYS_ANIM_VALUE, 0, 0, 0), 0x0B);
        sys(SYS_SLEEP, 1, 0, 0);
    }
    if ((i32)sys(SYS_ANIM_VALUE, 0, 0, 0) == 35)
        sys(SYS_PRINT, (u32)"app: slide-in done, title at col 35\n", 0, 0);

    sys(SYS_TONE, 523, 120, 0);
    sys(SYS_TONE, 659, 120, 0);
    sys(SYS_TONE, 784, 240, 0);
    sys(SYS_PRINT, (u32)"app: chime played\n", 0, 0);

    sys(SYS_ANIM_START, 1u | ((u32)EASE_IN_OUT << 8) | (50u << 16), 0u, 6u);
    for (u32 f = 0; f < 60; f++) {
        i32 level = (i32)sys(SYS_ANIM_VALUE, 1, 0, 0);
        draw_title(35, level < 3 ? 0x0B : 0x0F);
        sys(SYS_SLEEP, 1, 0, 0);
    }
    sys(SYS_PRINT, (u32)"app: pulse done\n", 0, 0);
}
