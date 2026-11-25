#ifndef FLARE_C_SETJMP_H
#define FLARE_C_SETJMP_H

typedef unsigned long jmp_buf[24];

#if defined(__i386__) || defined(__x86_64__)
#define setjmp(env) __builtin_setjmp((void **)(env))
#define longjmp(env, val) __builtin_longjmp((void **)(env), (val) ? (val) : 1)
#else
static inline int setjmp(jmp_buf env) { (void)env; return 0; }
static inline void longjmp(jmp_buf env, int val) { (void)env; (void)val; for (;;) {} }
#endif

#endif
