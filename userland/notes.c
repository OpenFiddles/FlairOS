#include "../include/axi.h"
#include "../include/fs.h"
#include "../kernel/textile.h"
#define ATTR 0x70u
#define SELECTED 0x1Fu
#define MAX_NOTE 48u
void app_main(void);
static void app_entry(void) { app_main(); ax_call(SYS_EXIT, 0, 0, 0); for (;;) ; }
__attribute__((section(".head"), used))
const unsigned int app_head[2] = { TX_APP_MAGIC, (unsigned int)(unsigned long)app_entry };
static unsigned int length;
static char note[MAX_NOTE + 1];
static const char *status = "Type a note. Ctrl+S saves NOTE.TXT.";
static void draw(unsigned int win) {
    ax_clear(win, ATTR);
    ax_text(win, 0, 1, SELECTED, "QUICK NOTE");
    ax_text(win, 2, 1, ATTR, note[0] ? note : "Your note appears here...");
    ax_text(win, 5, 1, ATTR, "Type to edit. Backspace removes a character.");
    ax_text(win, 7, 1, ATTR, "Ctrl+S  Save to NOTE.TXT");
    ax_text(win, 9, 1, ATTR, "Esc      Close");
    ax_text(win, 11, 1, 0x78u, status);
}
static void load_note(void) {
    unsigned int h = fs_open("NOTE.TXT", FS_READ);
    length = 0;
    note[0] = 0;
    if (h != FS_FAIL) {
        length = fs_read(h, note, MAX_NOTE);
        fs_close(h);
        if (length > MAX_NOTE) length = MAX_NOTE;
        note[length] = 0;
        status = "Loaded NOTE.TXT. Ctrl+S saves changes.";
    } else {
        status = "Type a note. Ctrl+S saves NOTE.TXT.";
    }
}
static void save_note(void) {
    unsigned int h = fs_open("NOTE.TXT", FS_WRITE);
    if (h == FS_FAIL) {
        status = "Could not open NOTE.TXT.";
        return;
    }
    if (fs_write(h, note, length) != length) {
        fs_close(h);
        status = "Save failed.";
        return;
    }
    fs_close(h);
    if (fs_is_volatile()) status = "Saved for this LiveCD session only.";
    else if (!fs_disk_writes_enabled()) status = "Saved in RAM; no writable disk is ready.";
    else status = "Saved NOTE.TXT to disk.";
}
void app_main(void) {
    unsigned int win = ax_open(8, 4, 64, 13, "Notes");
    if (win == AX_FAIL) return;
    load_note();
    draw(win);
    while (ax_alive(win)) {
        unsigned int key;
        ax_present();
        key = ax_call(SYS_GETKEY, 0, 0, 0);
        if (key == 27) break;
        if (key == 19) {
            save_note();
            draw(win);
        } else if (key == 8 || key == 127) {
            if (length) note[--length] = 0;
            draw(win);
        } else if (key >= 32 && key < 127 && length < MAX_NOTE) {
            note[length++] = (char)key;
            note[length] = 0;
            draw(win);
        }
        (void)ax_click(win);
        ax_call(SYS_SLEEP, 1, 0, 0);
    }
    ax_close(win);
}
