#ifndef FLARE_C_TIME_H
#define FLARE_C_TIME_H
#include "flareos.h"

typedef long time_t;
struct timespec {
    long tv_sec;
    long tv_nsec;
};

static inline time_t time(time_t *t) {
    time_t now = (time_t)flare_time_unix();
    if (t) *t = now;
    return now;
}

#endif
