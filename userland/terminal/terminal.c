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
static unsigned int scroll_offset;

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
    /* New output follows the live end unless the user scrolls again. */
    scroll_offset = 0;
}

static void draw(void) {
    unsigned int rows = TERM_ROWS - 3;
    unsigned int max_offset = log_count > rows ? log_count - rows : 0;
    unsigned int first, thumb_size, thumb_top, i;
    char line[TERM_COLS + 1];
    char track[2] = { (char)0xB0, 0 };
    char thumb[2] = { (char)0xDB, 0 };
    if (scroll_offset > max_offset) scroll_offset = max_offset;
    first = log_count > rows + scroll_offset
          ? log_count - rows - scroll_offset : 0;
    ax_clear(win, ATTR_BODY);
    ax_text(win, 0, 0, ATTR_HEAD, " FlareOS Terminal | Userland");
    for (i = 0; i < rows; ++i)
        ax_text(win, i + 1, TERM_COLS - 1, 0x78, track);
    for (i = 0; i < rows && first + i < log_count; ++i)
        ax_text(win, i + 1, 0, ATTR_BODY, logbuf[first + i]);
    thumb_size = log_count > rows ? (rows * rows) / log_count : rows;
    if (!thumb_size) thumb_size = 1;
    thumb_top = max_offset
              ? (rows - thumb_size) -
                (scroll_offset * (rows - thumb_size)) / max_offset
              : rows - thumb_size;
    for (i = 0; i < thumb_size; ++i)
        ax_text(win, thumb_top + i + 1, TERM_COLS - 1, 0x1F, thumb);
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
    ax_text(win, TERM_ROWS - 2, 0, ATTR_INPUT, line);
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

static void log_uint(const char *prefix, unsigned int v) {
    char out[INPUT_MAX + 1], digits[12];
    unsigned int n = 0, d = 0;
    while (prefix[n] && n < INPUT_MAX - 12) { out[n] = prefix[n]; ++n; }
    do { digits[d++] = (char)('0' + v % 10u); v /= 10u; } while (v && d < 10);
    while (d) out[n++] = digits[--d];
    out[n] = 0;
    log_line(out);
}

static int parse_ipv4(const char *s, unsigned char out[4]) {
    unsigned int val = 0;
    int part = 0, has_digit = 0;
    for (unsigned int i = 0; s[i]; ++i) {
        if (s[i] >= '0' && s[i] <= '9') {
            val = val * 10u + (unsigned int)(s[i] - '0');
            if (val > 255u) return -1;
            has_digit = 1;
        } else if (s[i] == '.' && has_digit && part < 3) {
            out[part++] = (unsigned char)val;
            val = 0; has_digit = 0;
        } else return -1;
    }
    if (!has_digit || part != 3) return -1;
    out[3] = (unsigned char)val;
    return 0;
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
        log_line("help clear echo uname time uptime pid rand beep exit");
        log_line("ls pwd cd mkdir cat write size rm mv touch install");
        log_line("net | ping A.B.C.D | Ctrl+Up/Down: scroll");
    } else if (same(arg[0], "clear")) {
        log_count = 0;
        scroll_offset = 0;
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
        char cwd[INPUT_MAX + 1];
        if (ax_call(SYS_VFS_GETCWD, (unsigned int)(unsigned long)cwd, sizeof(cwd), 0) == 0)
            log_line(cwd);
        else
            log_line("/");
    } else if (same(arg[0], "cd") && argc > 1) {
        log_line((int)ax_call(SYS_VFS_CHDIR, (unsigned int)(unsigned long)arg[1], 0, 0) == 0
                 ? "Changed directory." : "cd: not found");
    } else if (same(arg[0], "mkdir") && argc > 1) {
        log_line((int)ax_call(SYS_VFS_MKDIR, (unsigned int)(unsigned long)arg[1], 0, 0) == 0
                 ? "Directory created." : "mkdir: failed");
    } else if (same(arg[0], "uname")) {
        log_line("FlairOS V2 i386 (AirGraph / WinDown)");
    } else if (same(arg[0], "time")) {
        struct flare_time ft;
        char out[32];
        ax_call(SYS_TIME, (unsigned int)(unsigned long)&ft, 0, 0);
        out[0]=(char)('0'+(ft.year/1000)%10); out[1]=(char)('0'+(ft.year/100)%10);
        out[2]=(char)('0'+(ft.year/10)%10);   out[3]=(char)('0'+ft.year%10); out[4]='-';
        out[5]=(char)('0'+ft.month/10); out[6]=(char)('0'+ft.month%10); out[7]='-';
        out[8]=(char)('0'+ft.day/10);   out[9]=(char)('0'+ft.day%10); out[10]=' ';
        out[11]=(char)('0'+ft.hour/10); out[12]=(char)('0'+ft.hour%10); out[13]=':';
        out[14]=(char)('0'+ft.min/10);  out[15]=(char)('0'+ft.min%10); out[16]=':';
        out[17]=(char)('0'+ft.sec/10);  out[18]=(char)('0'+ft.sec%10);
        out[19]=' '; out[20]='U'; out[21]='T'; out[22]='C'; out[23]=0;
        log_line(out);
    } else if (same(arg[0], "pid")) {
        log_uint("Current PID: ", ax_call(SYS_GETPID, 0, 0, 0));
        log_uint("Active tasks: ", ax_call(SYS_TASK_COUNT, 0, 0, 0));
    } else if (same(arg[0], "rand")) {
        unsigned char rbuf[8];
        static const char hex[] = "0123456789abcdef";
        char out[24];
        ax_call(SYS_GETRANDOM, (unsigned int)(unsigned long)rbuf, 8, 0);
        for (i = 0; i < 8; ++i) {
            out[i * 2] = hex[(rbuf[i] >> 4) & 0xF];
            out[i * 2 + 1] = hex[rbuf[i] & 0xF];
        }
        out[16] = 0;
        log_line(out);
    } else if (same(arg[0], "net")) {
        int st = (int)ax_call(SYS_NET_INFO, 0, 0, 0);
        log_line(st == 1 ? "NIC: RTL8139 link UP" :
                 st == 0 ? "NIC: link DOWN" : "NIC: no network device");
    } else if (same(arg[0], "ping") && argc > 1) {
        unsigned char ip[4];
        if (parse_ipv4(arg[1], ip) != 0) log_line("Usage: ping A.B.C.D");
        else {
            int rc = (int)ax_call(SYS_NET_PING, (unsigned int)(unsigned long)ip, 0, 0);
            log_line(rc == 0 ? "ICMP echo reply received." : "ping: timeout or no NIC");
        }
    } else if (same(arg[0], "write") && argc > 2) {
        unsigned int h = fs_open(arg[1], FS_WRITE);
        if (h == FS_FAIL) log_line("write: failed");
        else {
            unsigned int n = 0;
            while (arg[2][n]) ++n;
            fs_write(h, arg[2], n);
            fs_close(h);
            log_line("Written.");
        }
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
    } else if (same(arg[0], "install") && argc > 2) {
        unsigned int sh = fs_open(arg[1], FS_READ);
        if (sh == FS_FAIL) log_line("install: source not found");
        else {
            char dst[INPUT_MAX + 1];
            const char *pfx = "/bin/";
            unsigned int di = 0, si = 0;
            while (pfx[di]) { dst[di] = pfx[di]; ++di; }
            while (arg[2][si] && di < INPUT_MAX) { dst[di++] = arg[2][si++]; }
            dst[di] = 0;
            int dfd = (int)ax_call(50u /* fallback */, 0, 0, 0);
            (void)dfd;
            dfd = (int)ax_call(47u, (unsigned int)(unsigned long)dst, 0x41u, 0);
            if (dfd < 0) log_line("install: VFS open failed");
            else {
                char buf[256];
                unsigned int n;
                while ((n = fs_read(sh, buf, sizeof(buf))) > 0 && n != FS_FAIL)
                    ax_call(49u, (unsigned int)dfd, (unsigned int)(unsigned long)buf, n);
                ax_call(50u, (unsigned int)dfd, 0, 0);
                log_line("Installed to /bin.");
            }
            fs_close(sh);
        }
    } else if (same(arg[0], "apk") && argc > 1) {
        int res = ax_apk_launch(arg[1]);
        log_line(res >= 0 ? "Launched Android APK window." : "apk: launch failed.");
    } else if (same(arg[0], "install") && argc > 2 && arg[1][0] == '/') {
        int res = ax_apk_install(arg[1], arg[2]);
        log_line(res == 0 ? "Installed APK to /apps." : "install: APK install failed.");
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
        if (arrow == ARROW_PAGE_UP) {
            unsigned int page = TERM_ROWS - 3;
            unsigned int max_offset = log_count > page ? log_count - page : 0;
            scroll_offset = scroll_offset + page > max_offset ? max_offset : scroll_offset + page;
        } else if (arrow == ARROW_PAGE_DOWN) {
            unsigned int page = TERM_ROWS - 3;
            scroll_offset = scroll_offset > page ? scroll_offset - page : 0;
        } else if (arrow == ARROW_LEFT && cursor) --cursor;
        else if (arrow == ARROW_RIGHT && cursor < input_len) ++cursor;
        else if (arrow == ARROW_UP) load_history(1);
        else if (arrow == ARROW_DOWN) load_history(-1);
        if (ax_call(SYS_TOGGLE_PENDING, 0, 0, 0)) exit_requested = 1;
        ax_call(SYS_SLEEP, 1, 0, 0);
    }
    ax_close(win);
}
__attribute__((section(".head"), used))
const unsigned int app_head[2] = {
    TX_APP_MAGIC, (unsigned int)(unsigned long)app_entry
};
