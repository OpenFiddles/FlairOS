#include "flair_posix.h"

#define AIRP_MAGIC 0x50524941u
#define ZIP_MAGIC  0x04034B50u

static void app_puts(const char *s) { while (s && *s) flare_putc(*s++); }

static int inspect_pkg(const char *path) {
    int fd = posix_open(path, 0u);
    if (fd < 0) return -1;
    unsigned int magic = 0;
    int n = posix_read(fd, &magic, (flare_u32)sizeof(magic));
    posix_close(fd);
    if (n != 4) return -1;
    if (magic == AIRP_MAGIC) return 1;
    if (magic == ZIP_MAGIC) return 2;
    return 0;
}

int main(void) {
    flare_clear();
    app_puts("FlairOS App Manager (.airp)\n");
    const char *pkgs[4] = {
        "/system/priv-app/AirStinUI.airp",
        "/system/priv-app/NetService.airp",
        "/apps/hello.apk",
        "/apps/sysinfo.apk"
    };
    for (int i = 0; i < 4; i++) {
        int kind = inspect_pkg(pkgs[i]);
        app_puts(pkgs[i]);
        app_puts(kind == 1 ? " [AIRP]\n" : (kind == 2 ? " [APK]\n" : " [N/A]\n"));
    }
    return 0;
}
