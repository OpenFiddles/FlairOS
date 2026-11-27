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

/* Session strings. The shell and the shared drawer name a fixed key set, so a
   package restyles the wording without the kernel knowing the words.

   A layout carries a *view* of the package's strings.json rather than a copy of
   every label: those bytes already live in the image, and copying ~70 labels
   would cost kilobytes of kernel RAM for no gain. The built-in wording lives in
   the code that draws it, so an undeclared key keeps its default. */
#define TV_STRINGS_MAX 96
#define TV_STRING_KEY_MAX 24
#define TV_STRING_TEXT_MAX 44

struct tv_strings {
    const char *json; /* staged strings.json, or NULL for built-in wording only */
    unsigned int len;
    int count;        /* validated entries in the package */
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

/* Copies the text for `key` into `buf` and returns 1 when the layout or the
   built-in wording declares it; 0 means neither does. The package wins over the
   built-in table, so partial packages only override what they name. */
int tv_strings_copy(const struct tv_strings *strings, const char *key,
                    char *buf, unsigned int cap);

/* Reads launcher.json out of a staged FlairTV.airp. Returns 1 and fills `out`
   when the package is valid and declares at least one usable tile; otherwise
   returns 0 with `report->reason` set and `out` untouched. */
int tv_launcher_load_package(const uint8_t *stage, uint32_t stage_len,
                             struct tv_launcher *out, struct tv_launcher_report *report);

#endif
