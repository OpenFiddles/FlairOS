#include <stdio.h>
#include <string.h>

#include "airjson.h"

static int failures;

static void check(int cond, const char *what) {
    printf("%s %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond)
        failures++;
}

#define OPEN(v) airjson_open(&doc, (v), (unsigned int)strlen(v))

static const char *THEME =
    "{\n"
    "  \"name\": \"FlairOS Default\",\n"
    "  \"variant\": \"dark\",\n"
    "  \"radius\": 12,\n"
    "  \"spacing\": 10,\n"
    "  \"border_width\": 1,\n"
    "  \"colors\": {\n"
    "    \"background\": \"141218\",\n"
    "    \"surface\": \"211F26\",\n"
    "    \"text\": \"E6E0E9\",\n"
    "    \"accent\": \"6750A4\"\n"
    "  },\n"
    "  \"animations\": true,\n"
    "  \"dock\": [\"terminal\", \"files\"]\n"
    "}";

static const char *LAUNCHER =
    "{\n"
    "  \"dock\": [\"welcome\", \"terminal\"],\n"
    "  \"start_menu\": [\n"
    "    { \"id\": \"welcome\", \"label\": \"Welcome\", \"target\": \"app:hello\" },\n"
    "    { \"id\": \"notes\", \"label\": \"Notes\", \"target\": \"app:notes\" }\n"
    "  ]\n"
    "}";

int main(void) {
    struct airjson_doc doc;
    const char *value;
    unsigned int len;
    int rc, ival;
    char text[64];
    unsigned int color;

    rc = OPEN(THEME);
    check(rc == AIRJSON_OK, "theme document is valid JSON");

    rc = airjson_root_get(&doc, "variant", &value, &len);
    check(rc == AIRJSON_OK, "root key variant is found");
    check(airjson_get_string(value, len, text, sizeof(text)) == AIRJSON_OK &&
          strcmp(text, "dark") == 0, "variant decodes to dark");

    rc = airjson_root_get(&doc, "radius", &value, &len);
    check(rc == AIRJSON_OK && airjson_get_int(value, len, &ival) == AIRJSON_OK &&
          ival == 12, "radius reads as 12");

    rc = airjson_root_get(&doc, "spacing", &value, &len);
    check(rc == AIRJSON_OK && airjson_get_int(value, len, &ival) == AIRJSON_OK &&
          ival == 10, "spacing reads as 10");

    rc = airjson_root_get(&doc, "animations", &value, &len);
    check(rc == AIRJSON_OK && airjson_get_bool(value, len, &ival) == AIRJSON_OK &&
          ival == 1, "animations reads as true");

    rc = airjson_root_get(&doc, "colors", &value, &len);
    check(rc == AIRJSON_OK, "colors object is found");
    rc = airjson_object_get(&doc, value, len, "accent", &value, &len);
    check(rc == AIRJSON_OK, "nested accent is found");
    check(airjson_get_color(value, len, &color) == AIRJSON_OK &&
          color == 0x6750A4u, "accent decodes to 0x6750A4");

    rc = airjson_root_get(&doc, "colors", &value, &len);
    rc = airjson_object_get(&doc, value, len, "background", &value, &len);
    check(rc == AIRJSON_OK && airjson_get_color(value, len, &color) == AIRJSON_OK &&
          color == 0x141218u, "background decodes to 0x141218");

    rc = airjson_root_get(&doc, "dock", &value, &len);
    check(rc == AIRJSON_OK && len > 2 && value[0] == '[', "dock array is found");

    rc = airjson_root_get(&doc, "missing", &value, &len);
    check(rc == AIRJSON_ERR_NOTFOUND, "missing root key is reported");

    rc = OPEN(LAUNCHER);
    check(rc == AIRJSON_OK, "launcher document is valid JSON");
    rc = airjson_root_get(&doc, "start_menu", &value, &len);
    check(rc == AIRJSON_OK && len > 2, "start_menu array is found");

    /* Escapes, including a surrogate pair and a plain / and newline. */
    {
        const char *esc = "{\"s\":\"a\\\"b\\\\c\\nd\\u00e9\\ud83d\\ude00\"}";
        rc = OPEN(esc);
        check(rc == AIRJSON_OK, "escaped document is valid");
        rc = airjson_root_get(&doc, "s", &value, &len);
        check(rc == AIRJSON_OK &&
              airjson_get_string(value, len, text, sizeof(text)) == AIRJSON_OK,
              "escaped string decodes");
        check(strcmp(text, "a\"b\\c\nd\xc3\xa9\xf0\x9f\x98\x80") == 0,
              "escape sequences resolve exactly");
    }

    /* Rejections. */
    {
        const char *bad[] = {
            "{\"a\":}", "{\"a\":1,}", "{\"a\" 1}", "{a:1}", "{\"a\":1} trailing",
            "[1,2", "\"unterminated", "{\"a\":01}", "{\"a\":1.}", "{\"a\":.5}",
        };
        unsigned int i;
        int all = 1;
        for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
            struct airjson_doc d;
            if (airjson_open(&d, bad[i], (unsigned int)strlen(bad[i])) == AIRJSON_OK)
                all = 0;
        }
        check(all, "malformed documents are rejected");
    }

    /* Excessive nesting must be refused rather than recursing forever. */
    {
        char deep[128];
        unsigned int i;
        for (i = 0; i < 40; i++)
            deep[i] = '[';
        for (i = 40; i < 80; i++)
            deep[i] = ']';
        check(airjson_open(&doc, deep, 80) == AIRJSON_ERR_DEPTH,
              "excessive nesting is rejected");
    }

    /* Type mismatches. */
    check(airjson_get_int("true", 4, &ival) == AIRJSON_ERR_TYPE,
          "boolean is not accepted as an integer");
    check(airjson_get_int("1.5", 3, &ival) == AIRJSON_ERR_TYPE,
          "non-integer number is rejected");
    check(airjson_get_string("12", 2, text, sizeof(text)) == AIRJSON_ERR_TYPE,
          "number is not accepted as a string");
    check(airjson_get_color("\"#fff\"", 6, &color) == AIRJSON_OK,
          "short hex colour is accepted");

    printf("%s\n", failures ? "airjson: FAILURES" : "airjson: all checks passed");
    return failures ? 1 : 0;
}
