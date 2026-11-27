/* SPDX-License-Identifier: AGPL-3.0-only */
/* Session-shell launcher loader.
 *
 * FlairTV.airp and FlairMobile.airp are component packages: validated containers
 * with typed resources and no executable. Their launcher.json describes the home
 * screen and their strings.json every label, so retiling or rewording a session
 * is a packaging change rather than a kernel rebuild. Every value is bounded
 * before use and a rejected package never half-applies.
 */
#include "shell_launcher.h"
#include "../../include/airjson.h"
#include "../../include/airp.h"

#define SHELL_DEFAULT_ACCENTS 6
static const uint32_t shell_tile_accents[SHELL_DEFAULT_ACCENTS] = {
    0x00EC4899u, 0x00F97316u, 0x003B82F6u, 0x0010B981u, 0x00A855F7u, 0x0006B6D4u
};

static void shell_text_copy(char *dst, int cap, const char *src) {
    int i = 0;
    if (!dst || cap <= 0) return;
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static int shell_str_eq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

/* Reads a string member of the JSON object spanning [obj, obj+obj_len). */
static int shell_json_string(const struct airjson_doc *doc, const char *obj,
                             unsigned int obj_len, const char *key, char *out,
                             unsigned int cap) {
    const char *value = 0;
    unsigned int value_len = 0;
    if (airjson_object_get(doc, obj, obj_len, key, &value, &value_len) != AIRJSON_OK)
        return -1;
    if (airjson_get_string(value, value_len, out, cap) != AIRJSON_OK)
        return -1;
    return out[0] ? 0 : -1;
}

/* Built-in wording for the keys the TV shell draws without a package. Every
   other label falls back to a literal at its draw site. */
static const char *const shell_builtin_keys[] = {
    "app", "home", "sections", "focus", "status_card", "focused", "open"
};
static const char *const shell_builtin_text[] = {
    "FlairTV", "HOME", "Apps   Android   AirP   Status", "10-FOOT FOCUS",
    "AIRP + BT AUDIO", "FOCUSED", "OPEN"
};
#define SHELL_BUILTIN_COUNT ((int)(sizeof(shell_builtin_keys) / sizeof(shell_builtin_keys[0])))

const char *shell_launcher_root(enum shell_kind kind) {
    if (kind == SHELL_KIND_MOBILE) return "mobile";
    if (kind == SHELL_KIND_DESKTOP) return "desktop";
    return "tv";
}

const char *shell_string_or(const struct shell_strings *strings, const char *key,
                            const char *fallback) {
    static char text[SHELL_STRING_TEXT_MAX];
    if (strings && shell_strings_copy(strings, key, text, sizeof text)) return text;
    return fallback;
}

void shell_strings_default(struct shell_strings *out) {
    if (!out) return;
    out->json = 0;
    out->len = 0;
    out->count = 0;
    out->kind = SHELL_KIND_TV;
}

int shell_strings_copy(const struct shell_strings *strings, const char *key,
                       char *buf, unsigned int cap) {
    const char *value = 0;
    unsigned int value_len = 0;

    if (!buf || cap == 0) return 0;
    buf[0] = 0;
    if (!strings || !key) return 0;

    if (strings->json &&
        airjson_object_get(0, strings->json, strings->len, key, &value, &value_len) == AIRJSON_OK &&
        airjson_get_string(value, value_len, buf, cap) == AIRJSON_OK && buf[0])
        return 1;

    if (strings->kind != SHELL_KIND_TV) return 0;
    for (int i = 0; i < SHELL_BUILTIN_COUNT; i++) {
        if (shell_str_eq(shell_builtin_keys[i], key)) {
            shell_text_copy(buf, (int)cap, shell_builtin_text[i]);
            return 1;
        }
    }
    return 0;
}

/* Validates every entry of strings.json without copying it: the layout keeps a
   view of these bytes, so this pass is what makes an over-long or malformed
   entry reject the package before anything is applied. */
static int shell_strings_parse(const char *json, unsigned int len, struct shell_strings *out,
                               enum shell_kind kind) {
    struct airjson_doc doc;
    int count = 0;
    char key[SHELL_STRING_KEY_MAX];
    char text[SHELL_STRING_TEXT_MAX];

    if (airjson_open(&doc, json, len) != AIRJSON_OK) return 0;
    if (airjson_object_count(json, len, &count) != AIRJSON_OK || count <= 0) return 0;
    if (count > SHELL_STRINGS_MAX) return 0;
    for (int i = 0; i < count; i++) {
        const char *k = 0, *v = 0;
        unsigned int klen = 0, vlen = 0;
        if (airjson_object_at(json, len, i, &k, &klen, &v, &vlen) != AIRJSON_OK) return 0;
        if (airjson_get_string(k, klen, key, sizeof(key)) != AIRJSON_OK) return 0;
        if (airjson_get_string(v, vlen, text, sizeof(text)) != AIRJSON_OK) return 0;
        if (!key[0] || !text[0]) return 0;
    }
    out->json = json;
    out->len = len;
    out->count = count;
    out->kind = kind;
    return 1;
}

static void shell_set_dock(struct shell_launcher *out, const char *const *ids, int count) {
    out->dock_count = 0;
    for (int i = 0; i < count && i < SHELL_LAUNCHER_MAX_DOCK; i++) {
        shell_text_copy(out->dock_ids[i], (int)sizeof(out->dock_ids[i]), ids[i]);
        out->dock_count++;
    }
}

static void shell_launcher_default_tv(struct shell_launcher *out) {
    struct shell_tile tiles[3] = {
        { "hello", "Hello World", "Android APK", "app:0", "", -1, 0x00EC4899u },
        { "calc", "DroidCalc", "Dalvik Math", "app:1", "", -1, 0x00F97316u },
        { "cast", "AirP Cast", "Mirror Screen", "airp:cast", "", -1, 0x003B82F6u }
    };
    shell_text_copy(out->title, (int)sizeof(out->title), "FlairTV Living Room");
    shell_text_copy(out->hint, (int)sizeof(out->hint),
                    "UP: Control Drawer  |  LEFT/RIGHT: Focus  |  ENTER: Launch");
    out->tile_count = 3;
    for (int i = 0; i < 3; i++) out->tiles[i] = tiles[i];
    out->dock_count = 0;
}

static void shell_launcher_default_mobile(struct shell_launcher *out) {
    struct shell_tile tiles[6] = {
        { "browser", "Browser", "Web", "screen:browser", "BR", 5, 0x003B82F6u },
        { "files", "Files", "Storage", "screen:files", "FN", 1, 0x0010B981u },
        { "calc", "Calc", "Math", "screen:calc", "CA", 4, 0x00F97316u },
        { "notes", "Notes", "Text", "screen:notes", "NT", 3, 0x00EC4899u },
        { "activity", "Activity", "ART", "screen:activity", "AP", 6, 0x00A855F7u },
        { "settings", "Settings", "System", "screen:settings", "ST", 7, 0x0006B6D4u }
    };
    static const char *const dock[] = { "browser", "files", "activity", "settings" };
    shell_text_copy(out->title, (int)sizeof(out->title), "FlairOS Mobile");
    shell_text_copy(out->hint, (int)sizeof(out->hint),
                    "Tap an app  |  Top-right status  |  Bottom bar: Home");
    out->tile_count = 6;
    for (int i = 0; i < 6; i++) out->tiles[i] = tiles[i];
    shell_set_dock(out, dock, 4);
}

static void shell_launcher_default_desktop(struct shell_launcher *out) {
    static const char *const ids[] = { "launcher", "finder", "terminal", "notes",
                                       "calc", "browser", "apphost", "settings" };
    static const char *const labels[] = { "Launcher", "Finder", "Terminal", "Notes",
                                          "Calc", "Browser", "AppHost", "Settings" };
    static const char *const glyphs[] = { "LA", "FN", ">_", "NT", "CA", "BR", "AP", "ST" };
    static const int icons[] = { 7, 1, 2, 3, 4, 5, 6, 0 };
    shell_text_copy(out->title, (int)sizeof(out->title), "FlairOS Desktop");
    shell_text_copy(out->hint, (int)sizeof(out->hint),
                    "Click the menu bar or a dock icon  |  Spotlight: search apps");
    out->tile_count = 0;
    for (int i = 0; i < 8; i++) {
        struct shell_tile *tile = &out->tiles[i];
        shell_text_copy(tile->id, (int)sizeof(tile->id), ids[i]);
        shell_text_copy(tile->label, (int)sizeof(tile->label), labels[i]);
        tile->subtitle[0] = 0;
        shell_text_copy(tile->target, (int)sizeof(tile->target), "screen:");
        shell_text_copy(tile->target + 7, (int)sizeof(tile->target) - 7, ids[i]);
        shell_text_copy(tile->glyph, (int)sizeof(tile->glyph), glyphs[i]);
        tile->icon = icons[i];
        tile->accent = shell_tile_accents[i % SHELL_DEFAULT_ACCENTS];
        out->tile_count++;
    }
    shell_set_dock(out, ids, 8);
}

void shell_launcher_default(struct shell_launcher *out, enum shell_kind kind) {
    if (!out) return;
    if (kind == SHELL_KIND_MOBILE)
        shell_launcher_default_mobile(out);
    else if (kind == SHELL_KIND_DESKTOP)
        shell_launcher_default_desktop(out);
    else
        shell_launcher_default_tv(out);
    shell_strings_default(&out->strings);
    out->strings.kind = kind;
}

int shell_launcher_load_package(const uint8_t *stage, uint32_t stage_len,
                                enum shell_kind kind, struct shell_launcher *out,
                                struct shell_launcher_report *report) {
    struct airp_package pkg;
    struct airp_view view;
    struct airjson_doc doc;
    struct shell_launcher candidate;
    const char *root = shell_launcher_root(kind);
    const char *root_value = 0;
    unsigned int root_len = 0;
    const char *tiles_value = 0;
    unsigned int tiles_len = 0;
    const char *json_value = 0;
    unsigned int json_len = 0;
    unsigned int type = 0;
    int count = 0;

    if (!out || !report) return 0;
    report->loaded = 0;
    report->tiles = 0;
    report->dock = 0;
    report->strings = 0;
    report->reason = "no session package staged";

    if (!stage || stage_len == 0) return 0;

    if (airp_open(stage, stage_len, AIRP_ARCH_ANY, &pkg) != AIRP_LOAD_OK) {
        report->reason = "session package failed validation";
        return 0;
    }
    if (!(pkg.flags & AIRP_FLAG_RESOURCES_ONLY)) {
        report->reason = "session package is not a component package";
        return 0;
    }
    if (airp_find_resource(&pkg, "launcher.json", &type, &view) != AIRP_LOAD_OK) {
        report->reason = "session package has no launcher.json";
        return 0;
    }
    if (type != AIRP_RT_UI) {
        report->reason = "session launcher.json is not a ui resource";
        return 0;
    }
    if (airjson_open(&doc, (const char *)view.data, view.size) != AIRJSON_OK) {
        report->reason = "session launcher.json is not valid JSON";
        return 0;
    }
    if (airjson_root_get(&doc, root, &root_value, &root_len) != AIRJSON_OK) {
        report->reason = "session launcher.json has no root object";
        return 0;
    }

    shell_launcher_default(&candidate, kind);
    if (shell_json_string(&doc, root_value, root_len, "title", candidate.title,
                          sizeof(candidate.title)) != 0) {
        report->reason = "session launcher has no title";
        return 0;
    }
    if (shell_json_string(&doc, root_value, root_len, "hint", candidate.hint,
                          sizeof(candidate.hint)) != 0)
        candidate.hint[0] = 0; /* optional */

    if (airjson_object_get(&doc, root_value, root_len, "tiles", &tiles_value,
                           &tiles_len) != AIRJSON_OK) {
        report->reason = "session launcher has no tiles";
        return 0;
    }
    if (airjson_array_count(tiles_value, tiles_len, &count) != AIRJSON_OK || count <= 0) {
        report->reason = "session launcher tiles is not a non-empty array";
        return 0;
    }
    if (count > SHELL_LAUNCHER_MAX_TILES) {
        report->reason = "session launcher has more tiles than the shell can show";
        return 0;
    }

    for (int i = 0; i < count; i++) {
        struct shell_tile *tile = &candidate.tiles[i];
        const char *tile_value = 0;
        unsigned int tile_len = 0;

        if (airjson_array_at(tiles_value, tiles_len, i, &tile_value, &tile_len) != AIRJSON_OK) {
            report->reason = "session launcher tile is unreadable";
            return 0;
        }
        if (shell_json_string(&doc, tile_value, tile_len, "id", tile->id,
                              sizeof(tile->id)) != 0 ||
            shell_json_string(&doc, tile_value, tile_len, "label", tile->label,
                              sizeof(tile->label)) != 0 ||
            shell_json_string(&doc, tile_value, tile_len, "target", tile->target,
                              sizeof(tile->target)) != 0) {
            report->reason = "session launcher tile is missing id, label or target";
            return 0;
        }
        if (shell_json_string(&doc, tile_value, tile_len, "subtitle", tile->subtitle,
                              sizeof(tile->subtitle)) != 0)
            tile->subtitle[0] = 0;
        if (shell_json_string(&doc, tile_value, tile_len, "glyph", tile->glyph,
                              sizeof(tile->glyph)) != 0)
            tile->glyph[0] = 0;
        tile->icon = -1;
        if (airjson_object_get(&doc, tile_value, tile_len, "icon", &json_value,
                               &json_len) == AIRJSON_OK) {
            int icon = 0;
            if (airjson_get_int(json_value, json_len, &icon) != AIRJSON_OK || icon < 0) {
                report->reason = "session launcher tile has an invalid icon";
                return 0;
            }
            tile->icon = icon;
        }
        tile->accent = shell_tile_accents[i % SHELL_DEFAULT_ACCENTS];
        if (airjson_object_get(&doc, tile_value, tile_len, "accent", &json_value,
                               &json_len) == AIRJSON_OK) {
            unsigned int colour = 0;
            if (airjson_get_color(json_value, json_len, &colour) != AIRJSON_OK) {
                report->reason = "session launcher tile has an invalid accent";
                return 0;
            }
            tile->accent = colour & 0x00FFFFFFu;
        }
    }

    /* Optional dock: ids that must name a declared tile. */
    if (airjson_object_get(&doc, root_value, root_len, "dock", &json_value,
                           &json_len) == AIRJSON_OK) {
        int dock_count = 0;
        if (airjson_array_count(json_value, json_len, &dock_count) != AIRJSON_OK ||
            dock_count <= 0 || dock_count > SHELL_LAUNCHER_MAX_DOCK) {
            report->reason = "session launcher dock is not a usable array";
            return 0;
        }
        candidate.dock_count = 0;
        for (int d = 0; d < dock_count; d++) {
            const char *entry = 0;
            unsigned int entry_len = 0;
            char id[SHELL_LAUNCHER_ID_MAX];
            int found = 0;
            if (airjson_array_at(json_value, json_len, d, &entry, &entry_len) != AIRJSON_OK ||
                airjson_get_string(entry, entry_len, id, sizeof(id)) != AIRJSON_OK ||
                !id[0]) {
                report->reason = "session launcher dock entry is unreadable";
                return 0;
            }
            for (int t = 0; t < count; t++) {
                if (shell_str_eq(candidate.tiles[t].id, id)) { found = 1; break; }
            }
            if (!found) {
                report->reason = "session launcher dock names an unknown tile";
                return 0;
            }
            shell_text_copy(candidate.dock_ids[candidate.dock_count],
                            (int)sizeof(candidate.dock_ids[0]), id);
            candidate.dock_count++;
        }
    }

    if (airp_find_resource(&pkg, "strings.json", &type, &view) != AIRP_LOAD_OK) {
        report->reason = "session package has no strings.json";
        return 0;
    }
    if (type != AIRP_RT_STRINGS) {
        report->reason = "session strings.json is not a strings resource";
        return 0;
    }
    if (!shell_strings_parse((const char *)view.data, view.size, &candidate.strings, kind)) {
        report->reason = "session strings.json is invalid";
        return 0;
    }

    candidate.tile_count = count;
    *out = candidate;
    report->loaded = 1;
    report->tiles = count;
    report->dock = candidate.dock_count;
    report->strings = candidate.strings.count;
    report->reason = 0;
    return 1;
}
