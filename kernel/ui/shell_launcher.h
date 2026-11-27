#ifndef FLAIROS_SHELL_LAUNCHER_H
#define FLAIROS_SHELL_LAUNCHER_H

#include <stdint.h>

/* Session shells are packages.
 *
 * A shell's home screen is a resource, not kernel tables: `FlairTV.airp` for the
 * ten-foot session and `FlairMobile.airp` for the phone session each carry a
 * launcher.json (title, hint, tiles, dock) and a strings.json (every visible
 * label). The kernel owns the container reader and the validation; the shell
 * only draws what was accepted, and keeps its built-in layout when the package
 * is missing, malformed or out of bounds.
 *
 * Both packages are resources-only component packages: validated containers
 * with typed resources and no executable. */

#define SHELL_LAUNCHER_MAX_TILES 8
#define SHELL_LAUNCHER_MAX_DOCK 4
#define SHELL_LAUNCHER_ID_MAX 16
#define SHELL_LAUNCHER_LABEL_MAX 32
#define SHELL_LAUNCHER_TARGET_MAX 24
#define SHELL_LAUNCHER_TITLE_MAX 40
#define SHELL_LAUNCHER_HINT_MAX 96
#define SHELL_LAUNCHER_GLYPH_MAX 8

#define SHELL_STRINGS_MAX 96
#define SHELL_STRING_KEY_MAX 24
#define SHELL_STRING_TEXT_MAX 44

struct shell_tile {
    char id[SHELL_LAUNCHER_ID_MAX];
    char label[SHELL_LAUNCHER_LABEL_MAX];
    char subtitle[SHELL_LAUNCHER_LABEL_MAX];
    char target[SHELL_LAUNCHER_TARGET_MAX];
    char glyph[SHELL_LAUNCHER_GLYPH_MAX]; /* optional badge drawn on the icon */
    int icon;                             /* optional icon index, -1 when absent */
    uint32_t accent;                      /* 0x00RRGGBB */
};

enum shell_kind {
    SHELL_KIND_TV = 0,
    SHELL_KIND_MOBILE = 1
};

/* A layout carries a view of the package's strings.json, not a copy: those
   bytes already live in the image, and the built-in wording lives in the code
   that draws it. `kind` selects the built-in key table, so a phone session
   never inherits a ten-foot label. */
struct shell_strings {
    const char *json;
    unsigned int len;
    int count;
    enum shell_kind kind;
};

struct shell_launcher {
    char title[SHELL_LAUNCHER_TITLE_MAX];
    char hint[SHELL_LAUNCHER_HINT_MAX];
    int tile_count;
    struct shell_tile tiles[SHELL_LAUNCHER_MAX_TILES];
    int dock_count;
    char dock_ids[SHELL_LAUNCHER_MAX_DOCK][SHELL_LAUNCHER_ID_MAX];
    struct shell_strings strings;
};

struct shell_launcher_report {
    int loaded;      /* 1 when the package layout replaced the built-in one */
    int tiles;       /* tiles taken from the package */
    int dock;        /* dock entries taken from the package */
    int strings;     /* session strings in the resolved layout */
    const char *reason; /* why the built-in layout was kept, when not loaded */
};

/* Built-in home screen and wording for a session, used when no package is
   staged or the staged one is rejected. */
void shell_launcher_default(struct shell_launcher *out, enum shell_kind kind);
void shell_strings_default(struct shell_strings *out);

/* Copies the text for `key` into `buf` and returns 1 when the package or the
   built-in shell wording declares it. The package wins, so a partial package
   overrides only what it names. */
int shell_strings_copy(const struct shell_strings *strings, const char *key,
                       char *buf, unsigned int cap);

/* The JSON object a session's launcher.json keeps its layout under. */
const char *shell_launcher_root(enum shell_kind kind);

/* Reads launcher.json + strings.json out of a staged session package of the
   given kind. All-or-nothing: returns 1 and fills `out` only when every value is
   valid; otherwise returns 0 with `report->reason` set and `out` untouched. */
int shell_launcher_load_package(const uint8_t *stage, uint32_t stage_len,
                                enum shell_kind kind, struct shell_launcher *out,
                                struct shell_launcher_report *report);

#endif
