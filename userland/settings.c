#include "../include/axi.h"
#include "../include/fs.h"
#include "../kernel/textile.h"
#define ATTR 0x70u
#define MUTED 0x78u
#define SELECTED 0x1Fu
void app_main(void);
static void app_entry(void) { app_main(); ax_call(SYS_EXIT, 0, 0, 0); for (;;) ; }
__attribute__((section(".head"), used))
const unsigned int app_head[2] = { TX_APP_MAGIC, (unsigned int)(unsigned long)app_entry };

static const char *save_accent(unsigned int accent) {
    unsigned char value = (unsigned char)accent;
    unsigned int h = fs_open("UI.CFG", FS_WRITE);
    if (h == FS_FAIL) return "Could not open UI.CFG.";
    if (fs_write(h, &value, 1) != 1) {
        fs_close(h);
        return "Accent changed, but saving failed.";
    }
    fs_close(h);
    if (fs_is_volatile()) return "Saved for this LiveCD session only.";
    if (!fs_disk_writes_enabled()) return "Saved in RAM; no writable disk is ready.";
    return "Accent saved to UI.CFG.";
}
static void draw(unsigned int win, unsigned int accent, const char *status) {
    ax_clear(win, ATTR);
    ax_text(win, 0, 1, SELECTED, "PERSONALIZATION");
    ax_text(win, 1, 2, ATTR, "Accent colour");
    ax_text(win, 2, 4, accent ? ATTR : SELECTED, "1  Violet");
    ax_text(win, 2, 18, accent ? SELECTED : ATTR, "2  Teal");
    ax_text(win, 4, 1, SELECTED, "DISPLAY");
    ax_text(win, 5, 2, ATTR, "Renderer: AirGraph pixel pipeline");
    ax_text(win, 6, 2, ATTR, "Compositor: WinDown / SprunkUI");
    ax_text(win, 7, 2, MUTED, "Resolution follows the active build profile.");
    ax_text(win, 9, 1, SELECTED, "ABOUT FLAIROS");
    ax_text(win, 10, 2, ATTR, "FlairOS V2  |  .airp API level 2");
    ax_text(win, 11, 2, MUTED, status);
}
void app_main(void) {
    unsigned int win = ax_open(8, 3, 64, 13, "Settings");
    unsigned int accent = 0, h;
    unsigned char saved = 0;
    const char *status = "Choose accent with 1 or 2. Esc closes.";
    if (win == AX_FAIL) return;
    h = fs_open("UI.CFG", FS_READ);
    if (h != FS_FAIL) {
        unsigned int n = fs_read(h, &saved, 1);
        fs_close(h);
        if (n == 1 && saved <= 1) {
            accent = saved;
            status = "Loaded saved accent from UI.CFG.";
        }
    }
    if (ax_set_accent(accent) != 0)
        status = "Could not apply the saved accent.";
    draw(win, accent, status);
    while (ax_alive(win)) {
        unsigned int key;
        ax_present();
        key = ax_call(SYS_GETKEY, 0, 0, 0);
        if (key == 27) break;
        if (key == '1' || key == '2' || key == 'r' || key == 'R') {
            accent = (key == '2') ? 1u : 0u;
            if (ax_set_accent(accent) == 0)
                status = save_accent(accent);
            else
                status = "Could not update accent.";
            draw(win, accent, status);
        }
        {
            unsigned int click = ax_click(win);
            if (click != AX_FAIL && AX_CLICK_ROW(click) == 2) {
                unsigned int col = AX_CLICK_COL(click);
                if (col >= 4 && col < 16) accent = 0;
                else if (col >= 18 && col < 30) accent = 1;
                else continue;
                if (ax_set_accent(accent) == 0)
                    status = save_accent(accent);
                else
                    status = "Could not update accent.";
                draw(win, accent, status);
            }
        }
        ax_call(SYS_SLEEP, 1, 0, 0);
    }
    ax_close(win);
}
