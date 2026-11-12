#include "../../sdk/flare_libc.h"

FLARE_APP();

static unsigned char arena[512] __attribute__((aligned(8)));

void app_main(void) {
    flare_heap_t heap;
    flare_file_t f;
    char *line;

    flare_heap_init(&heap, arena, sizeof arena);
    flare_clear();
    flare_print("libc demo: heap and files\n");

    line = flare_heap_zalloc(&heap, 32);
    if (!line) {
        flare_print("libc demo: heap empty\n");
        return;
    }
    flare_strlcpy(line, "hello from libc", 32);
    flare_print(line);
    flare_putc('\n');

    if (flare_fopen(&f, "libc.txt", 'w') == 0) {
        flare_fputs(&f, line);
        flare_fputs(&f, "\n");
        flare_fclose(&f);
    }

    if (flare_fopen(&f, "libc.txt", 'r') == 0) {
        char back[32];
        flare_u32 n = flare_fread(&f, back, sizeof back - 1);
        back[n] = 0;
        flare_print("read back: ");
        flare_print(back);
        flare_fclose(&f);
    }

    flare_print("free file slots: ");
    flare_print_uint(flare_fs_slots());
    flare_putc('\n');
}
