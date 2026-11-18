/*
 * FlareOS GUI Terminal - standalone userland program.
 * Shell parsing, command history, input editing and output rendering live here.
 * The kernel is used only through the public syscall and filesystem interfaces.
 */
#include "../../include/axi.h"
#include "../../include/fs.h"
#include "../../kernel/textile.h"
#include "../../sdk/stdlib.h"
#include "../../sdk/string.h"

#define TERM_ROWS 12
#define TERM_COLS 64
#define LOG_LINES 64
#define INPUT_MAX 61
#define HISTORY 8
#define ATTR_BODY 0x70
#define ATTR_HEAD 0x2F
#define ATTR_INPUT 0x1F

static unsigned int win;
static char logbuf[LOG_LINES][INPUT_MAX + 1];
static unsigned int log_count;
static char input[INPUT_MAX + 1];
static unsigned int input_len, cursor;
static char history[HISTORY][INPUT_MAX + 1];
static unsigned int history_count, history_next;
static int history_pos = -1;
static int exit_requested;

static void log_line(const char *s) {
    unsigned int i = 0;
    if (log_count == LOG_LINES) {
        for (i = 1; i < LOG_LINES; ++i)
            memcpy(logbuf[i - 1], logbuf[i], INPUT_MAX + 1);
        --log_count;
    }
    i = 0;
    while (s && s[i] && i < INPUT_MAX) {
        logbuf[log_count][i] = s[i];
        ++i;
    }
    logbuf[log_count][i] = 0;
    ++log_count;
}

static void draw(void) {
    unsigned int rows = TERM_ROWS - 2;
    unsigned int first = log_count > rows ? log_count - rows : 0;
    unsigned int i;
    char line[TERM_COLS + 1];
    ax_clear(win, ATTR_BODY);
    ax_text(win, 0, 0, ATTR_HEAD, " FlareOS Terminal | Userland");
    for (i = 0; i < rows && first + i < log_count; ++i)
        ax_text(win, i + 1, 0, ATTR_BODY, logbuf[first + i]);
    memset(line, 0, sizeof(line));
    line[0] = '>';
    line[1] = ' ';
    {
        unsigned int out = 2;
        line[0] = '>';
        line[1] = ' ';
        for (i = 0; i < input_len && out < TERM_COLS; ++i) {
            if (i == cursor && out < TERM_COLS) line[out++] = '|';
            if (out < TERM_COLS) line[out++] = input[i];
        }
        if (cursor == input_len && out < TERM_COLS) line[out++] = '|';
        line[out] = 0;
    }
    ax_text(win, TERM_ROWS - 1, 0, ATTR_INPUT, line);
    ax_present();
}

static int same(const char *a, const char *b) { return strcmp(a, b) == 0; }

static void save_history(void) {
    unsigned int last;
    if (!input_len) return;
    if (history_count) {
        last = (history_next + HISTORY - 1) % HISTORY;
        if (same(history[last], input)) return;
    }
    memcpy(history[history_next], input, input_len + 1);
    history_next = (history_next + 1) % HISTORY;
    if (history_count < HISTORY) ++history_count;
}

static void load_history(int direction) {
    unsigned int index, n;
    if (!history_count) return;
    if (history_pos < 0) history_pos = 0;
    else history_pos += direction;
    if (history_pos < 0) history_pos = 0;
    if (history_pos >= (int)history_count) {
        history_pos = -1;
        input_len = cursor = 0;
        input[0] = 0;
        return;
    }
    index = (history_next + HISTORY - 1 - (unsigned int)history_pos) % HISTORY;
    n = strlen(history[index]);
    memcpy(input, history[index], n + 1);
    input_len = cursor = n;
}

static void cat_file(const char *name) {
    unsigned int h = fs_open(name, FS_READ);
    char chunk[128], line[INPUT_MAX + 1];
    unsigned int used = 0, got;
    if (h == FS_FAIL) {
        log_line("cat: cannot open file");
        return;
    }
    for (;;) {
        got = fs_read(h, chunk, sizeof(chunk));
        if (got == FS_FAIL) {
            log_line("cat: read failed");
            break;
        }
        if (!got) break;
        for (unsigned int i = 0; i < got; ++i) {
            char ch = chunk[i];
            if (ch == '\r') continue;
            if (ch == '\n') {
                line[used] = 0;
                log_line(line);
                used = 0;
                continue;
            }
            if (used == INPUT_MAX) {
                line[used] = 0;
                log_line(line);
                used = 0;
            }
            line[used++] = ch;
        }
    }
    if (used) {
        line[used] = 0;
        log_line(line);
    }
    fs_close(h);
}

static void log_file_size(const char *name) {
    unsigned int h = fs_open(name, FS_READ);
    unsigned int size, value, digits_count = 0, n = 0;
    char digits[11], out[INPUT_MAX + 1];
    if (h == FS_FAIL) {
        log_line("size: cannot open file");
        return;
    }
    size = fs_size(h);
    fs_close(h);
    if (size == FS_FAIL) {
        log_line("size: unable to read size");
        return;
    }
    while (name[n] && n < INPUT_MAX - 16) {
        out[n] = name[n];
        ++n;
    }
    out[n++] = ':';
    out[n++] = ' ';
    value = size;
    do {
        digits[digits_count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value && digits_count < sizeof(digits));
    while (digits_count) out[n++] = digits[--digits_count];
    out[n++] = ' ';
    out[n++] = 'b';
    out[n++] = 'y';
    out[n++] = 't';
    out[n++] = 'e';
    out[n++] = 's';
    out[n] = 0;
    log_line(out);
}

static unsigned int number(const char *s) {
    unsigned int n = 0;
    if (!*s) return 0xFFFFFFFFu;
    while (*s) {
        if (*s < '0' || *s > '9') return 0xFFFFFFFFu;
        if (n > (0xFFFFFFFFu - (*s - '0')) / 10u) return 0xFFFFFFFFu;
        n = n * 10u + (*s++ - '0');
    }
    return n;
}

static void command(void) {
    char work[INPUT_MAX + 1], *arg[4];
    unsigned int argc = 0, i = 0, j;
    memcpy(work, input, input_len + 1);
    while (work[i] && argc < 4) {
        while (work[i] == ' ' || work[i] == '\t') ++i;
        if (!work[i]) break;
        arg[argc++] = &work[i];
        while (work[i] && work[i] != ' ' && work[i] != '\t') ++i;
        if (work[i]) work[i++] = 0;
    }
    if (!argc) return;
    if (same(arg[0], "help")) {
        log_line("help clear echo uptime beep ls pwd exit");
        log_line("cat NAME size NAME rm NAME mv OLD NEW touch NAME");
    } else if (same(arg[0], "clear")) {
        log_count = 0;
    } else if (same(arg[0], "echo")) {
        char out[INPUT_MAX + 1];
        unsigned int n = 0;
        for (i = 1; i < argc && n < INPUT_MAX; ++i) {
            if (i > 1) out[n++] = ' ';
            for (j = 0; arg[i][j] && n < INPUT_MAX; ++j) out[n++] = arg[i][j];
        }
        out[n] = 0; log_line(out);
    } else if (same(arg[0], "uptime")) {
        char out[INPUT_MAX + 1], digits[12];
        unsigned int t = ax_call(SYS_TICKS, 0, 0, 0), n = 0, d = 0;
        const char prefix[] = "Ticks since boot: ";
        while (prefix[n]) { out[n] = prefix[n]; ++n; }
        do { digits[d++] = (char)('0' + t % 10u); t /= 10u; } while (t && d < 10);
        while (d) out[n++] = digits[--d];
        out[n] = 0; log_line(out);
    } else if (same(arg[0], "beep")) {
        unsigned int hz = argc > 1 ? number(arg[1]) : 523;
        unsigned int ms = argc > 2 ? number(arg[2]) : 150;
        if (hz < 20 || hz > 20000 || ms == 0 || ms > 5000)
            log_line("Usage: beep HZ MS");
        else { ax_call(SYS_TONE, hz, ms, 0); log_line("Tone played."); }
    } else if (same(arg[0], "ls")) {
        char name[FS_NAME_MAX + 1];
        for (i = 0; i < FS_FILES; ++i)
            if (fs_list(i, name) != FS_FAIL) log_line(name);
    } else if (same(arg[0], "pwd")) {
        log_line("/");
    } else if (same(arg[0], "cat") && argc > 1) {
        cat_file(arg[1]);
    } else if (same(arg[0], "size") && argc > 1) {
        log_file_size(arg[1]);
    } else if (same(arg[0], "rm") && argc > 1) {
        log_line(fs_delete(arg[1]) == FS_FAIL ? "rm: failed" : "File deleted.");
    } else if (same(arg[0], "mv") && argc > 2) {
        log_line(fs_rename(arg[1], arg[2]) == FS_FAIL
                 ? "mv: rename failed" : "File renamed.");
    } else if (same(arg[0], "touch") && argc > 1) {
        unsigned int h = fs_open(arg[1], FS_WRITE);
        if (h == FS_FAIL) log_line("touch: failed");
        else { fs_close(h); log_line("File created."); }
    } else if (same(arg[0], "exit")) {
        log_line("Closing Terminal.");
        exit_requested = 1;
    } else {
        log_line("Unknown command. Type help.");
    }
}

static void on_char(int c) {
    unsigned int i;
    if (c == '\n') {
        input[input_len] = 0;
        save_history();
        log_line(input);
        command();
        input_len = cursor = 0;
        input[0] = 0;
        history_pos = -1;
    } else if (c == '\b' && cursor) {
        for (i = cursor - 1; i + 1 < input_len; ++i) input[i] = input[i + 1];
        --cursor; --input_len; input[input_len] = 0;
    } else if (c >= 32 && c < 127 && input_len < INPUT_MAX) {
        for (i = input_len; i > cursor; --i) input[i] = input[i - 1];
        input[cursor++] = (char)c;
        input[++input_len] = 0;
        history_pos = -1;
    }
}

void app_main(void);

static void app_entry(void) {
    app_main();
    ax_call(SYS_EXIT, 0, 0, 0);
    for (;;) ;
}
void app_main(void) {
    int key, arrow;
    win = ax_open(12, 3, TERM_COLS, TERM_ROWS, "Terminal");
    if (win == AX_FAIL) return;
    exit_requested = 0;
    log_line("FlairOS Terminal");
    log_line("Type help to see available commands.");
    while (ax_alive(win) && !exit_requested) {
        draw();
        key = (int)ax_call(SYS_GETKEY, 0, 0, 0);
        if (key > 0) on_char(key);
        arrow = (int)ax_call(SYS_GETARROW, 0, 0, 0);
        if (arrow == ARROW_LEFT && cursor) --cursor;
        else if (arrow == ARROW_RIGHT && cursor < input_len) ++cursor;
        else if (arrow == ARROW_UP) load_history(1);
        else if (arrow == ARROW_DOWN) load_history(-1);
        ax_call(SYS_SLEEP, 1, 0, 0);
    }
    ax_close(win);
}
__attribute__((section(".head"), used))
const unsigned int app_head[2] = {
    TX_APP_MAGIC, (unsigned int)(unsigned long)app_entry
};
