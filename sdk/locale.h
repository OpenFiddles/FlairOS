#ifndef FLARE_C_LOCALE_H
#define FLARE_C_LOCALE_H
#define LC_ALL 0
#define LC_CTYPE 1
#define LC_NUMERIC 2
struct lconv { char *decimal_point; char *thousands_sep; };
static inline char *setlocale(int cat, const char *loc) { (void)cat; (void)loc; return "C"; }
#endif
