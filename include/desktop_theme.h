#ifndef FLAIROS_DESKTOP_THEME_H
#define FLAIROS_DESKTOP_THEME_H

#include <stdint.h>

struct sp_theme;

/* Result of loading the desktop package's theme resource. Reported to the
   caller so a rejected or unreadable theme is visible instead of silently
   falling back to a different palette. */
struct theme_report {
    int loaded;          /* 1 when the package theme was applied */
    int contrast_ok;     /* 1 when text/surface clears the WCAG AA floor */
    uint32_t contrast_x100; /* measured contrast ratio, times 100 */
    int fields_applied;  /* number of individual values the package supplied */
    const char *reason;  /* why the built-in palette was kept, when not loaded */
};

/* Loads FlairDesktop.airp from `stage`, validates the container, and applies its
   theme.json resource to `theme` (which must already hold the fallback palette).
   Returns 1 when the package theme was applied. */
int desktop_theme_load_package(const unsigned char *stage, unsigned int stage_len,
                               struct sp_theme *theme, struct theme_report *report);

/* Writes the theme decision to the serial log. */
void desktop_theme_log(const struct theme_report *report);

/* WCAG relative-luminance contrast ratio (x100) between two XRGB8888 colours. */
unsigned int theme_contrast_x100(uint32_t a, uint32_t b);

#endif
