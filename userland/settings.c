/* FlairOS Settings — compact Material 3 system preferences. */
#include "../include/axi.h"
#include "../include/fs.h"
#include "../kernel/textile.h"

#define ATTR     0x70u
#define MUTED    0x78u
#define SELECTED 0x1Fu
#define ACCENT   0x2Fu
#define DANGER   0x4Fu

void app_main(void);
static void app_entry(void) { app_main(); ax_call(SYS_EXIT, 0, 0, 0); for (;;) ; }
__attribute__((section(".head"), used))
const unsigned int app_head[2] = { TX_APP_MAGIC, (unsigned int)(unsigned long)app_entry };

static void number(char *out, unsigned int value) {
    char tmp[10]; unsigned int n = 0;
    do { tmp[n++] = (char)('0' + value % 10u); value /= 10u; }
    while (value && n < sizeof(tmp));
    while (n) *out++ = tmp[--n];
    *out = 0;
}
static const char *save_cfg(unsigned int accent, unsigned int profile,
                            unsigned int touch, unsigned int anim,
                            unsigned int gpu_allowed) {
    unsigned char cfg[5];
    cfg[0] = (unsigned char)accent;
    cfg[1] = (unsigned char)profile;
    cfg[2] = (unsigned char)touch;
    cfg[3] = (unsigned char)anim;
    cfg[4] = (unsigned char)gpu_allowed;
    unsigned int h = fs_open("UI.CFG", FS_WRITE);
    if (h == FS_FAIL) return "Could not open UI.CFG.";
    if (fs_write(h, cfg, 5) != 5) {
        fs_close(h);
        return "Applied, but saving UI.CFG failed.";
    }
    fs_close(h);
    if (fs_is_volatile()) return "Saved for this LiveCD session only.";
    if (!fs_disk_writes_enabled()) return "Saved in RAM; no writable disk is ready.";
    return "Preferences saved to UI.CFG.";
}
static void draw(unsigned int win, unsigned int accent, unsigned int profile,
                 unsigned int touch, unsigned int anim, unsigned int gpu_allowed,
                 const char *status) {
    char line[65], n[12];
    unsigned int st = ax_ui_status();
    int ainfo = ax_set_anim(0xFFu);
    unsigned int gpu_in_use = (ainfo >= 0) ? (((unsigned int)ainfo >> 1) & 1u) : 0u;
    unsigned int gpu_present = (ainfo >= 0) ? (((unsigned int)ainfo >> 3) & 1u) : 0u;
    unsigned int net_present = st & 1u;
    unsigned int net_up = (st >> 1) & 1u;
    unsigned int bat_present = (st >> 2) & 1u;
    unsigned int ac_online = (st >> 3) & 1u;
    unsigned int bat_pct = (st >> 4) & 0x7Fu;
    unsigned int vram_mb = (st >> 12) & 0xFFu;
    unsigned int virt_4k = (st >> 20) & 1u;

    for (unsigned int i = 0; i < 64; ++i) line[i] = ' ';
    line[64] = 0;
    ax_clear(win, ATTR);
    for (unsigned int i=0;i<15;i++) line[2+i]="SYSTEM SETTINGS"[i];
    for (unsigned int i=0;i<10;i++) line[52+i]="MATERIAL 3"[i];
    ax_text(win, 0, 0, ACCENT, line);

    ax_text(win, 1, 2, SELECTED, "APPEARANCE & MOTION");
    ax_text(win, 2, 4, accent == 0 ? SELECTED : ATTR, "[1] Violet");
    ax_text(win, 2, 16, accent == 1 ? SELECTED : ATTR, "[2] Teal");
    ax_text(win, 2, 27, anim ? SELECTED : ATTR, anim ? "[9] Anim:ON" : "[9] Anim:OFF");
    ax_text(win, 2, 42, gpu_allowed ? SELECTED : ATTR, gpu_allowed ? "[G] GPU:ON" : "[G] GPU:OFF");

    ax_text(win, 3, 2, SELECTED, "DISPLAY & FORM FACTOR");
    ax_text(win, 4, 4, profile == 0 ? SELECTED : ATTR, "[3] Desk 800x600");
    ax_text(win, 4, 23, profile == 1 ? SELECTED : ATTR, "[4] Phone 600x750");
    ax_text(win, 4, 43, profile == 2 ? SELECTED : ATTR, "[5] Lap 1024x768");
    ax_text(win, 5, 4, profile == 3 ? SELECTED : ATTR, "[6] TV 1280x720");
    ax_text(win, 5, 23, profile == 4 ? SELECTED : ATTR, "[0] 4K 3840x2160");

    ax_text(win, 6, 2, SELECTED, "INPUT");
    ax_text(win, 7, 4, touch == 0 ? SELECTED : ATTR, "[7] Mouse / keyboard");
    ax_text(win, 7, 30, touch == 1 ? SELECTED : ATTR, "[8] Touch");
    ax_text(win, 7, 44, MUTED, "R Rotate");

    ax_text(win, 8, 2, SELECTED, "SYSTEM STATUS");
    ax_text(win, 9, 4, net_up ? ACCENT : MUTED,
            net_up ? "Net: ETH UP" : (net_present ? "Net: NO LINK" : "Net: NONE"));
    if (bat_present) {
        ax_text(win, 9, 18, ATTR, "Pwr: BAT");
        number(n, bat_pct);
        ax_text(win, 9, 27, ATTR, n);
        ax_text(win, 9, 30, ATTR, "%");
    } else {
        ax_text(win, 9, 18, ac_online ? ACCENT : MUTED, "Pwr: AC");
    }
    ax_text(win, 9, 33, gpu_in_use ? ACCENT : MUTED,
            gpu_in_use ? "Comp: GPU" : (gpu_present ? "Comp: CPU*" : "Comp: CPU"));
    ax_text(win, 9, 45, MUTED, "VRAM:");
    number(n, vram_mb);
    ax_text(win, 9, 51, ATTR, n);
    ax_text(win, 9, 55, MUTED, virt_4k ? "M+4K" : "MB");

    ax_text(win, 10, 4, MUTED, "Slots");
    unsigned int free_slots = fs_slots();
    if (free_slots == FS_FAIL) free_slots = 0;
    number(n, free_slots);
    ax_text(win, 10, 11, ATTR, n);
    ax_text(win, 10, 15, MUTED, "/");
    number(n, FS_FILES);
    ax_text(win, 10, 17, ATTR, n);
    ax_text(win, 10, 22, MUTED, "available");
    ax_text(win, 10, 43, fs_is_volatile() ? DANGER : ACCENT,
            fs_is_volatile() ? "LIVECD" :
            (fs_disk_writes_enabled() ? "DISK" : "RAM"));

    ax_text(win, 11, 2, ACCENT, "S Save");
    ax_text(win, 11, 12, ATTR, "D Defaults");
    ax_text(win, 11, 25, MUTED, status);
}
void app_main(void) {
    unsigned int win = ax_open(8, 3, 64, 13, "Settings");
    unsigned int accent = 0, profile = 0, touch = 0, anim = 1, gpu_allowed = 1, h;
    unsigned char cfg[5] = {0, 0, 0, 1, 1};
    const char *status = "Changes apply immediately. S saves.";
    if (win == AX_FAIL) return;
    {
        int p = ax_set_display(0xFFu), t = ax_set_touch(0xFFu), a = ax_set_anim(0xFFu);
        if (p >= 0 && p <= 4) profile = (unsigned int)p;
        if (t >= 0 && t <= 1) touch = (unsigned int)t;
        if (a >= 0) {
            anim = (unsigned int)a & 1u;
            gpu_allowed = ((unsigned int)a >> 2) & 1u;
        }
    }
    h = fs_open("UI.CFG", FS_READ);
    if (h != FS_FAIL) {
        unsigned int n = fs_read(h, cfg, 5);
        fs_close(h);
        if (n >= 1 && cfg[0] <= 1) accent = cfg[0];
        if (n >= 2 && cfg[1] <= 4) profile = cfg[1];
        if (n >= 3 && cfg[2] <= 1) touch = cfg[2];
        if (n >= 4 && cfg[3] <= 1) anim = cfg[3];
        if (n >= 5 && cfg[4] <= 1) gpu_allowed = cfg[4];
        status = "Loaded preferences from UI.CFG.";
    }
    ax_set_accent(accent);
    ax_set_display(profile);
    ax_set_touch(touch);
    ax_set_anim(anim);
    ax_set_anim(gpu_allowed ? 3u : 2u);
    draw(win, accent, profile, touch, anim, gpu_allowed, status);
    while (ax_alive(win)) {
        ax_present();
        unsigned int key = ax_call(SYS_GETKEY, 0, 0, 0);
        int changed = 0, save = 0;
        if (key == 27) break;
        if (key == '1') { accent = 0; changed = 1; }
        else if (key == '2') { accent = 1; changed = 1; }
        else if (key == '3') { profile = 0; changed = 1; }
        else if (key == '4') { profile = 1; changed = 1; }
        else if (key == '5') { profile = 2; changed = 1; }
        else if (key == '6') { profile = 3; changed = 1; }
        else if (key == '0') { profile = 4; changed = 1; }
        else if (key == '7') { touch = 0; changed = 1; }
        else if (key == '8') { touch = 1; changed = 1; }
        else if (key == '9') { anim = anim ? 0u : 1u; changed = 1; }
        else if (key == 'g' || key == 'G') { gpu_allowed = gpu_allowed ? 0u : 1u; changed = 1; }
        else if (key == 's' || key == 'S') save = 1;
        else if (key == 'd' || key == 'D') {
            accent = 0; profile = 0; touch = 0; anim = 1; gpu_allowed = 1;
            changed = 1; save = 1;
        } else if (key == 'r' || key == 'R') {
            if (ax_set_display(0xFEu) == 0) {
                int p = ax_set_display(0xFFu);
                if (p >= 0 && p <= 4) profile = (unsigned int)p;
                status = "Display orientation changed.";
            } else status = "Rotation is unavailable for this device.";
            save = 1;
        }
        unsigned int click = ax_click(win);
        if (click != AX_FAIL) {
            unsigned int row = AX_CLICK_ROW(click), col = AX_CLICK_COL(click);
            if (row == 2 && col >= 4 && col < 15) { accent = 0; changed = 1; }
            else if (row == 2 && col >= 16 && col < 26) { accent = 1; changed = 1; }
            else if (row == 2 && col >= 27 && col < 41) { anim = anim ? 0u : 1u; changed = 1; }
            else if (row == 2 && col >= 42) { gpu_allowed = gpu_allowed ? 0u : 1u; changed = 1; }
            else if (row == 4 && col >= 4 && col < 21) { profile = 0; changed = 1; }
            else if (row == 4 && col >= 23 && col < 41) { profile = 1; changed = 1; }
            else if (row == 4 && col >= 43) { profile = 2; changed = 1; }
            else if (row == 5 && col >= 4 && col < 21) { profile = 3; changed = 1; }
            else if (row == 5 && col >= 23) { profile = 4; changed = 1; }
            else if (row == 7 && col >= 4 && col < 27) { touch = 0; changed = 1; }
            else if (row == 7 && col >= 30 && col < 42) { touch = 1; changed = 1; }
            else if (row == 11 && col >= 2 && col < 10) save = 1;
            else if (row == 11 && col >= 12 && col < 23) {
                accent = 0; profile = 0; touch = 0; anim = 1; gpu_allowed = 1;
                changed = 1; save = 1;
            }
        }
        if (changed) {
            ax_set_accent(accent);
            if (ax_set_display(profile) != 0)
                status = "Display mode unsupported on this device.";
            else {
                ax_set_touch(touch);
                ax_set_anim(anim);
                ax_set_anim(gpu_allowed ? 3u : 2u);
                status = "Applied. Press S to save.";
            }
        }
        if (save) status = save_cfg(accent, profile, touch, anim, gpu_allowed);
        if (changed || save) draw(win, accent, profile, touch, anim, gpu_allowed, status);
        ax_call(SYS_SLEEP, 1, 0, 0);
    }
    ax_close(win);
}
