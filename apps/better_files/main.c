#include "flair_posix.h"

static void app_puts(const char *s) { while (s && *s) flare_putc(*s++); }

static int copy_file(const char *src, const char *dst) {
    int in_fd = posix_open(src, 0u);
    if (in_fd < 0) return -1;
    int out_fd = posix_open(dst, 0x241u);
    if (out_fd < 0) { posix_close(in_fd); return -1; }
    char buf[128];
    int total = 0;
    for (;;) {
        int n = posix_read(in_fd, buf, (flare_u32)sizeof(buf));
        if (n <= 0) break;
        (void)posix_write(out_fd, buf, (flare_u32)n);
        total += n;
    }
    posix_close(in_fd);
    posix_close(out_fd);
    return total;
}

int main(void) {
    flare_clear();
    app_puts("FlairOS Better Files (.airp)\n");
    (void)posix_mkdir("/home/backups");
    int copied = copy_file("/home/NOTE.TXT", "/home/backups/NOTE.TXT");
    app_puts(copied >= 0 ? "Backup OK: /home/backups/NOTE.TXT\n" : "Backup skipped\n");
    return 0;
}
