#ifndef FLAIROS_TV_LAUNCHER_H
#define FLAIROS_TV_LAUNCHER_H

#include <stdint.h>

/* The TV session's launcher layout is a resource, not code: FlairTV.airp is a
   resources-only component package carrying launcher.json, and this module is
   the only place that turns it into the tiles the shell draws. A package that
   is missing, malformed or out of bounds leaves the built-in 10-foot layout in
   place, so the shell always has something to draw.

   Target vocabulary (declarative, resolved by the shell):
     "app:N"     launch ART package index N
     "airp:cast" start the AirP screen stream                              */

#define TV_LAUNCHER_MAX_TILES 8
#define TV_LAUNCHER_ID_MAX 16
#define TV_LAUNCHER_LABEL_MAX 32
#define TV_LAUNCHER_TARGET_MAX 24
#define TV_LAUNCHER_TITLE_MAX 40
#define TV_LAUNCHER_HINT_MAX 96

/* Session strings. The shell names a fixed key set, so a package can restyle
   the wording without the kernel knowing what the wording is. */
#define TV_STRINGS_MAX 12
#define TV_STRING_KEY_MAX 20
#define TV_STRING_TEXT_MAX 48

struct tv_string {
    char key[TV_STRING_KEY_MAX];
    char text[TV_STRING_TEXT_MAX];
};

struct tv_strings {
    int count;
    struct tv_string items[TV_STRINGS_MAX];
};

struct tv_launcher_tile {
    char id[TV_LAUNCHER_ID_MAX];
    char label[TV_LAUNCHER_LABEL_MAX];
    char subtitle[TV_LAUNCHER_LABEL_MAX];
    char target[TV_LAUNCHER_TARGET_MAX];
    uint32_t accent; /* 0x00RRGGBB */
};

struct tv_launcher {
    char title[TV_LAUNCHER_TITLE_MAX];
    char hint[TV_LAUNCHER_HINT_MAX];
    int tile_count;
    struct tv_launcher_tile tiles[TV_LAUNCHER_MAX_TILES];
    struct tv_strings strings;
};

struct tv_launcher_report {
    int loaded;     /* 1 when the package layout replaced the built-in one */
    int tiles;      /* tiles taken from the package */
    int strings;    /* session strings in the resolved layout */
    const char *reason; /* why the built-in layout was kept, when not loaded */
};

/* Built-in 10-foot layout and strings, used when no package is staged. */
void tv_launcher_default(struct tv_launcher *out);
void tv_strings_default(struct tv_strings *out);

/* Resolved text for `key`, or NULL when the layout has no such string. */
const char *tv_strings_get(const struct tv_strings *strings, const char *key);

/* Reads launcher.json out of a staged FlairTV.airp. Returns 1 and fills `out`
   when the package is valid and declares at least one usable tile; otherwise
   returns 0 with `report->reason` set and `out` untouched. */
int tv_launcher_load_package(const uint8_t *stage, uint32_t stage_len,
                             struct tv_launcher *out, struct tv_launcher_report *report);

#endif
