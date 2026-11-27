/* SPDX-License-Identifier: AGPL-3.0-only */
/* FlairOS WiFi core: adapter interface, scan table and connect state.
 *
 * No hardware is claimed without a registered driver. Beacons enter through
 * wifi_rx_beacon() (the driver already stripped the radio header), and the
 * security mask is decoded from the RSN/WPA elements, not assumed.
 */
#include "wifi_core.h"
#include "wpa2.h"

static const struct wifi_drv_ops *g_ops = 0;
static struct wifi_bss g_scan[WIFI_MAX_BSS];
static int g_scan_count = 0;
static enum wifi_state g_state = WIFI_STATE_OFF;
static int g_powered = 0;
static int g_connected = 0;
static char g_ssid[WIFI_SSID_MAX + 1];
static char g_status[56] = "WiFi off";

static int str_len(const char *s) {
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static void str_copy(char *d, const char *s, int cap) {
    int i = 0;
    while (s && s[i] && i + 1 < cap) { d[i] = s[i]; i++; }
    if (cap > 0) d[i] = 0;
}

static int bssid_same(const uint8_t *a, const uint8_t *b) {
    for (int i = 0; i < 6; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static void set_error(const char *text) {
    g_state = WIFI_STATE_ERROR;
    str_copy(g_status, text, sizeof(g_status));
}

int wifi_register_adapter(const struct wifi_drv_ops *ops) {
    if (!ops || !ops->scan || !ops->connect) return -1;
    g_ops = ops;
    g_state = WIFI_STATE_OFF;
    g_powered = 0;
    g_connected = 0;
    g_scan_count = 0;
    g_ssid[0] = 0;
    str_copy(g_status, "WiFi off", sizeof(g_status));
    return 0;
}

void wifi_unregister_adapter(void) {
    g_ops = 0;
    g_powered = 0;
    g_connected = 0;
    g_scan_count = 0;
    g_ssid[0] = 0;
    g_state = WIFI_STATE_NO_HW;
    str_copy(g_status, "No WiFi adapter", sizeof(g_status));
}

int wifi_present(void) { return g_ops != 0; }

const char *wifi_adapter_name(void) { return g_ops && g_ops->name ? g_ops->name : "none"; }

int wifi_power(int on) {
    if (!g_ops) {
        g_state = WIFI_STATE_NO_HW;
        str_copy(g_status, "No WiFi adapter", sizeof(g_status));
        return -1;
    }
    if (!on) {
        if (g_connected && g_ops->disconnect) (void)g_ops->disconnect(g_ops->ctx);
        if (g_ops->power) (void)g_ops->power(g_ops->ctx, 0);
        g_powered = 0;
        g_connected = 0;
        g_scan_count = 0;
        g_ssid[0] = 0;
        g_state = WIFI_STATE_OFF;
        str_copy(g_status, "WiFi off", sizeof(g_status));
        return 0;
    }
    if (g_ops->power && g_ops->power(g_ops->ctx, 1) != 0) {
        set_error("Adapter refused power on");
        return -1;
    }
    g_powered = 1;
    g_state = WIFI_STATE_IDLE;
    str_copy(g_status, "Idle", sizeof(g_status));
    return 0;
}

int wifi_powered(void) { return g_powered; }

int wifi_scan_start(void) {
    if (!g_ops) {
        g_state = WIFI_STATE_NO_HW;
        str_copy(g_status, "No WiFi adapter", sizeof(g_status));
        return -1;
    }
    if (!g_powered) {
        set_error("Adapter is off");
        return -1;
    }
    g_scan_count = 0;
    g_state = WIFI_STATE_SCANNING;
    str_copy(g_status, "Scanning", sizeof(g_status));
    if (g_ops->scan(g_ops->ctx) != 0) {
        set_error("Scan rejected by adapter");
        return -1;
    }
    return 0;
}

int wifi_scan_stop(void) {
    if (g_state == WIFI_STATE_SCANNING) {
        g_state = g_connected ? WIFI_STATE_CONNECTED : (g_powered ? WIFI_STATE_IDLE : WIFI_STATE_OFF);
        str_copy(g_status, g_connected ? "Connected" : (g_powered ? "Idle" : "WiFi off"), sizeof(g_status));
    }
    return 0;
}

int wifi_scan_count(void) { return g_scan_count; }

const struct wifi_bss *wifi_scan_at(int idx) {
    if (idx < 0 || idx >= g_scan_count) return 0;
    return &g_scan[idx];
}

static int add_or_update_bss(const struct wifi_bss *bss) {
    int weakest = 0;
    for (int i = 0; i < g_scan_count; i++) {
        if (bssid_same(g_scan[i].bssid, bss->bssid)) {
            int8_t rssi = g_scan[i].rssi;
            g_scan[i] = *bss;
            /* Keep the strongest reading seen in this scan. */
            if (rssi != WIFI_RSSI_UNKNOWN && (bss->rssi == WIFI_RSSI_UNKNOWN || rssi > bss->rssi))
                g_scan[i].rssi = rssi;
            return 0;
        }
        if (g_scan[i].rssi < g_scan[weakest].rssi) weakest = i;
    }
    if (g_scan_count < WIFI_MAX_BSS) {
        g_scan[g_scan_count++] = *bss;
        return 0;
    }
    if (bss->rssi > g_scan[weakest].rssi) {
        g_scan[weakest] = *bss;
        return 0;
    }
    return -1; /* table is full of stronger networks */
}

int wifi_rx_beacon(const uint8_t *frame, int len, int8_t rssi) {
    struct wifi_bss bss;
    if (!g_powered) return -1;
    if (wifi_parse_beacon(frame, len, rssi, &bss) != 0) return -1;
    return add_or_update_bss(&bss);
}

int wifi_connect(int idx, const char *passphrase) {
    const struct wifi_bss *bss;
    uint8_t pmk[WIFI_PMK_LEN];

    if (!g_ops) {
        g_state = WIFI_STATE_NO_HW;
        str_copy(g_status, "No WiFi adapter", sizeof(g_status));
        return -1;
    }
    if (!g_powered) {
        set_error("Adapter is off");
        return -1;
    }
    bss = wifi_scan_at(idx);
    if (!bss) {
        set_error("No such scan result");
        return -1;
    }
    for (int i = 0; i < WIFI_PMK_LEN; i++) pmk[i] = 0;

    if (bss->security & WIFI_SEC_ENTERPRISE) {
        set_error("WPA2-Enterprise is not implemented");
        return -1;
    }
    if (bss->security & WIFI_SEC_WEP) {
        set_error("WEP is not supported");
        return -1;
    }
    if (bss->security & (WIFI_SEC_WPA_PSK | WIFI_SEC_WPA2_PSK)) {
        int plen = str_len(passphrase);
        if (plen < 8 || plen > 63) {
            set_error("Passphrase must be 8-63 characters");
            return -1;
        }
        if (wifi_wpa2_pmk(passphrase, (const uint8_t *)bss->ssid, bss->ssid_len, pmk) != 0) {
            set_error("PSK derivation failed");
            return -1;
        }
    }

    if (g_ops->connect(g_ops->ctx, bss->bssid, bss->ssid, pmk, bss->security) != 0) {
        set_error("Adapter rejected the connection");
        return -1;
    }
    str_copy(g_ssid, bss->ssid, sizeof(g_ssid));
    g_state = WIFI_STATE_JOINING;
    str_copy(g_status, "Joining", sizeof(g_status));
    return 0;
}

int wifi_disconnect(void) {
    if (!g_ops) {
        g_state = WIFI_STATE_NO_HW;
        str_copy(g_status, "No WiFi adapter", sizeof(g_status));
        return -1;
    }
    if (g_connected && g_ops->disconnect) (void)g_ops->disconnect(g_ops->ctx);
    g_connected = 0;
    g_ssid[0] = 0;
    g_state = g_powered ? WIFI_STATE_IDLE : WIFI_STATE_OFF;
    str_copy(g_status, g_powered ? "Disconnected" : "WiFi off", sizeof(g_status));
    return 0;
}

void wifi_driver_report_link(int connected) {
    if (!g_ops) return;
    g_connected = connected ? 1 : 0;
    if (connected) {
        g_state = WIFI_STATE_CONNECTED;
        str_copy(g_status, g_ssid[0] ? g_ssid : "Connected", sizeof(g_status));
    } else {
        g_state = g_powered ? WIFI_STATE_IDLE : WIFI_STATE_OFF;
        str_copy(g_status, g_powered ? "Link lost" : "WiFi off", sizeof(g_status));
    }
}

int wifi_connected(void) { return g_connected; }

int wifi_signal(void) {
    if (!g_ops || !g_connected || !g_ops->signal) return -1;
    return g_ops->signal(g_ops->ctx);
}

const char *wifi_ssid(void) { return g_ssid; }

int wifi_state(void) { return (int)g_state; }

const char *wifi_state_text(void) {
    switch (g_state) {
        case WIFI_STATE_OFF:       return "off";
        case WIFI_STATE_NO_HW:     return "no adapter";
        case WIFI_STATE_IDLE:      return "idle";
        case WIFI_STATE_SCANNING:  return "scanning";
        case WIFI_STATE_JOINING:   return "joining";
        case WIFI_STATE_CONNECTED: return "connected";
        case WIFI_STATE_ERROR:     return "error";
        default:                   return "unknown";
    }
}

const char *wifi_last_error(void) { return g_status; }
