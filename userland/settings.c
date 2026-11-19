#include "../include/axi.h"
#include "../kernel/textile.h"
#define ATTR 0x70u
#define MUTED 0x78u
#define SELECTED 0x1Fu
void app_main(void);
static void app_entry(void) { app_main(); ax_call(SYS_EXIT, 0, 0, 0); for (;;) ; }
__attribute__((section(".head"), used))
const unsigned int app_head[2] = { TX_APP_MAGIC, (unsigned int)(unsigned long)app_entry };
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
    unsigned int win = ax_open(8, 3, 64, 13, "Settings"), accent = 0;
    const char *status = "Choose accent with 1 or 2. Esc closes.";
    if (win == AX_FAIL) return;
    draw(win, accent, status);
    while (ax_alive(win)) {
        unsigned int key;
        ax_present();
        key = ax_call(SYS_GETKEY, 0, 0, 0);
        if (key == 27) break;
        if (key == '1' || key == '2') {
            accent = key == '2';
            status = ax_set_accent(accent) == 0 ? "Accent updated for this session." : "Could not update accent.";
            draw(win, accent, status);
        } else if (key == 'r' || key == 'R') {
            accent = 0; ax_set_accent(0); status = "Default accent restored."; draw(win, accent, status);
        }
        (void)ax_click(win);
        ax_call(SYS_SLEEP, 1, 0, 0);
    }
    ax_close(win);
}
