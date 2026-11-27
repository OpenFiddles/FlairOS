#ifndef AIRJSON_H
#define AIRJSON_H

/* AirJSON: a small, strict JSON reader for FlairOS configuration resources.
 *
 * It supports the subset FlairOS packages use: objects, arrays, strings,
 * integers, booleans and null. It allocates nothing and never modifies the
 * input, so it can read straight out of a package resource in a read-only
 * staging buffer.
 *
 * Values are returned by reference:
 *   - strings: the decoded bytes are copied into a caller buffer, because JSON
 *     escapes must be resolved.
 *   - integers/booleans: returned as an int.
 *   - objects/arrays: reported by their byte span so the caller can iterate.
 *
 * Any malformed input, trailing garbage, or excessive nesting is rejected.
 */

#define AIRJSON_OK            0
#define AIRJSON_ERR_SYNTAX  (-1)
#define AIRJSON_ERR_RANGE   (-2)
#define AIRJSON_ERR_TYPE    (-3)
#define AIRJSON_ERR_DEPTH   (-4)
#define AIRJSON_ERR_STRING  (-5)
#define AIRJSON_ERR_NOTFOUND (-6)
#define AIRJSON_MAX_DEPTH    16

struct airjson_doc {
    const char *text;
    unsigned int len;
};

/* Validates the whole document and leaves it ready for lookups. */
int airjson_open(struct airjson_doc *doc, const char *text, unsigned int len);

/* Looks up a top-level key in the root object. Returns AIRJSON_OK when found.
   `value` and `value_len` describe the raw span of the value, which can be fed
   to the typed helpers below or to airjson_object_get for nesting. */
int airjson_object_get(const struct airjson_doc *doc, const char *object,
                       unsigned int object_len, const char *key,
                       const char **value, unsigned int *value_len);
int airjson_root_get(const struct airjson_doc *doc, const char *key,
                     const char **value, unsigned int *value_len);

/* Array iteration. `array`/`len` is the raw span of a JSON array value as
   returned by airjson_root_get or airjson_object_get. Counts and reads are
   re-validated against that span, so a caller never indexes past it. */
int airjson_array_count(const char *array, unsigned int len, int *out);
int airjson_array_at(const char *array, unsigned int len, int index,
                     const char **value, unsigned int *value_len);

/* Typed reads. airjson_get_string resolves escapes and NUL-terminates. */
int airjson_get_int(const char *value, unsigned int len, int *out);
int airjson_get_string(const char *value, unsigned int len, char *out, unsigned int cap);
int airjson_get_bool(const char *value, unsigned int len, int *out);

/* Hex colour helper for themes: accepts "RRGGBB" or "#RRGGBB" in a JSON string.
   Returns 0x00RRGGBB. */
int airjson_get_color(const char *value, unsigned int len, unsigned int *out);

#endif
