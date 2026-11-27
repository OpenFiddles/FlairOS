#ifndef FLAIROS_WIFI_IEEE80211_H
#define FLAIROS_WIFI_IEEE80211_H

#include <stdint.h>

#define WIFI_MAX_BSS   16
#define WIFI_SSID_MAX  32

/* What the RSN / WPA information elements say about a BSS. More than one bit
   can be set (for example a transition-mode AP advertising WPA and WPA2). */
#define WIFI_SEC_OPEN       0u
#define WIFI_SEC_WEP        (1u << 0)
#define WIFI_SEC_WPA_PSK    (1u << 1)
#define WIFI_SEC_WPA2_PSK   (1u << 2)
#define WIFI_SEC_ENTERPRISE (1u << 3)
#define WIFI_SEC_TKIP       (1u << 4)
#define WIFI_SEC_CCMP       (1u << 5)

struct wifi_bss {
    int used;
    uint8_t bssid[6];
    char ssid[WIFI_SSID_MAX + 1];
    uint8_t ssid_len;   /* 0 for a hidden SSID */
    uint8_t channel;    /* 0 when no DS Parameter Set element was present */
    uint32_t security;
    int8_t rssi;        /* BT-style contract: -128 when the driver has no value */
};

/* RSSI value meaning "the driver did not report one". */
#define WIFI_RSSI_UNKNOWN (-128)

/* Parse a complete 802.11 beacon (subtype 0x08) or probe response (0x50),
   management header included. Returns 0 and fills *out for a well-formed
   frame, -1 otherwise. A truncated information element ends the walk rather
   than inventing data. */
int wifi_parse_beacon(const uint8_t *frame, int len, int8_t rssi, struct wifi_bss *out);

const char *wifi_security_text(uint32_t security);

#endif
