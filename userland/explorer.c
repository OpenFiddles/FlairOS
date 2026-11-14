/*
 * FlareOS File Explorer
 *
 * UI contract: all drawing passes through ex_ui_* helpers. SprunkUI can
 * later replace this backend with reusable widgets without changing the
 * filesystem/controller code below it.
 */
#include "../include/axi.h"
#include "../include/fs.h"
#include "../kernel/textile.h"
#include "../sdk/stdlib.h"
#include "../sdk/string.h"

#define EX_ATTR          0x70u
#define EX_SELECTED      0x1Fu
#define EX_MUTED         0x78u
#define EX_ACCENT        0x2Fu
#define EX_ROWS          8u
#define EX_VISIBLE       8u
#define EX_KEY_ESC       27
#define EX_KEY_ENTER     13
#define EX_KEY_BACKSPACE 8

struct ex_file {
    char name[FS_NAME_MAX + 1];
    unsigned int size;
};

struct ex_state {
    unsigned int win;
    struct ex_file files[FS_FILES];
    unsigned int count;
    unsigned int selected;
    unsigned int first;
    unsigned int view_pos;
    unsigned int length;
    char *buffer;
    char note[41];
    int viewing;
    int dirty;
};

static const char ex_welcome[] =
    "Welcome to FlairOS Explorer.\n"
    "Use N to create a note, arrows to navigate, Enter to open.\n";

void app_main(void);

static void app_entry(void) {
    app_main();
    ax_call(SYS_EXIT, 0, 0, 0);
    for (;;) ;
}

__attribute__((section(".head"), used))
const unsigned int app_head[2] = {
    TX_APP_MAGIC, (unsigned int)(unsigned long)app_entry
};

/* ---------- Small, allocation-free formatting helpers ---------- */

static void ex_blank(char *line) {
    unsigned int i;
    for (i = 0; i < 40; ++i) line[i] = ' ';
    line[40] = 0;
}

static unsigned int ex_len(const char *s) {
    unsigned int n = 0;
    while (s && s[n]) ++n;
    return n;
}

static void ex_copy(char *dst, const char *src, unsigned int cap) {
    unsigned int i = 0;
    if (!cap) return;
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static void ex_number(char *out, unsigned int value) {
    char tmp[11];
    unsigned int n = 0;
    do {
        tmp[n++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value && n < sizeof(tmp));
    while (n) *out++ = tmp[--n];
    *out = 0;
}

static void ex_put(char *line, unsigned int col, const char *s, unsigned int max) {
    unsigned int i = 0;
    while (s && s[i] && i < max && col + i < 40) {
        line[col + i] = s[i];
        ++i;
    }
}

static void ex_draw(unsigned int row, unsigned int attr, const char *line) {
    ax_text(0, row, 0, attr, line);
}

/* ---------- SprunkUI-ready presentation backend ---------- */

static void ex_ui_header(struct ex_state *s, const char *section) {
    char line[41];
    ex_blank(line);
    ex_put(line, 1, "FLAREOS", 7);
    ex_put(line, 10, "/", 1);
    ex_put(line, 12, section, 18);
    ex_put(line, 32, "EXPLORER", 8);
    ex_draw(0, EX_ACCENT, line);
    (void)s;
}

static void ex_ui_status(struct ex_state *s) {
    char line[41];
    ex_blank(line);
    ex_put(line, 1, s->note, 38);
    ex_draw(10, EX_MUTED, line);
}

static void ex_ui_footer(struct ex_state *s, const char *left, const char *right) {
    char line[41];
    ex_blank(line);
    ex_put(line, 1, left, 25);
    ex_put(line, 28, right, 11);
    ex_draw(11, EX_ACCENT, line);
    (void)s;
}

static void ex_ui_list(struct ex_state *s) {
    char line[41], number[11], size[11];
    unsigned int i, index, free_count;
    ex_ui_header(s, "Files");
    ex_blank(line);
    ex_put(line, 2, "NAME", 12);
    ex_put(line, 23, "SIZE", 8);
    ex_put(line, 32, "TYPE", 5);
    ex_draw(1, EX_MUTED, line);

    for (i = 0; i < EX_VISIBLE; ++i) {
        index = s->first + i;
        ex_blank(line);
        if (index < s->count) {
            ex_put(line, 1, index == s->selected ? ">" : " ", 1);
            ex_put(line, 3, s->files[index].name, 18);
            ex_number(size, s->files[index].size);
            ex_put(line, 23, size, 8);
            ex_put(line, 33, "FILE", 4);
        } else if (i == 0 && s->count == 0) {
            ex_put(line, 3, "This folder is empty.", 24);
        }
        ex_draw(2 + i, index == s->selected && index < s->count
                ? EX_SELECTED : EX_ATTR, line);
    }

    ex_blank(line);
    free_count = fs_slots();
    ex_put(line, 1, "ITEMS", 5);
    ex_number(number, s->count);
    ex_put(line, 7, number, 3);
    ex_put(line, 13, "FREE", 4);
    ex_number(number, free_count == FS_FAIL ? 0 : free_count);
    ex_put(line, 18, number, 3);
    ex_put(line, 25, "MAX 16", 6);
    ex_draw(9, EX_MUTED, line);

    ex_ui_status(s);
    ex_ui_footer(s, "N New  Enter Open  D Delete", "Esc Close");
}

static void ex_ui_reader(struct ex_state *s) {
    char line[41], pos[11], total[11];
    unsigned int row, col, cursor;
    ex_ui_header(s, s->files[s->selected].name);
    ex_blank(line);
    ex_put(line, 1, "CONTENT", 7);
    ex_put(line, 29, "TEXT VIEW", 9);
    ex_draw(1, EX_MUTED, line);

    cursor = s->view_pos;
    for (row = 0; row < EX_ROWS; ++row) {
        ex_blank(line);
        col = 0;
        while (cursor < s->length && col < 38) {
            char ch = s->buffer[cursor++];
            if (ch == '\n') break;
            if (ch == '\r') continue;
            if ((unsigned char)ch < 32u) ch = '.';
            line[1 + col++] = ch;
        }
        ex_draw(2 + row, EX_ATTR, line);
    }

    ex_blank(line);
    ex_number(pos, s->view_pos);
    ex_number(total, s->length);
    ex_put(line, 1, "BYTE", 4);
    ex_put(line, 6, pos, 10);
    ex_put(line, 18, "OF", 2);
    ex_put(line, 21, total, 10);
    ex_draw(10, EX_MUTED, line);
    ex_ui_footer(s, "Arrows Scroll  D Delete", "Esc Back");
}

static void ex_status(struct ex_state *s, const char *message) {
    ex_copy(s->note, message, sizeof(s->note));
}

/* ---------- Filesystem controller ---------- */

static void ex_refresh(struct ex_state *s) {
    unsigned int i, size;
    s->count = 0;
    for (i = 0; i < FS_FILES; ++i) {
        size = fs_list(i, s->files[s->count].name);
        if (size != FS_FAIL) {
            s->files[s->count].size = size;
            ++s->count;
        }
    }
    if (s->selected >= s->count)
        s->selected = s->count ? s->count - 1 : 0;
    if (s->selected < s->first) s->first = s->selected;
    if (s->selected >= s->first + EX_VISIBLE)
        s->first = s->selected - EX_VISIBLE + 1;
}

static void ex_open_selected(struct ex_state *s) {
    unsigned int handle, got;
    if (s->selected >= s->count) return;
    handle = fs_open(s->files[s->selected].name, FS_READ);
    if (handle == FS_FAIL) {
        ex_status(s, "Unable to open this file.");
        return;
    }
    got = fs_read(handle, s->buffer, FS_FILE_MAX);
    fs_close(handle);
    if (got == FS_FAIL) {
        ex_status(s, "Read failed. File remains unchanged.");
        return;
    }
    s->length = got;
    s->view_pos = 0;
    s->viewing = 1;
    ex_status(s, "Read-only preview. Press Esc to return.");
}

static void ex_create(struct ex_state *s) {
    char name[FS_NAME_MAX + 1];
    unsigned int n, handle;
    if (fs_slots() == 0 || fs_slots() == FS_FAIL) {
        ex_status(s, "Storage is full. Delete a file first.");
        return;
    }
    for (n = 1; n <= 99; ++n) {
        name[0] = 'n'; name[1] = 'o'; name[2] = 't'; name[3] = 'e';
        name[4] = (char)('0' + n / 10);
        name[5] = (char)('0' + n % 10);
        name[6] = '.'; name[7] = 't'; name[8] = 'x'; name[9] = 't';
        name[10] = 0;
        if (fs_exists(name)) continue;
        handle = fs_open(name, FS_WRITE);
        if (handle == FS_FAIL) {
            ex_status(s, "Could not create the file.");
            return;
        }
        if (fs_write(handle, ex_welcome, sizeof(ex_welcome) - 1) == FS_FAIL) {
            fs_close(handle);
            fs_delete(name);
            ex_status(s, "Write failed; partial file removed.");
            return;
        }
        fs_close(handle);
        ex_status(s, "Created a new text file.");
        ex_refresh(s);
        for (n = 0; n < s->count; ++n)
            if (strcmp(s->files[n].name, name) == 0) {
                s->selected = n;
                break;
            }
        return;
    }
    ex_status(s, "No available note filename.");
}

static void ex_delete(struct ex_state *s) {
    char message[41];
    if (s->selected >= s->count) return;
    ex_copy(message, "Deleted: ", sizeof(message));
    ex_put(message, 9, s->files[s->selected].name, 12);
    message[21] = 0;
    if (fs_delete(s->files[s->selected].name) == FS_FAIL) {
        ex_status(s, "Delete failed. File was not removed.");
        return;
    }
    ex_status(s, message);
    s->viewing = 0;
    ex_refresh(s);
}

static void ex_move(struct ex_state *s, int delta) {
    if (!s->count) return;
    if (delta < 0 && s->selected) --s->selected;
    if (delta > 0 && s->selected + 1 < s->count) ++s->selected;
    if (s->selected < s->first) s->first = s->selected;
    if (s->selected >= s->first + EX_VISIBLE)
        s->first = s->selected - EX_VISIBLE + 1;
}

static void ex_scroll(struct ex_state *s, int delta) {
    unsigned int step = 32u;
    if (delta < 0) s->view_pos = s->view_pos > step ? s->view_pos - step : 0;
    else if (s->view_pos + step < s->length) s->view_pos += step;
    else s->view_pos = s->length;
}

static void ex_key(struct ex_state *s, int key) {
    if (key == EX_KEY_ESC) {
        if (s->viewing) {
            s->viewing = 0;
            ex_status(s, "Back to files.");
        } else {
            s->dirty = 1;
        }
        return;
    }
    if (s->viewing) {
        if (key == ARROW_UP || key == 'w' || key == 'W') ex_scroll(s, -1);
        else if (key == ARROW_DOWN || key == 's' || key == 'S') ex_scroll(s, 1);
        else if (key == 'd' || key == 'D') ex_delete(s);
        return;
    }
    if (key == ARROW_UP || key == 'w' || key == 'W') ex_move(s, -1);
    else if (key == ARROW_DOWN || key == 's' || key == 'S') ex_move(s, 1);
    else if (key == EX_KEY_ENTER) ex_open_selected(s);
    else if (key == 'n' || key == 'N') ex_create(s);
    else if (key == 'd' || key == 'D') ex_delete(s);
}

static void ex_click(struct ex_state *s, unsigned int row, unsigned int col) {
    if (s->viewing) {
        if (row == 11 && col < 8) s->viewing = 0;
        else if (row == 11 && col >= 8 && col < 21) ex_delete(s);
        else if (row >= 2 && row < 10) ex_scroll(s, 1);
        return;
    }
    if (row >= 2 && row < 2 + EX_VISIBLE) {
        unsigned int index = s->first + row - 2;
        if (index < s->count) {
            if (index == s->selected) ex_open_selected(s);
            else s->selected = index;
        }
    } else if (row == 11 && col >= 1 && col < 8) ex_create(s);
    else if (row == 11 && col >= 9 && col < 19) ex_delete(s);
}

void app_main(void) {
    struct ex_state *s;
    int key;
    unsigned int click;

    s = (struct ex_state *)malloc(sizeof(*s));
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->buffer = (char *)malloc(FS_FILE_MAX);
    if (!s->buffer) {
        free(s);
        return;
    }

    s->win = ax_open(8, 2, 40, 14, "FlairOS Explorer");
    if (s->win == AX_FAIL) {
        free(s->buffer);
        free(s);
        return;
    }

    ex_status(s, "Ready. Select a file or create a new note.");
    ex_refresh(s);
    while (!s->dirty && ax_alive(s->win)) {
        if (s->viewing) ex_ui_reader(s);
        else ex_ui_list(s);
        ax_present();

        key = (int)ax_call(SYS_GETKEY, 0, 0, 0);
        if (key > 0) ex_key(s, key);
        click = ax_click(s->win);
        if (click != AX_FAIL)
            ex_click(s, AX_CLICK_ROW(click), AX_CLICK_COL(click));
        ax_call(SYS_SLEEP, 1, 0, 0);
    }

    ax_close(s->win);
    free(s->buffer);
    free(s);
}
