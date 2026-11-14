#include "py/mpconfig.h"
#include "py/mphal.h"
#include "flareos_abi.h"

static int interrupt_char = -1;

void mp_hal_set_interrupt_char(int c) {
    interrupt_char = c;
}

static char arrow_sequence[2];
static unsigned int arrow_pos;
static unsigned int arrow_len;

int mp_hal_stdin_rx_chr(void) {
    for (;;) {
        if (arrow_pos < arrow_len)
            return arrow_sequence[arrow_pos++];
        arrow_pos = arrow_len = 0;

        int c = (int)flareos_call(SYS_GETKEY, 0, 0, 0);
        if (c >= 0) {
            if (interrupt_char >= 0 && c == interrupt_char) return interrupt_char;
            if (c == '\r') return '\n';
            if (c == 127) return '\b';
            return c;
        }

        /* Translate the kernel arrow queue into readline's ANSI key sequence. */
        int arrow = (int)flareos_call(SYS_GETARROW, 0, 0, 0);
        if (arrow >= 1 && arrow <= 4) {
            arrow_sequence[0] = '[';
            arrow_sequence[1] = arrow == 1 ? 'A' : arrow == 2 ? 'B' :
                                arrow == 3 ? 'D' : 'C';
            arrow_pos = 0;
            arrow_len = 2;
            return 0x1B;
        }
        flareos_call(SYS_SLEEP, 1, 0, 0);
    }
}

static unsigned int console_column;
static unsigned int ansi_number;
static int ansi_state;
static int ansi_has_number;

static void console_emit(char c) {
    flareos_call(SYS_PUTC, (u8)c, 0, 0);
    if (c == '\n' || c == '\r') console_column = 0;
    else if (c == '\b') {
        if (console_column) --console_column;
    } else if ((u8)c >= 32 && (u8)c != 127) {
        if (++console_column >= 80) console_column = 0;
    }
}

void mp_hal_reset_console(void) {
    console_column = 0;
    ansi_number = 0;
    ansi_state = 0;
    ansi_has_number = 0;
    arrow_pos = arrow_len = 0;
}

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)str[i];
        if (ansi_state == 0) {
            if (c == 0x1B) {
                ansi_state = 1;
                continue;
            }
            console_emit((char)c);
        } else if (ansi_state == 1) {
            if (c == '[') {
                ansi_state = 2;
                ansi_number = 0;
                ansi_has_number = 0;
            } else {
                ansi_state = 0;
                console_emit((char)c);
            }
        } else {
            if (c >= '0' && c <= '9') {
                ansi_has_number = 1;
                ansi_number = ansi_number * 10u + (unsigned int)(c - '0');
            } else {
                if (c == 'D') {
                    unsigned int count = ansi_has_number && ansi_number ? ansi_number : 1u;
                    while (count--) console_emit('\b');
                } else if (c == 'K') {
                    unsigned int old_column = console_column;
                    unsigned int count = old_column < 79u ? 79u - old_column : 0u;
                    for (unsigned int n = 0; n < count; ++n) console_emit(' ');
                    for (unsigned int n = 0; n < count; ++n) console_emit('\b');
                    console_column = old_column;
                }
                ansi_state = 0;
                ansi_number = 0;
                ansi_has_number = 0;
            }
        }
    }
    return (mp_uint_t)len;
}

void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len) {
    (void)mp_hal_stdout_tx_strn(str, len);
}

void mp_hal_stdout_tx_str(const char *str) {
    size_t len = 0;
    while (str[len]) ++len;
    (void)mp_hal_stdout_tx_strn(str, len);
}

void mp_hal_delay_ms(mp_uint_t ms) {
    if (ms == 0) return;
    flareos_call(SYS_SLEEP, (u32)(ms / 10u + (ms % 10u != 0u)), 0, 0);
}

void mp_hal_delay_us(mp_uint_t us) {
    /* PIT resolution is 10 ms; sub-tick delays are best-effort only. */
    if (us >= 10000u) {
        mp_hal_delay_ms(us / 1000u + (us % 1000u != 0u));
    } else {
        volatile u32 loops = us * 12u;
        while (loops--) __asm__ volatile ("pause");
    }
}

mp_uint_t mp_hal_ticks_ms(void) {
    return (mp_uint_t)(flareos_call(SYS_TICKS, 0, 0, 0) * 10u);
}

mp_uint_t mp_hal_ticks_us(void) {
    return (mp_uint_t)(flareos_call(SYS_TICKS, 0, 0, 0) * 10000u);
}

mp_uint_t mp_hal_ticks_cpu(void) {
    return mp_hal_ticks_us();
}
