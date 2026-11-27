/* SPDX-License-Identifier: AGPL-3.0-only */
/* 802.11 beacon / probe-response parsing.
 *
 * Frame layout (IEEE 802.11-2020, 9.3.3.2 and 9.3.3.3):
 *   Frame Control(2) Duration(2) DA(6) SA(6) BSSID(6) Seq(2)  = 24 bytes
 *   Timestamp(8) Beacon Interval(2) Capability(2)            = 12 bytes
 *   Information elements...
 *
 * Only what a scan needs is decoded: SSID (0), DS Parameter Set (3), RSN (48)
 * and the WPA vendor element (221, OUI 00:50:F2 type 1). Everything else is
 * skipped by length. No frame content is invented and no heap is used.
 */
#include "wifi_ieee80211.h"

#define HDR_LEN 24
#define FIXED_LEN 12
#define IE_SSID 0u
#define IE_DS_PARAMS 3u
#define IE_RSN 48u
#define IE_VENDOR 221u

#define OUI_RSN_0 0x00u
#define OUI_RSN_1 0x0Fu
#define OUI_RSN_2 0xACu
#define OUI_WPA_0 0x00u
#define OUI_WPA_1 0x50u
#define OUI_WPA_2 0xF2u

/* RSN elements use the IEEE 802.11 OUI; the older WPA vendor element uses
   Microsoft's, and both list their suites with the same layout. */
static const uint8_t oui_rsn[3] = { OUI_RSN_0, OUI_RSN_1, OUI_RSN_2 };
static const uint8_t oui_wpa[3] = { OUI_WPA_0, OUI_WPA_1, OUI_WPA_2 };

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void copy_bytes(char *dst, const uint8_t *src, int n) {
    for (int i = 0; i < n; i++) dst[i] = (char)src[i];
}

/* Walk one cipher/AKM suite list. The version field is skipped by the caller.
   `rsn` selects which PSK bit an AKM 2 entry sets. */
static int parse_suites(const uint8_t *v, int len, const uint8_t oui[3], int rsn,
                        uint32_t *sec) {
    int i = 2, n = 0, k = 0;

    if (len < 2) return -1;
    if (i + 4 > len) return -1;
    if (v[i] == oui[0] && v[i + 1] == oui[1] && v[i + 2] == oui[2]) {
        uint8_t type = v[i + 3];
        if (type == 2u) *sec |= WIFI_SEC_TKIP;
        else if (type == 4u) *sec |= WIFI_SEC_CCMP;
        else if (type == 5u) *sec |= WIFI_SEC_WEP; /* WEP-104 */
    }
    i += 4;

    if (i + 2 > len) return -1;
    n = rd16(v + i);
    i += 2;
    for (k = 0; k < n; k++) {
        if (i + 4 > len) return -1;
        if (v[i] == oui[0] && v[i + 1] == oui[1] && v[i + 2] == oui[2]) {
            uint8_t type = v[i + 3];
            if (type == 2u) *sec |= WIFI_SEC_TKIP;      /* TKIP */
            else if (type == 4u) *sec |= WIFI_SEC_CCMP; /* CCMP-128 */
        }
        i += 4;
    }

    if (i + 2 > len) return -1;
    n = rd16(v + i);
    i += 2;
    for (k = 0; k < n; k++) {
        if (i + 4 > len) return -1;
        if (v[i] == oui[0] && v[i + 1] == oui[1] && v[i + 2] == oui[2]) {
            uint8_t type = v[i + 3];
            if (type == 1u) *sec |= WIFI_SEC_ENTERPRISE;             /* 802.1X */
            else if (type == 2u || type == 4u || type == 6u)         /* PSK, FT-PSK, PSK-SHA256 */
                *sec |= rsn ? WIFI_SEC_WPA2_PSK : WIFI_SEC_WPA_PSK;
        }
        i += 4;
    }
    return 0;
}

int wifi_parse_beacon(const uint8_t *f, int len, int8_t rssi, struct wifi_bss *out) {
    uint8_t fc0, subtype;
    uint16_t capability = 0;
    int i;

    if (!f || !out || len < HDR_LEN + FIXED_LEN) return -1;
    fc0 = f[0];
    if ((fc0 & 0x03u) != 0u) return -1;   /* protocol version must be 0 */
    if ((fc0 & 0x0Cu) != 0u) return -1;   /* management frames only */
    subtype = (uint8_t)(fc0 >> 4);
    if (subtype != 0x08u && subtype != 0x05u) return -1; /* beacon / probe response */

    out->used = 1;
    for (i = 0; i < 6; i++) out->bssid[i] = f[16 + i];
    for (i = 0; i <= WIFI_SSID_MAX; i++) out->ssid[i] = 0;
    out->ssid_len = 0;
    out->channel = 0;
    out->security = WIFI_SEC_OPEN;
    out->rssi = rssi;
    capability = rd16(f + HDR_LEN + 8 + 2);

    i = HDR_LEN + FIXED_LEN;
    while (i + 2 <= len) {
        uint8_t id = f[i];
        uint8_t ielen = f[i + 1];
        const uint8_t *v = f + i + 2;
        if (i + 2 + (int)ielen > len) break; /* truncated element: stop here */
        if (id == IE_SSID) {
            if (ielen > WIFI_SSID_MAX) return -1;
            copy_bytes(out->ssid, v, ielen);
            out->ssid[ielen] = 0;
            out->ssid_len = ielen;
        } else if (id == IE_DS_PARAMS) {
            if (ielen >= 1) out->channel = v[0];
        } else if (id == IE_RSN) {
            if (parse_suites(v, ielen, oui_rsn, 1, &out->security) != 0) return -1;
        } else if (id == IE_VENDOR) {
            if (ielen >= 4 && v[0] == OUI_WPA_0 && v[1] == OUI_WPA_1 &&
                v[2] == OUI_WPA_2 && v[3] == 1u) {
                if (parse_suites(v + 4, ielen - 4, oui_wpa, 0, &out->security) != 0) return -1;
            }
        }
        i += 2 + (int)ielen;
    }

    /* The privacy bit without an RSN/WPA element is what WEP looks like. */
    if ((capability & 0x0010u) && out->security == WIFI_SEC_OPEN)
        out->security = WIFI_SEC_WEP;
    return 0;
}

const char *wifi_security_text(uint32_t sec) {
    if (sec & WIFI_SEC_ENTERPRISE) return "WPA2-Enterprise";
    if ((sec & WIFI_SEC_WPA2_PSK) && (sec & WIFI_SEC_WPA_PSK)) return "WPA/WPA2-PSK";
    if (sec & WIFI_SEC_WPA2_PSK) return "WPA2-PSK";
    if (sec & WIFI_SEC_WPA_PSK) return "WPA-PSK";
    if (sec & WIFI_SEC_WEP) return "WEP";
    return "open";
}
