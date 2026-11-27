#include "../../include/airjson.h"

typedef unsigned int u32;

/* AirJSON is a validating reader: airjson_open parses the whole document once
   so later lookups never have to trust the input. It is freestanding, allocates
   nothing, and only ever reads the caller's buffer. */

static int is_ws(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void skip_ws(const char *t, u32 len, u32 *pos) {
    while (*pos < len && is_ws(t[*pos]))
        (*pos)++;
}

/* Parses one quoted string starting at *pos (which must point at '"') and
   advances past the closing quote. Escape sequences are validated here; they
   are resolved by decode_string when a caller asks for the bytes. */
static int parse_string_raw(const char *t, u32 len, u32 *pos) {
    u32 i = *pos;
    if (i >= len || t[i] != '"')
        return AIRJSON_ERR_SYNTAX;
    i++;
    while (i < len) {
        unsigned char c = (unsigned char)t[i];
        if (c == '"') {
            *pos = i + 1;
            return AIRJSON_OK;
        }
        if (c < 0x20)
            return AIRJSON_ERR_STRING;
        if (c == '\\') {
            u32 k;
            i++;
            if (i >= len)
                return AIRJSON_ERR_STRING;
            switch (t[i]) {
            case '"': case '\\': case '/': case 'b':
            case 'f': case 'n': case 'r': case 't':
                i++;
                break;
            case 'u':
                if (i + 4 >= len)
                    return AIRJSON_ERR_STRING;
                for (k = 1; k <= 4; k++)
                    if (hex_value(t[i + k]) < 0)
                        return AIRJSON_ERR_STRING;
                i += 5;
                break;
            default:
                return AIRJSON_ERR_STRING;
            }
            continue;
        }
        i++;
    }
    return AIRJSON_ERR_STRING;
}

/* Strict JSON number grammar: -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)? */
static int parse_number_raw(const char *t, u32 len, u32 *pos) {
    u32 i = *pos;
    if (i < len && t[i] == '-')
        i++;
    if (i >= len)
        return AIRJSON_ERR_SYNTAX;
    if (t[i] == '0') {
        i++;
    } else if (t[i] >= '1' && t[i] <= '9') {
        while (i < len && is_digit(t[i]))
            i++;
    } else {
        return AIRJSON_ERR_SYNTAX;
    }
    if (i < len && t[i] == '.') {
        i++;
        if (i >= len || !is_digit(t[i]))
            return AIRJSON_ERR_SYNTAX;
        while (i < len && is_digit(t[i]))
            i++;
    }
    if (i < len && (t[i] == 'e' || t[i] == 'E')) {
        i++;
        if (i < len && (t[i] == '+' || t[i] == '-'))
            i++;
        if (i >= len || !is_digit(t[i]))
            return AIRJSON_ERR_SYNTAX;
        while (i < len && is_digit(t[i]))
            i++;
    }
    *pos = i;
    return AIRJSON_OK;
}

static int parse_value(const char *t, u32 len, u32 *pos, u32 depth);

static int parse_object(const char *t, u32 len, u32 *pos, u32 depth) {
    u32 i = *pos + 1;
    skip_ws(t, len, &i);
    if (i < len && t[i] == '}') {
        *pos = i + 1;
        return AIRJSON_OK;
    }
    for (;;) {
        int rc = parse_string_raw(t, len, &i);
        if (rc != AIRJSON_OK)
            return rc;
        skip_ws(t, len, &i);
        if (i >= len || t[i] != ':')
            return AIRJSON_ERR_SYNTAX;
        i++;
        rc = parse_value(t, len, &i, depth + 1);
        if (rc != AIRJSON_OK)
            return rc;
        skip_ws(t, len, &i);
        if (i < len && t[i] == ',') {
            i++;
            skip_ws(t, len, &i);
            continue;
        }
        if (i < len && t[i] == '}') {
            *pos = i + 1;
            return AIRJSON_OK;
        }
        return AIRJSON_ERR_SYNTAX;
    }
}

static int parse_array(const char *t, u32 len, u32 *pos, u32 depth) {
    u32 i = *pos + 1;
    skip_ws(t, len, &i);
    if (i < len && t[i] == ']') {
        *pos = i + 1;
        return AIRJSON_OK;
    }
    for (;;) {
        int rc = parse_value(t, len, &i, depth + 1);
        if (rc != AIRJSON_OK)
            return rc;
        skip_ws(t, len, &i);
        if (i < len && t[i] == ',') {
            i++;
            skip_ws(t, len, &i);
            continue;
        }
        if (i < len && t[i] == ']') {
            *pos = i + 1;
            return AIRJSON_OK;
        }
        return AIRJSON_ERR_SYNTAX;
    }
}

static int match_literal(const char *t, u32 len, u32 *pos, const char *word) {
    u32 i = *pos;
    u32 k = 0;
    while (word[k]) {
        if (i + k >= len || t[i + k] != word[k])
            return AIRJSON_ERR_SYNTAX;
        k++;
    }
    *pos = i + k;
    return AIRJSON_OK;
}

static int parse_value(const char *t, u32 len, u32 *pos, u32 depth) {
    char c;
    if (depth > AIRJSON_MAX_DEPTH)
        return AIRJSON_ERR_DEPTH;
    skip_ws(t, len, pos);
    if (*pos >= len)
        return AIRJSON_ERR_SYNTAX;
    c = t[*pos];
    if (c == '{')
        return parse_object(t, len, pos, depth);
    if (c == '[')
        return parse_array(t, len, pos, depth);
    if (c == '"')
        return parse_string_raw(t, len, pos);
    if (c == 't')
        return match_literal(t, len, pos, "true");
    if (c == 'f')
        return match_literal(t, len, pos, "false");
    if (c == 'n')
        return match_literal(t, len, pos, "null");
    if (c == '-' || is_digit(c))
        return parse_number_raw(t, len, pos);
    return AIRJSON_ERR_SYNTAX;
}

int airjson_open(struct airjson_doc *doc, const char *text, u32 len) {
    u32 pos = 0;
    int rc;
    if (!doc || (!text && len))
        return AIRJSON_ERR_SYNTAX;
    rc = parse_value(text, len, &pos, 0);
    if (rc != AIRJSON_OK)
        return rc;
    skip_ws(text, len, &pos);
    if (pos != len)
        return AIRJSON_ERR_SYNTAX;
    doc->text = text;
    doc->len = len;
    return AIRJSON_OK;
}

static void trim(const char *t, u32 *start, u32 *end) {
    u32 s = *start;
    u32 e = *end;
    while (s < e && is_ws(t[s]))
        s++;
    while (e > s && is_ws(t[e - 1]))
        e--;
    *start = s;
    *end = e;
}

static int append_utf8(char *out, u32 cap, u32 *used, u32 cp) {
    if (cp < 0x80) {
        if (*used + 1 >= cap) return AIRJSON_ERR_RANGE;
        out[(*used)++] = (char)cp;
    } else if (cp < 0x800) {
        if (*used + 2 >= cap) return AIRJSON_ERR_RANGE;
        out[(*used)++] = (char)(0xC0 | (cp >> 6));
        out[(*used)++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        if (*used + 3 >= cap) return AIRJSON_ERR_RANGE;
        out[(*used)++] = (char)(0xE0 | (cp >> 12));
        out[(*used)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[(*used)++] = (char)(0x80 | (cp & 0x3F));
    } else {
        if (*used + 4 >= cap) return AIRJSON_ERR_RANGE;
        out[(*used)++] = (char)(0xF0 | (cp >> 18));
        out[(*used)++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[(*used)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[(*used)++] = (char)(0x80 | (cp & 0x3F));
    }
    return AIRJSON_OK;
}

/* Decodes the string body between t[start] == '"' and the matching quote at
   end-1 into out, resolving escapes. */
static int decode_string(const char *t, u32 start, u32 end, char *out, u32 cap) {
    u32 i = start + 1;
    u32 used = 0;
    if (!out || cap == 0)
        return AIRJSON_ERR_RANGE;
    while (i + 1 < end) {
        unsigned char c = (unsigned char)t[i];
        if (c != '\\') {
            if (used + 1 >= cap)
                return AIRJSON_ERR_RANGE;
            out[used++] = (char)c;
            i++;
            continue;
        }
        i++;
        if (used + 1 >= cap)
            return AIRJSON_ERR_RANGE;
        switch (t[i]) {
        case '"':  out[used++] = '"';  i++; break;
        case '\\': out[used++] = '\\'; i++; break;
        case '/':  out[used++] = '/';  i++; break;
        case 'b':  out[used++] = '\b'; i++; break;
        case 'f':  out[used++] = '\f'; i++; break;
        case 'n':  out[used++] = '\n'; i++; break;
        case 'r':  out[used++] = '\r'; i++; break;
        case 't':  out[used++] = '\t'; i++; break;
        case 'u': {
            u32 cp = 0;
            u32 k;
            int rc;
            for (k = 1; k <= 4; k++)
                cp = (cp << 4) | (u32)hex_value(t[i + k]);
            i += 5;
            /* Combine a UTF-16 surrogate pair when both halves are escapes. */
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 5 < end &&
                t[i] == '\\' && t[i + 1] == 'u') {
                u32 low = 0;
                for (k = 2; k <= 5; k++)
                    low = (low << 4) | (u32)hex_value(t[i + k]);
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    i += 6;
                }
            }
            rc = append_utf8(out, cap, &used, cp);
            if (rc != AIRJSON_OK)
                return rc;
            break;
        }
        default:
            return AIRJSON_ERR_STRING;
        }
    }
    out[used] = 0;
    return AIRJSON_OK;
}

/* Scans an object span for `key`, returning the raw span of its value.
   The whole span must be exactly one JSON object. */
static int object_lookup(const char *t, u32 len, const char *key,
                         const char **value, u32 *value_len) {
    u32 pos = 0;
    if (!t || !key)
        return AIRJSON_ERR_SYNTAX;
    trim(t, &pos, &len);
    if (pos >= len || t[pos] != '{')
        return AIRJSON_ERR_TYPE;
    {
        u32 i = pos + 1;
        skip_ws(t, len, &i);
        if (i < len && t[i] == '}') {
            i++;
            skip_ws(t, len, &i);
            return (i == len) ? AIRJSON_ERR_NOTFOUND : AIRJSON_ERR_SYNTAX;
        }
        for (;;) {
            char name[128];
            u32 name_start, name_end, key_len;
            int rc;
            name_start = i;
            rc = parse_string_raw(t, len, &i);
            if (rc != AIRJSON_OK)
                return rc;
            name_end = i;
            skip_ws(t, len, &i);
            if (i >= len || t[i] != ':')
                return AIRJSON_ERR_SYNTAX;
            i++;
            skip_ws(t, len, &i);
            {
                u32 vstart = i;
                rc = parse_value(t, len, &i, 1);
                if (rc != AIRJSON_OK)
                    return rc;
                rc = decode_string(t, name_start, name_end, name, sizeof(name));
                if (rc == AIRJSON_OK) {
                    key_len = 0;
                    while (key[key_len])
                        key_len++;
                    {
                        u32 k = 0;
                        int same = 1;
                        while (k < key_len) {
                            if (name[k] != key[k]) { same = 0; break; }
                            k++;
                        }
                        if (same && name[k] == 0) {
                            if (value)
                                *value = t + vstart;
                            if (value_len)
                                *value_len = i - vstart;
                            return AIRJSON_OK;
                        }
                    }
                }
            }
            skip_ws(t, len, &i);
            if (i < len && t[i] == ',') {
                i++;
                skip_ws(t, len, &i);
                continue;
            }
            if (i < len && t[i] == '}') {
                i++;
                skip_ws(t, len, &i);
                return (i == len) ? AIRJSON_ERR_NOTFOUND : AIRJSON_ERR_SYNTAX;
            }
            return AIRJSON_ERR_SYNTAX;
        }
    }
}

int airjson_object_get(const struct airjson_doc *doc, const char *object,
                       u32 object_len, const char *key,
                       const char **value, u32 *value_len) {
    (void)doc;
    return object_lookup(object, object_len, key, value, value_len);
}

int airjson_root_get(const struct airjson_doc *doc, const char *key,
                     const char **value, u32 *value_len) {
    if (!doc)
        return AIRJSON_ERR_SYNTAX;
    return object_lookup(doc->text, doc->len, key, value, value_len);
}

int airjson_array_count(const char *array, u32 len, int *out) {
    u32 pos = 0;
    int count = 0;
    if (!array || !out)
        return AIRJSON_ERR_SYNTAX;
    skip_ws(array, len, &pos);
    if (pos >= len || array[pos] != '[')
        return AIRJSON_ERR_TYPE;
    pos++;
    skip_ws(array, len, &pos);
    if (pos < len && array[pos] == ']') {
        *out = 0;
        return AIRJSON_OK;
    }
    for (;;) {
        if (parse_value(array, len, &pos, 0) != AIRJSON_OK)
            return AIRJSON_ERR_SYNTAX;
        count++;
        skip_ws(array, len, &pos);
        if (pos < len && array[pos] == ',') {
            pos++;
            skip_ws(array, len, &pos);
            continue;
        }
        if (pos < len && array[pos] == ']') {
            *out = count;
            return AIRJSON_OK;
        }
        return AIRJSON_ERR_SYNTAX;
    }
}

int airjson_array_at(const char *array, u32 len, int index,
                     const char **value, u32 *value_len) {
    u32 pos = 0;
    int i = 0;
    if (!array || !value || !value_len || index < 0)
        return AIRJSON_ERR_RANGE;
    skip_ws(array, len, &pos);
    if (pos >= len || array[pos] != '[')
        return AIRJSON_ERR_TYPE;
    pos++;
    skip_ws(array, len, &pos);
    if (pos < len && array[pos] == ']')
        return AIRJSON_ERR_RANGE;
    for (;;) {
        u32 start = pos;
        if (parse_value(array, len, &pos, 0) != AIRJSON_OK)
            return AIRJSON_ERR_SYNTAX;
        if (i == index) {
            *value = array + start;
            *value_len = pos - start;
            return AIRJSON_OK;
        }
        i++;
        skip_ws(array, len, &pos);
        if (pos < len && array[pos] == ',') {
            pos++;
            skip_ws(array, len, &pos);
            continue;
        }
        return AIRJSON_ERR_RANGE;
    }
}

int airjson_get_int(const char *value, u32 len, int *out) {
    u32 start = 0, end = len, pos;
    int negative = 0;
    u32 i;
    unsigned long acc = 0;
    if (!value || !out)
        return AIRJSON_ERR_SYNTAX;
    trim(value, &start, &end);
    if (start >= end)
        return AIRJSON_ERR_SYNTAX;
    pos = start;
    if (parse_number_raw(value, end, &pos) != AIRJSON_OK || pos != end)
        return AIRJSON_ERR_TYPE;
    for (i = start; i < end; i++)
        if (value[i] == '.' || value[i] == 'e' || value[i] == 'E')
            return AIRJSON_ERR_TYPE;
    i = start;
    if (value[i] == '-') {
        negative = 1;
        i++;
    }
    for (; i < end; i++) {
        acc = acc * 10u + (unsigned long)(value[i] - '0');
        if (acc > 2147483648ul)
            return AIRJSON_ERR_RANGE;
    }
    if (negative) {
        if (acc > 2147483648ul)
            return AIRJSON_ERR_RANGE;
        *out = (acc == 2147483648ul) ? (int)(-2147483647 - 1) : -(int)acc;
    } else {
        if (acc > 2147483647ul)
            return AIRJSON_ERR_RANGE;
        *out = (int)acc;
    }
    return AIRJSON_OK;
}

int airjson_get_string(const char *value, u32 len, char *out, u32 cap) {
    u32 start = 0, end = len, pos;
    if (!value || !out || cap == 0)
        return AIRJSON_ERR_SYNTAX;
    trim(value, &start, &end);
    if (start >= end || value[start] != '"')
        return AIRJSON_ERR_TYPE;
    pos = start;
    if (parse_string_raw(value, end, &pos) != AIRJSON_OK || pos != end)
        return AIRJSON_ERR_TYPE;
    return decode_string(value, start, end, out, cap);
}

int airjson_get_bool(const char *value, u32 len, int *out) {
    u32 start = 0, end = len;
    if (!value || !out)
        return AIRJSON_ERR_SYNTAX;
    trim(value, &start, &end);
    if (end - start == 4 && value[start] == 't' && value[start + 1] == 'r' &&
        value[start + 2] == 'u' && value[start + 3] == 'e') {
        *out = 1;
        return AIRJSON_OK;
    }
    if (end - start == 5 && value[start] == 'f' && value[start + 1] == 'a' &&
        value[start + 2] == 'l' && value[start + 3] == 's' &&
        value[start + 4] == 'e') {
        *out = 0;
        return AIRJSON_OK;
    }
    return AIRJSON_ERR_TYPE;
}

int airjson_get_color(const char *value, u32 len, u32 *out) {
    char buf[16];
    u32 i = 0, digits = 0, result = 0;
    int rc;
    if (!out)
        return AIRJSON_ERR_SYNTAX;
    rc = airjson_get_string(value, len, buf, sizeof(buf));
    if (rc != AIRJSON_OK)
        return rc;
    if (buf[0] == '#')
        i = 1;
    while (buf[i]) {
        int h = hex_value(buf[i]);
        if (h < 0 || digits == 6)
            return AIRJSON_ERR_STRING;
        result = (result << 4) | (u32)h;
        digits++;
        i++;
    }
    if (digits == 0)
        return AIRJSON_ERR_STRING;
    *out = result;
    return AIRJSON_OK;
}
