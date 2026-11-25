#include "flair_posix.h"

static void app_puts(const char *s) { while (s && *s) flare_putc(*s++); }

struct flair_settings_cfg {
    int theme_idx;
    int anim_on;
    int tooltips_on;
    int wifi_on;
    int cell_5g_on;
    int airplane_mode;
};

static struct flair_settings_cfg g_cfg = { 0, 1, 1, 1, 1, 0 };

static void save_prefs(void) {
    int fd = posix_open("/data/data/com.flair.settings/prefs.cfg", 0x241u);
    if (fd >= 0) {
        (void)posix_write(fd, &g_cfg, (flare_u32)sizeof(g_cfg));
        posix_close(fd);
    }
}

int main(void) {
    flare_clear();
    app_puts("FlairOS Better Settings (.airp)\n");
    app_puts("Theme: Indigo | Animations: ON | Wi-Fi + 5G: ON\n");
    save_prefs();
    return 0;
}
