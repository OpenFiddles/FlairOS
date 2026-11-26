#include "kernel.h"
#include "../../include/airjson.h"
#include "../../include/airp.h"
#include "../../include/desktop_gfx.h"
#include "../../include/sprunkui.h"
#include "../../include/desktop_theme.h"
#include "../../include/srgb_linear.h"

/* Session theme loader.
 *
 * FlairDesktop.airp is a component package: a container with a manifest and
 * typed resources but no executable. Its theme resource is declarative JSON,
 * and this module is the only place that turns that JSON into a live SprunkUI
 * theme. Nothing here is hardcoded to a particular palette, so a replacement
 * desktop package can restyle the whole session.
 *
 * Every value is bounded before it is used. A theme that is unreadable, out of
 * range, or fails the contrast floor is rejected and the caller falls back to
 * the built-in palette; a broken package must never leave the desktop in an
 * unreadable state.
 */

typedef unsigned int u32;

/* Radius and spacing are pixel values on an 800x600 logical surface. Beyond
   these bounds a widget would either lose its shape or overflow its cell.
   desktop_gfx_apply_theme enforces the same limits when it installs a theme. */
#define THEME_RADIUS_MAX   28
#define THEME_SPACING_MAX  40
#define THEME_BORDER_MAX   8

/* WCAG 2.1 AA for normal text is 4.5:1. The theme is checked against that
   floor because unreadable text is a functional defect, not a taste issue. */
#define THEME_MIN_CONTRAST_X100 450


/* Small unsigned-to-decimal helper for diagnostic logs. */
static void serial_dec_local(u32 v) {
    char buf[12];
    int n = 11;
    buf[n] = 0;
    do {
        buf[--n] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v);
    serial_str(buf + n);
}

/* Logs a theme decision to the serial port. Kept next to the decision so the
   message and the values it reports cannot drift apart. */
void desktop_theme_log(const struct theme_report *report) {
    if (!report)
        return;
    if (report->loaded) {
        serial_str("theme: FlairDesktop.airp applied, ");
        serial_dec_local((u32)report->fields_applied);
        serial_str(" values, contrast ");
        serial_dec_local(report->contrast_x100 / 100u);
        serial_str(".");
        serial_dec_local(report->contrast_x100 % 100u);
        serial_str(":1");
        serial_str(report->contrast_ok ? "\n" : " (below WCAG AA)\n");
    } else {
        serial_str("theme: using built-in palette (");
        serial_str(report->reason ? report->reason : "unknown reason");
        serial_str(")\n");
    }
}

/* Sums the linear-light intensities of an XRGB8888 colour. */
static u32 linear_luminance(u32 color) {
    const u32 r = (color >> 16) & 0xFFu;
    const u32 g = (color >> 8) & 0xFFu;
    const u32 b = color & 0xFFu;
    /* Rec. 709 coefficients, expressed in thousandths so the result stays an
       integer: 0.2126, 0.7152, 0.0722. */
    return (2126u * srgb_linear_at(r) + 7152u * srgb_linear_at(g) +
            722u * srgb_linear_at(b)) / 1000u;
}

/* WCAG relative-luminance contrast ratio, returned times 100.
   ratio = (L1 + 0.05) / (L2 + 0.05) with L in 0..1. Luminance is scaled to
   0..10000, so 0.05 becomes 500. */
unsigned int theme_contrast_x100(u32 a, u32 b) {
    u32 la = linear_luminance(a);
    u32 lb = linear_luminance(b);
    u32 hi, lo;
    if (la >= lb) {
        hi = la;
        lo = lb;
    } else {
        hi = lb;
        lo = la;
    }
    return ((hi + 500u) * 100u) / (lo + 500u);
}

static int root_color(const struct airjson_doc *doc, const char *object_key,
                      const char *key, u32 *out) {
    const char *value;
    u32 len;
    if (airjson_root_get(doc, object_key, &value, &len) != AIRJSON_OK)
        return 0;
    if (airjson_object_get(doc, value, len, key, &value, &len) != AIRJSON_OK)
        return 0;
    return airjson_get_color(value, len, out) == AIRJSON_OK;
}

static int root_int(const struct airjson_doc *doc, const char *key, int *out) {
    const char *value;
    u32 len;
    int parsed;
    if (airjson_root_get(doc, key, &value, &len) != AIRJSON_OK)
        return 0;
    if (airjson_get_int(value, len, &parsed) != AIRJSON_OK)
        return 0;
    *out = parsed;
    return 1;
}

/* Applies a theme resource to `theme`. `theme` must already hold the fallback
   palette, so any value the package omits keeps a sane default.
 *
 * Parsing works on a copy and commits only on success: a theme that turns out to
 * be out of range half-way through must not leave the session partially
 * restyled, because a single rejected value would otherwise be invisible while
 * the rest of the package silently took effect. */
static int apply_theme_resource(const char *json, u32 len, struct sp_theme *theme,
                                struct theme_report *report) {
    struct airjson_doc doc;
    struct sp_theme candidate;
    u32 color;

    if (airjson_open(&doc, json, len) != AIRJSON_OK) {
        report->reason = "theme resource is not valid JSON";
        return 0;
    }
    candidate = *theme;

    /* Each colour is read independently: a package may declare any subset and
       the rest keep the fallback palette. Writes go to the candidate copy so a
       rejected theme cannot leave the session half-restyled. */
    if (root_color(&doc, "colors", "background", &color)) {
        candidate.color.background = color;
        report->fields_applied++;
    }
    if (root_color(&doc, "colors", "surface", &color)) {
        candidate.color.surface = color;
        report->fields_applied++;
    }
    if (root_color(&doc, "colors", "elevated", &color)) {
        candidate.color.elevated = color;
        report->fields_applied++;
    }
    if (root_color(&doc, "colors", "border", &color)) {
        candidate.color.border = color;
        report->fields_applied++;
    }
    if (root_color(&doc, "colors", "text", &color)) {
        candidate.color.text = color;
        report->fields_applied++;
    }
    if (root_color(&doc, "colors", "muted", &color)) {
        candidate.color.muted = color;
        report->fields_applied++;
    }
    if (root_color(&doc, "colors", "accent", &color)) {
        candidate.color.accent = color;
        report->fields_applied++;
    }
    if (root_color(&doc, "colors", "accent_text", &color)) {
        candidate.color.accent_text = color;
        report->fields_applied++;
    }
    if (root_color(&doc, "colors", "success", &color)) {
        candidate.color.success = color;
        report->fields_applied++;
    }
    if (root_color(&doc, "colors", "warning", &color)) {
        candidate.color.warning = color;
        report->fields_applied++;
    }
    if (root_color(&doc, "colors", "danger", &color)) {
        candidate.color.danger = color;
        report->fields_applied++;
    }

    {
        int v;
        if (root_int(&doc, "radius", &v)) {
            if (v < 0 || v > THEME_RADIUS_MAX) {
                report->reason = "theme radius is out of range";
                return 0;
            }
            candidate.radius = v;
            report->fields_applied++;
        }
        if (root_int(&doc, "spacing", &v)) {
            if (v < 0 || v > THEME_SPACING_MAX) {
                report->reason = "theme spacing is out of range";
                return 0;
            }
            candidate.spacing = v;
            report->fields_applied++;
        }
        if (root_int(&doc, "border_width", &v)) {
            if (v < 0 || v > THEME_BORDER_MAX) {
                report->reason = "theme border width is out of range";
                return 0;
            }
            candidate.border_width = v;
            report->fields_applied++;
        }
    }

    if (report->fields_applied == 0) {
        report->reason = "theme resource declares no known values";
        return 0;
    }

    /* Colours are masked and clamped once, when the theme is installed by
       desktop_gfx_apply_theme. Measure contrast on the values as parsed: the
       high byte is already zero because airjson_get_color parses six digits. */
    report->contrast_x100 = theme_contrast_x100(candidate.color.text, candidate.color.surface);
    report->contrast_ok = report->contrast_x100 >= THEME_MIN_CONTRAST_X100;
    report->loaded = 1;
    *theme = candidate;
    return 1;
}

/* Loads FlairDesktop.airp from a staged boot buffer, validates the container,
   finds the theme resource and applies it. Returns 1 when the package theme was
   applied, 0 when the built-in palette was kept. */
int desktop_theme_load_package(const u8 *stage, u32 stage_len, struct sp_theme *theme,
                               struct theme_report *report) {
    struct airp_package pkg;
    struct airp_view view;
    unsigned int type = 0;
    int rc;

    if (!stage || !theme || !report)
        return 0;
    report->loaded = 0;
    report->contrast_ok = 0;
    report->contrast_x100 = 0;
    report->fields_applied = 0;
    report->reason = "desktop package not found";

    rc = airp_open(stage, stage_len, AIRP_ARCH_I386, &pkg);
    if (rc != AIRP_LOAD_OK) {
        report->reason = "desktop package failed validation";
        serial_str("theme: desktop package rejected: ");
        serial_str(airp_error_string(rc));
        serial_str("\n");
        return 0;
    }
    if (!(pkg.flags & AIRP_FLAG_DESKTOP)) {
        report->reason = "staged package is not a desktop package";
        return 0;
    }
    if (airp_find_resource(&pkg, "theme.json", &type, &view) != AIRP_LOAD_OK) {
        report->reason = "desktop package has no theme.json resource";
        return 0;
    }
    if (type != AIRP_RT_THEME) {
        report->reason = "desktop theme.json is not a theme resource";
        return 0;
    }

    return apply_theme_resource((const char *)view.data, view.size, theme, report);
}
