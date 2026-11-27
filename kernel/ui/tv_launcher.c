/* SPDX-License-Identifier: AGPL-3.0-only */
/* TV launcher layout loader.
 *
 * FlairTV.airp is a component package: a validated container with typed
 * resources and no executable. Its launcher.json describes the ten-foot home
 * screen, so retiling the TV session is a packaging change rather than a kernel
 * rebuild. Every value is bounded before use and a rejected package never
 * half-applies: the caller keeps the built-in layout.
 */
#include "tv_launcher.h"
#include "../../include/airjson.h"
#include "../../include/airp.h"

#define TV_TILE_DEFAULT_ACCENTS 5
static const uint32_t tv_tile_accents[TV_TILE_DEFAULT_ACCENTS] = {
    0x00EC4899u, 0x00F97316u, 0x003B82F6u, 0x0010B981u, 0x00A855F7u
};

static void tv_text_copy(char *dst, int cap, const char *src) {
    int i = 0;
    if (!dst || cap <= 0) return;
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

/* Reads a string member of the JSON object spanning [obj, obj+obj_len). */
static int tv_json_string(const struct airjson_doc *doc, const char *obj,
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

static int tv_str_eq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int tv_strings_set(struct tv_strings *s, const char *key, const char *text) {
    for (int i = 0; i < s->count; i++) {
        if (tv_str_eq(s->items[i].key, key)) {
            tv_text_copy(s->items[i].text, (int)sizeof(s->items[i].text), text);
            return 1;
        }
    }
    if (s->count >= TV_STRINGS_MAX) return 0;
    tv_text_copy(s->items[s->count].key, (int)sizeof(s->items[s->count].key), key);
    tv_text_copy(s->items[s->count].text, (int)sizeof(s->items[s->count].text), text);
    s->count++;
    return 1;
}

void tv_strings_default(struct tv_strings *out) {
    static const char *keys[] = { "app", "home", "sections", "focus", "status_card", "focused", "open" };
    static const char *texts[] = {
        "FlairTV", "HOME", "Apps   Android   AirP   Status", "10-FOOT FOCUS",
        "AIRP + BT AUDIO", "FOCUSED", "OPEN"
    };
    if (!out) return;
    out->count = 0;
    for (int i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])); i++)
        (void)tv_strings_set(out, keys[i], texts[i]);
}

const char *tv_strings_get(const struct tv_strings *strings, const char *key) {
    if (!strings || !key) return 0;
    for (int i = 0; i < strings->count; i++)
        if (tv_str_eq(strings->items[i].key, key)) return strings->items[i].text;
    return 0;
}

/* Parses strings.json over the built-in wording, so a package only has to
   declare the keys it wants to change and an unknown key is still usable. */
static int tv_strings_parse(const char *json, unsigned int len, struct tv_strings *out) {
    struct airjson_doc doc;
    int count = 0;
    char key[TV_STRING_KEY_MAX];
    char text[TV_STRING_TEXT_MAX];

    if (airjson_open(&doc, json, len) != AIRJSON_OK) return 0;
    if (airjson_object_count(json, len, &count) != AIRJSON_OK || count <= 0) return 0;
    for (int i = 0; i < count; i++) {
        const char *k = 0, *v = 0;
        unsigned int klen = 0, vlen = 0;
        if (airjson_object_at(json, len, i, &k, &klen, &v, &vlen) != AIRJSON_OK) return 0;
        if (airjson_get_string(k, klen, key, sizeof(key)) != AIRJSON_OK) return 0;
        if (airjson_get_string(v, vlen, text, sizeof(text)) != AIRJSON_OK) return 0;
        if (!key[0] || !text[0]) return 0;
        if (!tv_strings_set(out, key, text)) return 0;
    }
    return 1;
}

void tv_launcher_default(struct tv_launcher *out) {
    if (!out) return;
    tv_text_copy(out->title, (int)sizeof(out->title), "FlairTV Living Room");
    tv_text_copy(out->hint, (int)sizeof(out->hint),
                 "UP: Control Drawer  |  LEFT/RIGHT: Focus  |  ENTER: Launch");
    out->tile_count = 3;
    {
        struct tv_launcher_tile tiles[3] = {
            { "hello", "Hello World", "Android APK", "app:0", 0x00EC4899u },
            { "calc", "DroidCalc", "Dalvik Math", "app:1", 0x00F97316u },
            { "cast", "AirP Cast", "Mirror Screen", "airp:cast", 0x003B82F6u }
        };
        for (int i = 0; i < 3; i++) out->tiles[i] = tiles[i];
    }
    tv_strings_default(&out->strings);
}

int tv_launcher_load_package(const uint8_t *stage, uint32_t stage_len,
                             struct tv_launcher *out, struct tv_launcher_report *report) {
    struct airp_package pkg;
    struct airp_view view;
    struct airjson_doc doc;
    struct tv_launcher candidate;
    const char *tv_value = 0;
    unsigned int tv_len = 0;
    const char *tiles_value = 0;
    unsigned int tiles_len = 0;
    unsigned int type = 0;
    int count = 0;
    int rc;

    if (!out || !report) return 0;
    report->loaded = 0;
    report->tiles = 0;
    report->strings = 0;
    report->reason = "no TV session package staged";

    if (!stage || stage_len == 0) return 0;

    rc = airp_open(stage, stage_len, AIRP_ARCH_ANY, &pkg);
    if (rc != AIRP_LOAD_OK) {
        report->reason = "TV package failed validation";
        return 0;
    }
    if (!(pkg.flags & AIRP_FLAG_RESOURCES_ONLY)) {
        report->reason = "TV package is not a component package";
        return 0;
    }
    if (airp_find_resource(&pkg, "launcher.json", &type, &view) != AIRP_LOAD_OK) {
        report->reason = "TV package has no launcher.json";
        return 0;
    }
    if (type != AIRP_RT_UI) {
        report->reason = "TV launcher.json is not a ui resource";
        return 0;
    }
    if (airjson_open(&doc, (const char *)view.data, view.size) != AIRJSON_OK) {
        report->reason = "TV launcher.json is not valid JSON";
        return 0;
    }
    if (airjson_root_get(&doc, "tv", &tv_value, &tv_len) != AIRJSON_OK) {
        report->reason = "TV launcher.json has no tv object";
        return 0;
    }

    tv_launcher_default(&candidate);
    if (tv_json_string(&doc, tv_value, tv_len, "title", candidate.title,
                       sizeof(candidate.title)) != 0) {
        report->reason = "TV launcher has no title";
        return 0;
    }
    if (tv_json_string(&doc, tv_value, tv_len, "hint", candidate.hint,
                       sizeof(candidate.hint)) != 0)
        candidate.hint[0] = 0; /* optional */

    if (airjson_object_get(&doc, tv_value, tv_len, "tiles", &tiles_value,
                           &tiles_len) != AIRJSON_OK) {
        report->reason = "TV launcher has no tiles";
        return 0;
    }
    if (airjson_array_count(tiles_value, tiles_len, &count) != AIRJSON_OK || count <= 0) {
        report->reason = "TV launcher tiles is not a non-empty array";
        return 0;
    }
    if (count > TV_LAUNCHER_MAX_TILES) {
        report->reason = "TV launcher has more tiles than the shell can show";
        return 0;
    }

    for (int i = 0; i < count; i++) {
        struct tv_launcher_tile *tile = &candidate.tiles[i];
        const char *tile_value = 0;
        unsigned int tile_len = 0;
        const char *accent_value = 0;
        unsigned int accent_len = 0;

        if (airjson_array_at(tiles_value, tiles_len, i, &tile_value, &tile_len) != AIRJSON_OK) {
            report->reason = "TV launcher tile is unreadable";
            return 0;
        }
        if (tv_json_string(&doc, tile_value, tile_len, "id", tile->id,
                           sizeof(tile->id)) != 0 ||
            tv_json_string(&doc, tile_value, tile_len, "label", tile->label,
                           sizeof(tile->label)) != 0 ||
            tv_json_string(&doc, tile_value, tile_len, "target", tile->target,
                           sizeof(tile->target)) != 0) {
            report->reason = "TV launcher tile is missing id, label or target";
            return 0;
        }
        if (tv_json_string(&doc, tile_value, tile_len, "subtitle", tile->subtitle,
                           sizeof(tile->subtitle)) != 0)
            tile->subtitle[0] = 0;
        tile->accent = tv_tile_accents[i % TV_TILE_DEFAULT_ACCENTS];
        if (airjson_object_get(&doc, tile_value, tile_len, "accent", &accent_value,
                               &accent_len) == AIRJSON_OK) {
            unsigned int colour = 0;
            if (airjson_get_color(accent_value, accent_len, &colour) == AIRJSON_OK)
                tile->accent = colour & 0x00FFFFFFu;
        }
    }

    if (airp_find_resource(&pkg, "strings.json", &type, &view) != AIRP_LOAD_OK) {
        report->reason = "TV package has no strings.json";
        return 0;
    }
    if (type != AIRP_RT_STRINGS) {
        report->reason = "TV strings.json is not a strings resource";
        return 0;
    }
    if (!tv_strings_parse((const char *)view.data, view.size, &candidate.strings)) {
        report->reason = "TV strings.json is invalid";
        return 0;
    }

    candidate.tile_count = count;
    *out = candidate;
    report->loaded = 1;
    report->tiles = count;
    report->strings = candidate.strings.count;
    report->reason = 0;
    return 1;
}
