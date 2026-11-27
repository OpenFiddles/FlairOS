#include <stdio.h>
#include <string.h>
#include "../net/wifi/wifi_core.h"
#include "../net/wifi/wpa2.h"

#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

/* ---- helpers ---------------------------------------------------------- */

static int hex_byte(const char *s) {
    int v = 0;
    for (int i = 0; i < 2; i++) {
        char c = s[i];
        int d = (c >= '0' && c <= '9') ? c - '0' : (c | 32) - 'a' + 10;
        v = (v << 4) | d;
    }
    return v;
}

/* EAPOL-Key msg-2 style frame (IEEE 802.11-2020 12.7.2): 802.1X header,
   RSN descriptor, key info with version 2 + pairwise + MIC, replay counter 1,
   the supplicant nonce, and a zeroed MIC at offset 81. 99 octets. */
static int build_eapol(uint8_t *f, const uint8_t snonce[32]) {
    int o = 0;
    f[o++] = 2;              /* 802.1X version */
    f[o++] = 3;              /* EAPOL-Key */
    f[o++] = 0x00;           /* body length 95, big-endian */
    f[o++] = 95;
    f[o++] = 2;              /* RSN key descriptor */
    f[o++] = 0x01;           /* key info: descriptor version 2, pairwise, MIC */
    f[o++] = 0x0A;
    f[o++] = 0x00;           /* key length 16 */
    f[o++] = 16;
    for (int i = 0; i < 7; i++) f[o++] = 0; /* replay counter 1, big-endian */
    f[o++] = 1;
    for (int i = 0; i < 32; i++) f[o++] = snonce[i];
    for (int i = 0; i < 16; i++) f[o++] = 0; /* IV */
    for (int i = 0; i < 8; i++) f[o++] = 0;  /* RSC */
    for (int i = 0; i < 8; i++) f[o++] = 0;  /* key identifier */
    for (int i = 0; i < 16; i++) f[o++] = 0; /* MIC, zero for the digest */
    f[o++] = 0x00;           /* key data length */
    f[o++] = 0x00;
    return o;
}

static int bytes_eq_hex(const uint8_t *b, int n, const char *hex) {
    int v;
    for (int i = 0; i < n; i++) {
        v = hex_byte(hex + i * 2);
        if (b[i] != (uint8_t)v) return 0;
    }
    return 1;
}

static void put(uint8_t *d, int *o, const uint8_t *s, int n) {
    for (int i = 0; i < n; i++) d[(*o)++] = s[i];
}

/* Build a complete beacon frame: 24-byte management header, 12-byte fixed
   parameters, then the SSID, DS Parameter Set and any extra elements.
   Layout from IEEE 802.11-2020 9.3.3.2. */
static int build_beacon(uint8_t *f, const char *ssid, uint8_t channel, uint16_t capability,
                        const uint8_t *extra, int extra_len, uint8_t tag) {
    uint8_t bssid[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, tag };
    int o = 0, slen = (int)strlen(ssid);

    f[o++] = 0x80; /* frame control: management, beacon */
    f[o++] = 0x00;
    f[o++] = 0x00; f[o++] = 0x00;
    for (int i = 0; i < 6; i++) f[o++] = 0xFF; /* DA broadcast */
    put(f, &o, bssid, 6);                    /* SA */
    put(f, &o, bssid, 6);                    /* BSSID */
    f[o++] = 0x00; f[o++] = 0x00;            /* sequence control */
    for (int i = 0; i < 8; i++) f[o++] = 0x00; /* timestamp */
    f[o++] = 0x64; f[o++] = 0x00;            /* beacon interval 100 */
    f[o++] = (uint8_t)(capability & 0xFF);
    f[o++] = (uint8_t)(capability >> 8);

    f[o++] = 0; /* SSID */
    f[o++] = (uint8_t)slen;
    for (int i = 0; i < slen; i++) f[o++] = (uint8_t)ssid[i];

    if (channel) {
        f[o++] = 3; /* DS Parameter Set */
        f[o++] = 1;
        f[o++] = channel;
    }
    if (extra_len > 0) put(f, &o, extra, extra_len);
    return o;
}

static const uint8_t rsn_psk_ccmp[] = {
    48, 18, 1, 0, 0x00, 0x0F, 0xAC, 4, 1, 0, 0x00, 0x0F, 0xAC, 4, 1, 0, 0x00, 0x0F, 0xAC, 2
};
static const uint8_t rsn_enterprise_ccmp[] = {
    48, 18, 1, 0, 0x00, 0x0F, 0xAC, 4, 1, 0, 0x00, 0x0F, 0xAC, 4, 1, 0, 0x00, 0x0F, 0xAC, 1
};
static const uint8_t wpa_psk_tkip[] = {
    221, 22, 0x00, 0x50, 0xF2, 0x01, 1, 0, 0x00, 0x50, 0xF2, 2,
    1, 0, 0x00, 0x50, 0xF2, 2, 1, 0, 0x00, 0x50, 0xF2, 2
};

/* ---- test-only WiFi adapter ------------------------------------------ */

static int mock_power_calls = 0;
static int mock_scan_calls = 0;
static int mock_disconnect_calls = 0;
static uint8_t mock_pmk[32];
static uint8_t mock_bssid[6];
static char mock_ssid[33];
static uint32_t mock_security = 0;

static int mock_power(void *ctx, int on) {
    (void)ctx;
    (void)on;
    mock_power_calls++;
    return 0;
}

static int mock_scan(void *ctx) {
    (void)ctx;
    mock_scan_calls++;
    return 0;
}

static int mock_connect(void *ctx, const uint8_t bssid[6], const char *ssid,
                        const uint8_t pmk[32], uint32_t security) {
    (void)ctx;
    for (int i = 0; i < 6; i++) mock_bssid[i] = bssid[i];
    for (int i = 0; i < 32; i++) mock_pmk[i] = pmk[i];
    snprintf(mock_ssid, sizeof(mock_ssid), "%s", ssid);
    mock_security = security;
    return 0;
}

static int mock_disconnect(void *ctx) {
    (void)ctx;
    mock_disconnect_calls++;
    return 0;
}

static int mock_signal(void *ctx) {
    (void)ctx;
    return 72;
}

static const struct wifi_drv_ops mock_ops = {
    "test adapter", mock_power, mock_scan, mock_connect, mock_disconnect, mock_signal,
    0, 0, /* no raw-frame join: this adapter runs the handshake in firmware */
    0
};

/* ---- tests ------------------------------------------------------------ */

int main(void) {
    uint8_t h20[20], pmk[32], frame[256], eapol[128], mic[16], mic2[16];
    struct wifi_bss bss;
    int len;

    /* 1. Published primitive vectors: RFC 3174, RFC 2202, RFC 6070. */
    wifi_sha1((const uint8_t *)"abc", 3, h20);
    CHECK(bytes_eq_hex(h20, 20, "a9993e364706816aba3e25717850c26c9cd0d89d"));
    wifi_hmac_sha1((const uint8_t *)"Jefe", 4, (const uint8_t *)"what do ya want for nothing?", 28, h20);
    CHECK(bytes_eq_hex(h20, 20, "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79"));
    CHECK(wifi_pbkdf2_sha1((const uint8_t *)"password", 8, (const uint8_t *)"salt", 4, 1, h20, 20) == 0);
    CHECK(bytes_eq_hex(h20, 20, "0c60c80f961f0e71f3a9b524af6012062fe037a6"));
    CHECK(wifi_pbkdf2_sha1((const uint8_t *)"password", 8, (const uint8_t *)"salt", 4, 2, h20, 20) == 0);
    CHECK(bytes_eq_hex(h20, 20, "ea6c014dc72d6f8ccd1ed92ace1d41f0d8de8957"));
    CHECK(wifi_pbkdf2_sha1((const uint8_t *)"password", 8, (const uint8_t *)"salt", 4, 4096, h20, 20) == 0);
    CHECK(bytes_eq_hex(h20, 20, "4b007901b765489abead49d926f721d065a429c1"));

    /* 2. WPA2-PSK: PMK = PBKDF2(passphrase, SSID, 4096, 256).
       "password" over SSID "IEEE" is the published 802.11i test value. */
    CHECK(wifi_wpa2_pmk("password", (const uint8_t *)"IEEE", 4, pmk) == 0);
    CHECK(bytes_eq_hex(pmk, 32, "f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e"));
    CHECK(wifi_wpa2_pmk("short", (const uint8_t *)"IEEE", 4, pmk) != 0);

    /* 3. PTK derivation. AP 00:11:22:33:44:55, STA 66:77:88:99:AA:BB,
       ANonce 00..1f, SNonce 20..3f. The expected KCK/KEK/TK were computed
       with Python's hmac/hashlib, an implementation independent of this one. */
    {
        uint8_t ap[6] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 };
        uint8_t sta[6] = { 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB };
        uint8_t anonce[32], snonce[32];
        struct wifi_ptk ptk;
        for (int i = 0; i < 32; i++) { anonce[i] = (uint8_t)i; snonce[i] = (uint8_t)(0x20 + i); }
        CHECK(wifi_wpa2_ptk(pmk, ap, sta, anonce, snonce, &ptk) == 0);
        CHECK(bytes_eq_hex(ptk.kck, 16, "85c98eca56145629359ac8830bb66a59"));
        CHECK(bytes_eq_hex(ptk.kek, 16, "c5562d473fddcb4eee9ce4de54e1cb1a"));
        CHECK(bytes_eq_hex(ptk.tk, 16, "12cdd4448325c84079abcd76b1b89f8f"));

        /* 4. EAPOL-Key MIC over a 99-byte msg-2 style frame with a zeroed MIC.
           Offset 81 holds the MIC; the expected value again comes from Python. */
        {
            uint8_t snonce[32];
            for (int i = 0; i < 32; i++) snonce[i] = (uint8_t)(0x20 + i);
            len = build_eapol(eapol, snonce);
        }
        CHECK(len == 99);
        wifi_wpa2_mic(ptk.kck, eapol, len, mic);
        CHECK(bytes_eq_hex(mic, 16, "d733a6793e13da3819878dac0481caf6"));
        CHECK(wifi_wpa2_mic_check(ptk.kck, eapol, len, mic) == 1);
        mic[0] ^= 0x01;
        CHECK(wifi_wpa2_mic_check(ptk.kck, eapol, len, mic) == 0);

        /* The same MIC, carried inside the frame at offset 81. */
        wifi_wpa2_mic(ptk.kck, eapol, len, mic2);
        for (int i = 0; i < 16; i++) eapol[81 + i] = mic2[i];
        CHECK(wifi_wpa2_eapol_mic_ok(ptk.kck, eapol, len) == 1);
        eapol[98] ^= 0x01;
        CHECK(wifi_wpa2_eapol_mic_ok(ptk.kck, eapol, len) == 0);
    }

    /* 5. Beacon parsing: security comes from the RSN/WPA elements. */
    len = build_beacon(frame, "IEEE", 6, 0x0011, rsn_psk_ccmp, (int)sizeof(rsn_psk_ccmp), 1);
    CHECK(wifi_parse_beacon(frame, len, -52, &bss) == 0);
    CHECK(strcmp(bss.ssid, "IEEE") == 0 && bss.ssid_len == 4);
    CHECK(bss.channel == 6);
    CHECK(bss.security == (WIFI_SEC_WPA2_PSK | WIFI_SEC_CCMP));
    CHECK(strcmp(wifi_security_text(bss.security), "WPA2-PSK") == 0);
    CHECK(bss.rssi == -52);
    CHECK(bss.bssid[0] == 0x02 && bss.bssid[5] == 1);

    len = build_beacon(frame, "OldRouter", 3, 0x0011, wpa_psk_tkip, (int)sizeof(wpa_psk_tkip), 2);
    CHECK(wifi_parse_beacon(frame, len, -70, &bss) == 0);
    CHECK(bss.security == (WIFI_SEC_WPA_PSK | WIFI_SEC_TKIP));
    CHECK(strcmp(wifi_security_text(bss.security), "WPA-PSK") == 0);

    len = build_beacon(frame, "Corp", 1, 0x0011, rsn_enterprise_ccmp, (int)sizeof(rsn_enterprise_ccmp), 3);
    CHECK(wifi_parse_beacon(frame, len, -60, &bss) == 0);
    CHECK(bss.security == (WIFI_SEC_ENTERPRISE | WIFI_SEC_CCMP));
    CHECK(strcmp(wifi_security_text(bss.security), "WPA2-Enterprise") == 0);

    len = build_beacon(frame, "Cafe Guest", 11, 0x0001, 0, 0, 4);
    CHECK(wifi_parse_beacon(frame, len, -44, &bss) == 0);
    CHECK(bss.security == WIFI_SEC_OPEN);
    CHECK(bss.channel == 11);

    /* Privacy bit set with no RSN/WPA element is WEP. */
    len = build_beacon(frame, "Legacy", 9, 0x0011, 0, 0, 5);
    CHECK(wifi_parse_beacon(frame, len, -80, &bss) == 0);
    CHECK(bss.security == WIFI_SEC_WEP);

    /* Hidden SSID stays empty rather than being guessed. */
    len = build_beacon(frame, "", 36, 0x0001, 0, 0, 6);
    CHECK(wifi_parse_beacon(frame, len, -66, &bss) == 0);
    CHECK(bss.ssid_len == 0 && bss.ssid[0] == 0);

    /* Not a beacon/probe response, and a truncated frame, are rejected. */
    len = build_beacon(frame, "X", 1, 0x0001, 0, 0, 7);
    frame[0] = 0x08; /* data frame */
    CHECK(wifi_parse_beacon(frame, len, -50, &bss) != 0);
    frame[0] = 0x80;
    CHECK(wifi_parse_beacon(frame, 30, -50, &bss) != 0);
    CHECK(wifi_parse_beacon(frame, 0, -50, &bss) != 0);

    /* 6. No adapter: the core says so and refuses to work. */
    wifi_unregister_adapter();
    CHECK(wifi_present() == 0);
    CHECK(wifi_scan_start() != 0);
    CHECK(wifi_state() == WIFI_STATE_NO_HW);
    CHECK(strcmp(wifi_state_text(), "no adapter") == 0);
    CHECK(wifi_connect(0, "password") != 0);
    CHECK(strcmp(wifi_last_error(), "No WiFi adapter") == 0);
    CHECK(wifi_signal() == -1);

    /* 7. With an adapter: scan, classify, refuse what is not implemented. */
    CHECK(wifi_register_adapter(&mock_ops) == 0);
    CHECK(wifi_present() == 1);
    CHECK(strcmp(wifi_adapter_name(), "test adapter") == 0);
    CHECK(wifi_power(1) == 0);
    CHECK(mock_power_calls == 1);
    CHECK(wifi_scan_start() == 0);
    CHECK(mock_scan_calls == 1);
    CHECK(wifi_state() == WIFI_STATE_SCANNING);

    len = build_beacon(frame, "Cafe Guest", 11, 0x0001, 0, 0, 4);
    CHECK(wifi_rx_beacon(frame, len, -44) == 0);
    len = build_beacon(frame, "IEEE", 6, 0x0011, rsn_psk_ccmp, (int)sizeof(rsn_psk_ccmp), 1);
    CHECK(wifi_rx_beacon(frame, len, -52) == 0);
    len = build_beacon(frame, "Corp", 1, 0x0011, rsn_enterprise_ccmp, (int)sizeof(rsn_enterprise_ccmp), 3);
    CHECK(wifi_rx_beacon(frame, len, -60) == 0);
    len = build_beacon(frame, "Legacy", 9, 0x0011, 0, 0, 5);
    CHECK(wifi_rx_beacon(frame, len, -80) == 0);
    CHECK(wifi_scan_count() == 4);
    CHECK(wifi_rx_beacon(frame, 4, -80) != 0); /* garbage is not a BSS */

    /* A repeated BSSID updates the entry instead of adding one, and keeps the
       stronger reading. */
    len = build_beacon(frame, "Cafe Guest", 11, 0x0001, 0, 0, 4);
    CHECK(wifi_rx_beacon(frame, len, -30) == 0);
    CHECK(wifi_scan_count() == 4);
    CHECK(wifi_scan_at(0)->rssi == -30);

    CHECK(wifi_connect(2, "password") != 0); /* enterprise */
    CHECK(strstr(wifi_last_error(), "Enterprise") != 0);
    CHECK(wifi_connect(3, "password") != 0); /* WEP */
    CHECK(strstr(wifi_last_error(), "WEP") != 0);
    CHECK(wifi_connect(1, "short") != 0);    /* passphrase too short */
    CHECK(strstr(wifi_last_error(), "8-63") != 0);

    /* 8. WPA2-PSK connect derives the PSK, hands it to the driver with the
       network's security mask, and only reports connected when the driver
       says the link is up. */
    CHECK(wifi_connect(1, "password") == 0);
    CHECK(wifi_state() == WIFI_STATE_JOINING);
    CHECK(wifi_connected() == 0);
    CHECK(strcmp(mock_ssid, "IEEE") == 0);
    CHECK(bytes_eq_hex(mock_pmk, 32, "f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e"));
    CHECK(mock_security == (WIFI_SEC_WPA2_PSK | WIFI_SEC_CCMP));
    CHECK(mock_bssid[0] == 0x02);
    CHECK(wifi_signal() == -1); /* not connected yet */

    wifi_driver_report_link(1);
    CHECK(wifi_connected() == 1);
    CHECK(wifi_state() == WIFI_STATE_CONNECTED);
    CHECK(strcmp(wifi_ssid(), "IEEE") == 0);
    CHECK(wifi_signal() == 72);

    /* A full-MAC driver has no raw-frame ops, so the core carries no data for
       it: the old behaviour is unchanged. */
    CHECK(wifi_data_ready() == 0);
    CHECK(wifi_send_data((const uint8_t[6]){ 0x02, 0, 0, 0, 0, 9 },
                         (const uint8_t *)"x", 1) != 0);

    CHECK(wifi_disconnect() == 0);
    CHECK(mock_disconnect_calls == 1);
    CHECK(wifi_connected() == 0);
    CHECK(wifi_state() == WIFI_STATE_IDLE);

    /* 9. Power off returns to OFF and empties the scan table. */
    CHECK(wifi_power(0) == 0);
    CHECK(wifi_state() == WIFI_STATE_OFF);
    CHECK(wifi_scan_count() == 0);
    CHECK(wifi_scan_start() != 0); /* adapter is off */
    CHECK(strcmp(wifi_last_error(), "Adapter is off") == 0);

    puts("WiFi 802.11 + WPA2-PSK driver core test: all checks passed");
    return 0;
}
