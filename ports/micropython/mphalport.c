#include "py/mpconfig.h"
#include "py/mphal.h"
#include "flareos_abi.h"

static u32 flareos_call(u32 number, u32 b, u32 c, u32 d) {
    u32 result;
    __asm__ volatile ("int $0x80"
        : "=a"(result)
        : "a"(number), "b"(b), "c"(c), "d"(d)
        : "memory");
    return result;
}

int mp_hal_stdin_rx_chr(void) {
    for (;;) {
        int c = (int)flareos_call(SYS_GETKEY, 0, 0, 0);
        if (c >= 0) {
            if (c == '\r') return '\n';
            if (c == 127) return '\b';
            return c;
        }
        flareos_call(SYS_SLEEP, 1, 0, 0);
    }
}

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    for (size_t i = 0; i < len; ++i)
        flareos_call(SYS_PUTC, (u8)str[i], 0, 0);
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
    flareos_call(SYS_SLEEP, (u32)((ms + 9u) / 10u), 0, 0);
}

void mp_hal_delay_us(mp_uint_t us) {
    /* PIT resolution is 10 ms; sub-tick delays are best-effort only. */
    if (us >= 10000u) {
        mp_hal_delay_ms((us + 999u) / 1000u);
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
