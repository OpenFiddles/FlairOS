#include <stdio.h>
#include <string.h>

#include "airp.h"
#include "desktop_theme.h"
#include "sprunkui.h"

/* Builds component packages the way tools/airpkg.py does, so this test proves
   the kernel's theme path against the container format rather than against a
   hand-built struct. */
#define TX_APP_BASE  0x40000u

/* The theme module logs to the kernel serial port; a host build has none. */
void serial_str(const char *s) { (void)s; }

static int failures;

static void check(int cond, const char *what) {
    printf("%s %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond)
        failures++;
}

static void put_le32(unsigned char *p, unsigned int v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}

static void put_le16(unsigned char *p, unsigned int v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
}

/* Packs a resources-only package with the given theme and launcher JSON.
   Returns the total size written to `buf`. */
static unsigned int build_desktop_pkg(unsigned char *buf, unsigned int buf_cap,
                                      const char *theme, const char *launcher,
                                      const char *strings_json,
                                      unsigned int flags) {
    const char *names[3] = { "theme.json", "launcher.json", "strings.json" };
    const char *data[3] = { theme, launcher, strings_json };
    unsigned short types[3] = { AIRP_RT_THEME, AIRP_RT_UI, AIRP_RT_STRINGS };
    unsigned int i, manifest_off, manifest_len, tab_off, tab_len, name_off, file_size;
    const char *manifest = "{\"name\":\"FlairDesktop\",\"version\":\"2.0.0\"}";
    unsigned int offsets[3][2];

    manifest_len = (unsigned int)strlen(manifest);
    manifest_off = AIRP_HEADER_V2_SIZE;
    tab_off = manifest_off + manifest_len;
    tab_len = 3u * AIRP_RESOURCE_SIZE;
    name_off = tab_off + tab_len;

    memset(buf, 0, buf_cap);
    memcpy(buf + manifest_off, manifest, manifest_len);

    /* Names first, then data, so the name blob is contiguous after the table. */
    {
        unsigned int cursor = name_off;
        for (i = 0; i < 3; i++) {
            offsets[i][0] = cursor;
            memcpy(buf + cursor, names[i], strlen(names[i]) + 1);
            cursor += (unsigned int)strlen(names[i]) + 1;
        }
        for (i = 0; i < 3; i++) {
            offsets[i][1] = cursor;
            memcpy(buf + cursor, data[i], strlen(data[i]));
            cursor += (unsigned int)strlen(data[i]);
        }
        file_size = cursor;
    }

    for (i = 0; i < 3; i++) {
        unsigned char *e = buf + tab_off + i * AIRP_RESOURCE_SIZE;
        unsigned int len = (unsigned int)strlen(data[i]);
        put_le32(e + 0, offsets[i][0]);
        put_le32(e + 4, offsets[i][1]);
        put_le32(e + 8, len);
        put_le32(e + 12, airp_crc32((const unsigned char *)data[i], len));
        put_le16(e + 16, types[i]);
        put_le16(e + 18, AIRP_RF_REQUIRED);
        put_le32(e + 20, 0);
    }

    put_le32(buf + 0, AIRP_MAGIC);
    put_le16(buf + 4, AIRP_VERSION_V2);
    put_le16(buf + 6, AIRP_HEADER_V2_SIZE);
    put_le32(buf + 8, AIRP_API_LEVEL);
    put_le32(buf + 12, 0);              /* no executable */
    put_le32(buf + 16, 0);
    put_le32(buf + 20, 0);
    put_le32(buf + 24, flags);
    put_le32(buf + 28, manifest_off);
    put_le32(buf + 32, manifest_len);
    put_le32(buf + 36, 3);
    put_le32(buf + 40, tab_off);
    put_le32(buf + 44, tab_len);
    put_le32(buf + 48, AIRP_ARCH_I386);
    put_le32(buf + 56, airp_crc32(buf + AIRP_HEADER_V2_SIZE,
                                  file_size - AIRP_HEADER_V2_SIZE));
    put_le32(buf + 60, file_size);
    put_le32(buf + 52, 0);
    put_le32(buf + 52, airp_crc32(buf, AIRP_HEADER_V2_SIZE));
    return file_size;
}

static const char *DARK_THEME =
    "{\"name\":\"Default\",\"variant\":\"dark\",\"radius\":12,\"spacing\":10,"
    "\"border_width\":1,\"colors\":{"
    "\"background\":\"141218\",\"surface\":\"211F26\",\"elevated\":\"2B2930\","
    "\"border\":\"49454F\",\"text\":\"E6E0E9\",\"muted\":\"CAC4D0\","
    "\"accent\":\"6750A4\",\"accent_text\":\"FFFFFF\",\"success\":\"2E9E6B\","
    "\"warning\":\"E6A23C\",\"danger\":\"E5534B\"}}";

static const char *LIGHT_THEME =
    "{\"variant\":\"light\",\"radius\":20,\"spacing\":8,"
    "\"colors\":{\"surface\":\"FFFBFE\",\"text\":\"1C1B1F\",\"accent\":\"6750A4\"}}";

static const char *LOW_CONTRAST_THEME =
    "{\"colors\":{\"surface\":\"777777\",\"text\":\"808080\"}}";

static const char *BAD_JSON = "{\"colors\":{\"text\":}}";

#define PKG_CAP 4096

int main(void) {
    static unsigned char pkg[PKG_CAP];
    struct sp_theme theme;
    struct theme_report report;
    unsigned int len;
    int rc;

    /* The real shipped theme must load and meet the contrast floor. */
    sp_theme_default(&theme);
    len = build_desktop_pkg(pkg, PKG_CAP, DARK_THEME, "{\"dock\":[]}", "{}",
                            AIRP_FLAG_DESKTOP | AIRP_FLAG_SYSTEM | AIRP_FLAG_RESOURCES_ONLY);
    rc = desktop_theme_load_package(pkg, len, &theme, &report);
    check(rc == 1 && report.loaded, "shipped desktop theme loads from a package");
    check(report.fields_applied == 14, "every theme value is applied");
    check(theme.color.accent == 0x6750A4u, "accent colour comes from the package");
    check(theme.color.surface == 0x211F26u, "surface colour comes from the package");
    check(theme.color.text == 0xE6E0E9u, "text colour comes from the package");
    check(theme.radius == 12 && theme.spacing == 10, "metrics come from the package");
    check(report.contrast_ok, "dark palette clears the WCAG AA contrast floor");
    check(report.contrast_x100 >= 1200, "dark palette contrast is measured accurately");

    /* A light theme overrides only what it declares. */
    sp_theme_default(&theme);
    len = build_desktop_pkg(pkg, PKG_CAP, LIGHT_THEME, "{}", "{}",
                            AIRP_FLAG_DESKTOP | AIRP_FLAG_RESOURCES_ONLY);
    rc = desktop_theme_load_package(pkg, len, &theme, &report);
    check(rc == 1, "partial theme loads");
    check(theme.color.surface == 0xFFFBFEu && theme.color.text == 0x1C1B1Fu,
          "declared colours are applied");
    check(theme.color.border == 0x0049454Fu, "undeclared colour keeps the fallback");
    check(theme.radius == 20, "declared radius overrides the fallback");
    check(report.fields_applied == 5, "only declared values are counted");

    /* Contrast floor is measured, not assumed. */
    sp_theme_default(&theme);
    len = build_desktop_pkg(pkg, PKG_CAP, LOW_CONTRAST_THEME, "{}", "{}",
                            AIRP_FLAG_DESKTOP | AIRP_FLAG_RESOURCES_ONLY);
    rc = desktop_theme_load_package(pkg, len, &theme, &report);
    check(rc == 1, "low-contrast theme still loads (it is the designer's call)");
    check(!report.contrast_ok, "low-contrast theme is flagged as below WCAG AA");
    check(theme_contrast_x100(0xE6E0E9u, 0x211F26u) > theme_contrast_x100(0x808080u, 0x777777u),
          "contrast ordering is monotonic");

    /* Rejections leave the fallback palette untouched. */
    sp_theme_default(&theme);
    len = build_desktop_pkg(pkg, PKG_CAP, BAD_JSON, "{}", "{}",
                            AIRP_FLAG_DESKTOP | AIRP_FLAG_RESOURCES_ONLY);
    rc = desktop_theme_load_package(pkg, len, &theme, &report);
    check(rc == 0 && !report.loaded, "invalid theme JSON is rejected");
    check(theme.color.accent == 0x006750A4u, "fallback palette survives a bad theme");

    sp_theme_default(&theme);
    len = build_desktop_pkg(pkg, PKG_CAP, DARK_THEME, "{}", "{}",
                            AIRP_FLAG_SYSTEM | AIRP_FLAG_RESOURCES_ONLY);
    rc = desktop_theme_load_package(pkg, len, &theme, &report);
    check(rc == 0, "package without the desktop flag is refused");
    check(strstr(report.reason, "not a desktop package") != 0,
          "refusal reason names the missing flag");

    /* A corrupted package must be rejected by the container CRC. */
    sp_theme_default(&theme);
    len = build_desktop_pkg(pkg, PKG_CAP, DARK_THEME, "{}", "{}",
                            AIRP_FLAG_DESKTOP | AIRP_FLAG_RESOURCES_ONLY);
    pkg[AIRP_HEADER_V2_SIZE + 40] ^= 0xFF;
    rc = desktop_theme_load_package(pkg, len, &theme, &report);
    check(rc == 0, "corrupted package is refused");

    /* Out-of-range metrics are refused rather than clamped silently. */
    {
        const char *huge = "{\"radius\":400,\"colors\":{\"accent\":\"112233\"}}";
        sp_theme_default(&theme);
        len = build_desktop_pkg(pkg, PKG_CAP, huge, "{}", "{}",
                               AIRP_FLAG_DESKTOP | AIRP_FLAG_RESOURCES_ONLY);
        rc = desktop_theme_load_package(pkg, len, &theme, &report);
        check(rc == 0, "out-of-range radius is refused");
        check(theme.color.accent == 0x006750A4u, "no partial theme is left installed");
    }

    /* A missing stage is a clean no-op, not a crash. */
    sp_theme_default(&theme);
    check(desktop_theme_load_package(0, 0, &theme, &report) == 0,
          "absent package stage is handled");

    printf("%s\n", failures ? "desktop theme: FAILURES" : "desktop theme: all checks passed");
    return failures ? 1 : 0;
}
