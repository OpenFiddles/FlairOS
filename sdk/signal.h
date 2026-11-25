#ifndef FLARE_C_SIGNAL_H
#define FLARE_C_SIGNAL_H
typedef int sig_atomic_t;
typedef void (*sighandler_t)(int);
#define SIGINT 2
#define SIGTERM 15
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
static inline sighandler_t signal(int sig, sighandler_t fn) { (void)sig; (void)fn; return SIG_DFL; }
static inline int raise(int sig) { (void)sig; return 0; }
#endif
