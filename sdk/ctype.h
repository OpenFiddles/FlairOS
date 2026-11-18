#ifndef FLARE_C_CTYPE_H
#define FLARE_C_CTYPE_H
#include "flare_stdlib.h"
#define FLARE_EOF (-1)
static inline int isalnum(int c) { return c == FLARE_EOF ? 0 : flare_isalnum((unsigned char)c); }
static inline int isalpha(int c) { return c == FLARE_EOF ? 0 : flare_isalpha((unsigned char)c); }
static inline int isdigit(int c) { return c == FLARE_EOF ? 0 : flare_isdigit((unsigned char)c); }
static inline int islower(int c) { return c == FLARE_EOF ? 0 : flare_islower((unsigned char)c); }
static inline int isupper(int c) { return c == FLARE_EOF ? 0 : flare_isupper((unsigned char)c); }
static inline int isspace(int c) { return c == FLARE_EOF ? 0 : flare_isspace((unsigned char)c); }
static inline int isxdigit(int c) { return c == FLARE_EOF ? 0 : flare_isxdigit((unsigned char)c); }
static inline int isprint(int c) { return c == FLARE_EOF ? 0 : flare_isprint((unsigned char)c); }
static inline int tolower(int c) { return c == FLARE_EOF ? c : flare_tolower((unsigned char)c); }
static inline int toupper(int c) { return c == FLARE_EOF ? c : flare_toupper((unsigned char)c); }
#endif
