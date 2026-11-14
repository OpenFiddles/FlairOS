#include <stdarg.h>
#include <stddef.h>

/* Tiny bounded formatter for the bare-metal build. The readline port uses
   snprintf only to format a decimal cursor-back distance. */
static int flareos_vsnprintf(char *dst, size_t size, const char *format, va_list ap) {
    size_t total = 0;
    for (size_t i = 0; format[i]; ++i) {
        if (format[i] == '%' && format[i + 1] == 'u') {
            unsigned int value = va_arg(ap, unsigned int);
            char digits[10];
            size_t count = 0;
            do {
                digits[count++] = (char)('0' + value % 10u);
                value /= 10u;
            } while (value && count < sizeof(digits));
            while (count) {
                char c = digits[--count];
                if (size && total + 1u < size) dst[total] = c;
                ++total;
            }
            ++i;
        } else if (format[i] == '%' && format[i + 1] == '%') {
            if (size && total + 1u < size) dst[total] = '%';
            ++total;
            ++i;
        } else {
            if (size && total + 1u < size) dst[total] = format[i];
            ++total;
        }
    }
    if (size) dst[total < size ? total : size - 1u] = 0;
    return (int)total;
}

int __snprintf_chk(char *dst, size_t maxlen, int flag, size_t object_size,
                   const char *format, ...) {
    va_list ap;
    int result;
    (void)flag;
    if (object_size != (size_t)-1 && maxlen > object_size)
        maxlen = object_size;
    va_start(ap, format);
    result = flareos_vsnprintf(dst, maxlen, format, ap);
    va_end(ap);
    return result;
}
