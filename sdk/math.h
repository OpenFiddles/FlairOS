#ifndef FLARE_C_MATH_H
#define FLARE_C_MATH_H

#define NAN (__builtin_nanf(""))
#define INFINITY (__builtin_inff())
#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)

static inline double fabs(double x) { return x < 0.0 ? -x : x; }
static inline float fabsf(float x) { return x < 0.0f ? -x : x; }
static inline double floor(double x) {
    long long i = (long long)x;
    return (x < (double)i) ? (double)(i - 1) : (double)i;
}
static inline double ceil(double x) {
    long long i = (long long)x;
    return (x > (double)i) ? (double)(i + 1) : (double)i;
}
static inline double sqrt(double x) {
    if (x <= 0.0) return 0.0;
    double g = x;
    for (int i = 0; i < 16; i++) g = 0.5 * (g + x / g);
    return g;
}

#endif
