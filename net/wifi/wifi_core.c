/* SPDX-License-Identifier: AGPL-3.0-only */
/* FlairOS WiFi core: adapter interface, scan table, and the supplicant.
 *
 * A raw-frame driver (send_frame + install_key) lets the core run the join:
 *
 *   authenticate request  -> authenticate response (status checked)
 *   associate request     -> associate response (status, AID)
 *   4-way message 1/4     -> 2/4 (MIC over the frame)
 *   4-way message 3/4     -> verify MIC, install PTK, 4/4
 *   group-key message 1   -> verify MIC, unwrap GTK, install it, 2/2
 *
 * Every received EAPOL-Key must carry a replay counter above the last one
 * accepted, and every failure ends disconnected with the reason recorded and
 * the driver's disconnect op invoked. A retry starts from a cleared state.
 */
#include "wifi_core.h"
#include "wpa2.h"

#define HDR_LEN 24
#define SNAP_LEN 8
#define EAPOL_KEY_MIN 99
#define FRAME_MAX 256

static const struct wifi_drv_ops *g_ops = 0;
static struct wifi_bss g_scan[WIFI_MAX_BSS];
static int g_scan_count = 0;
static enum wifi_state g_state = WIFI_STATE_OFF;
static int g_powered = 0;
static int g_connected = 0;
static char g_ssid[WIFI_SSID_MAX + 1];
static char g_status[72] = "WiFi off";

/* Platform inputs for the raw-frame join. */
static uint8_t g_sta[WIFI_MAC_LEN];
static int g_sta_set = 0;
static uint8_t g_snonce[WIFI_NONCE_LEN];
static int g_snonce_set = 0;

/* Join state. Cleared completely by join_reset(), so a retry cannot inherit
   a nonce, replay counter, PTK or group key from the previous attempt. */
static uint8_t g_join_bssid[6];
static char g_join_ssid[WIFI_SSID_MAX + 1];
static uint8_t g_join_ssid_len = 0;
static uint32_t g_join_security = 0;
static uint8_t g_join_sec_ie[64];
static uint8_t g_join_sec_ie_len = 0;
static uint8_t g_pmk[WIFI_PMK_LEN];
static enum wifi_join_step g_step = WIFI_JOIN_IDLE;
static uint32_t g_now_ms = 0;
static uint32_t g_deadline_ms = 0;
static uint64_t g_replay = 0;
static uint8_t g_anonce[WIFI_NONCE_LEN];
static struct wifi_ptk g_ptk;
static uint8_t g_gtk[32];
static int g_gtk_len = 0;
static uint8_t g_gtk_key_id = 0;
static int g_assoc_aid = 0;

/* Data path. Packet numbers are tracked per key index: 0 is the pairwise key,
   1-3 are group keys, and the current group key is the one in g_gtk. */
static uint64_t g_tx_pn[4];
static uint64_t g_rx_pn[4];
static uint8_t g_tx_buf[HDR_LEN + 8 + WIFI_DATA_MAX + 8];
static uint8_t g_rx_buf[WIFI_DATA_MAX + 16];
static wifi_data_rx_fn g_data_rx = 0;
static void *g_data_rx_ctx = 0;

static int add_or_update_bss(const struct wifi_bss *bss);

static int bssid_same(const uint8_t *a, const uint8_t *b) {
    for (int i = 0; i < 6; i++) if (a[i] != b[i]) return 0;
    return 1;
}

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

static void bytes_zero(uint8_t *p, int n) {
    for (int i = 0; i < n; i++) p[i] = 0;
}

static void bytes_copy(uint8_t *d, const uint8_t *s, int n) {
    for (int i = 0; i < n; i++) d[i] = s[i];
}

static uint16_t rd_le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint16_t rd_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint64_t rd_be64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | (uint64_t)p[i];
    return v;
}

static void set_error(const char *text) {
    g_state = WIFI_STATE_ERROR;
    str_copy(g_status, text, sizeof(g_status));
}

static const char *step_name(enum wifi_join_step s) {
    switch (s) {
        case WIFI_JOIN_AUTH:  return "authenticate";
        case WIFI_JOIN_ASSOC: return "association";
        case WIFI_JOIN_MSG1:  return "4-way message 1";
        case WIFI_JOIN_MSG3:  return "4-way message 3";
        case WIFI_JOIN_GROUP: return "group key handshake";
        case WIFI_JOIN_DONE:  return "done";
        default:              return "idle";
    }
}

int wifi_join_step(void) { return (int)g_step; }
const char *wifi_join_step_name(void) { return step_name(g_step); }
int wifi_assoc_aid(void) { return g_assoc_aid; }

static int raw_join_capable(void) {
    return g_ops && g_ops->send_frame && g_ops->install_key;
}

int wifi_raw_join(void) { return raw_join_capable(); }

/* Drop every trace of a join attempt. The platform inputs (MAC, SNonce) are
   not part of the attempt and stay. */
static void join_reset(void) {
    g_step = WIFI_JOIN_IDLE;
    g_replay = 0;
    g_deadline_ms = 0;
    g_assoc_aid = 0;
    bytes_zero(g_anonce, sizeof(g_anonce));
    bytes_zero((uint8_t *)&g_ptk, (int)sizeof(g_ptk));
    bytes_zero(g_gtk, sizeof(g_gtk));
    g_gtk_len = 0;
    g_gtk_key_id = 0;
    bytes_zero((uint8_t *)g_tx_pn, (int)sizeof(g_tx_pn));
    bytes_zero((uint8_t *)g_rx_pn, (int)sizeof(g_rx_pn));
    bytes_zero(g_pmk, sizeof(g_pmk));
    bytes_zero(g_join_bssid, sizeof(g_join_bssid));
    bytes_zero(g_join_sec_ie, sizeof(g_join_sec_ie));
    g_join_sec_ie_len = 0;
    g_join_ssid[0] = 0;
    g_join_ssid_len = 0;
    g_join_security = 0;
}

/* End a join attempt: reason recorded, link down, driver told to disconnect. */
static void join_fail(const char *reason) {
    str_copy(g_status, reason, sizeof(g_status));
    g_connected = 0;
    g_ssid[0] = 0;
    g_state = WIFI_STATE_ERROR;
    join_reset();
    if (g_ops && g_ops->disconnect) (void)g_ops->disconnect(g_ops->ctx);
}

static void join_fail_code(const char *prefix, unsigned code) {
    char buf[72];
    char digits[12];
    int o = 0, d = 0;
    while (prefix[o] && o < 52) { buf[o] = prefix[o]; o++; }
    if (o < 54) { buf[o++] = ' '; buf[o++] = '('; }
    if (code == 0) digits[d++] = '0';
    while (code > 0 && d < (int)sizeof(digits)) {
        digits[d++] = (char)('0' + (code % 10));
        code /= 10;
    }
    while (d > 0 && o < 68) buf[o++] = digits[--d];
    if (o < 70) buf[o++] = ')';
    buf[o] = 0;
    join_fail(buf);
}

void wifi_tick(uint32_t now_ms) {
    char buf[72];
    int o = 0;
    g_now_ms = now_ms;
    if (g_step == WIFI_JOIN_IDLE || g_step == WIFI_JOIN_DONE) return;
    if ((int32_t)(now_ms - g_deadline_ms) < 0) return;
    str_copy(buf, "Join timeout at ", sizeof(buf));
    while (buf[o]) o++;
    str_copy(buf + o, step_name(g_step), (int)sizeof(buf) - o);
    join_fail(buf);
}

static void put_header(uint8_t *f, uint8_t fc0, uint8_t fc1, const uint8_t *da,
                       const uint8_t *sa, const uint8_t *bssid) {
    f[0] = fc0;
    f[1] = fc1;
    f[2] = 0;
    f[3] = 0;
    bytes_copy(f + 4, da, 6);
    bytes_copy(f + 10, sa, 6);
    bytes_copy(f + 16, bssid, 6);
    f[22] = 0;
    f[23] = 0;
}

static int send_frame(const uint8_t *f, int len) {
    if (!g_ops || !g_ops->send_frame) return -1;
    return g_ops->send_frame(g_ops->ctx, f, len);
}

static int send_auth(void) {
    uint8_t f[HDR_LEN + 6];
    put_header(f, 0xB0, 0x00, g_join_bssid, g_sta, g_join_bssid);
    f[24] = 0; f[25] = 0; /* Open System */
    f[26] = 1; f[27] = 0; /* transaction sequence 1 */
    f[28] = 0; f[29] = 0; /* status */
    return send_frame(f, 30);
}

static int send_assoc(void) {
    static const uint8_t rates[10] = { 1, 8, 0x82, 0x84, 0x8B, 0x96, 0x0C, 0x12, 0x18, 0x24 };
    uint8_t f[FRAME_MAX];
    int o = HDR_LEN;
    uint16_t cap = (uint16_t)(0x0411u | ((g_join_security & (WIFI_SEC_WPA_PSK | WIFI_SEC_WPA2_PSK)) ? 0x0010u : 0u));

    put_header(f, 0x00, 0x00, g_join_bssid, g_sta, g_join_bssid);
    f[o++] = (uint8_t)(cap & 0xFFu);
    f[o++] = (uint8_t)(cap >> 8);
    f[o++] = 0x0A; /* listen interval 10 */
    f[o++] = 0x00;
    f[o++] = 0; /* SSID element */
    f[o++] = g_join_ssid_len;
    for (int i = 0; i < (int)g_join_ssid_len; i++) f[o++] = (uint8_t)g_join_ssid[i];
    bytes_copy(f + o, rates, (int)sizeof(rates));
    o += (int)sizeof(rates);
    if (g_join_sec_ie_len > 0 && (int)g_join_sec_ie_len <= FRAME_MAX - o) {
        bytes_copy(f + o, g_join_sec_ie, g_join_sec_ie_len);
        o += g_join_sec_ie_len;
    }
    return send_frame(f, o);
}

/* Build and send one EAPOL-Key frame wrapped in a ToDS data frame. The MIC is
   computed over the frame with the MIC field zeroed. */
static int send_eapol(uint16_t key_info, uint64_t replay, const uint8_t *nonce,
                      const uint8_t *key_data, int kd_len) {
    static const uint8_t snap[SNAP_LEN] = { 0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E };
    uint8_t f[FRAME_MAX];
    uint8_t mic[WIFI_EAPOL_MIC_LEN];
    uint8_t *e = f + HDR_LEN + SNAP_LEN;
    int body = 95 + kd_len;
    int total = HDR_LEN + SNAP_LEN + 4 + body;

    if (total > FRAME_MAX) return -1;
    put_header(f, 0x08, 0x01, g_join_bssid, g_sta, g_join_bssid);
    bytes_copy(f + HDR_LEN, snap, SNAP_LEN);

    e[0] = 2; /* 802.1X version */
    e[1] = 3; /* EAPOL-Key */
    e[2] = (uint8_t)((body >> 8) & 0xFF);
    e[3] = (uint8_t)(body & 0xFF);
    e[4] = 2; /* RSN key descriptor */
    e[5] = (uint8_t)((key_info >> 8) & 0xFF);
    e[6] = (uint8_t)(key_info & 0xFF);
    e[7] = 0; /* key length 16 */
    e[8] = 16;
    for (int i = 0; i < 8; i++) e[9 + i] = (uint8_t)(replay >> (8 * (7 - i)));
    for (int i = 0; i < 32; i++) e[17 + i] = nonce ? nonce[i] : 0;
    bytes_zero(e + 49, 16);  /* IV */
    bytes_zero(e + 65, 8);   /* RSC */
    bytes_zero(e + 73, 8);   /* key identifier */
    bytes_zero(e + 81, 16);  /* MIC */
    e[97] = (uint8_t)((kd_len >> 8) & 0xFF);
    e[98] = (uint8_t)(kd_len & 0xFF);
    for (int i = 0; i < kd_len; i++) e[99 + i] = key_data[i];

    wifi_wpa2_mic(g_ptk.kck, e, 99 + kd_len, mic);
    bytes_copy(e + 81, mic, WIFI_EAPOL_MIC_LEN);
    return send_frame(f, total);
}

static void link_up(void) {
    g_step = WIFI_JOIN_DONE;
    g_connected = 1;
    g_state = WIFI_STATE_CONNECTED;
    str_copy(g_ssid, g_join_ssid, sizeof(g_ssid));
    str_copy(g_status, g_ssid[0] ? g_ssid : "Connected", sizeof(g_status));
}

/* Unwrap a GTK from EAPOL key data. With the encrypted bit set the whole key
   data is AES-key-wrapped under the KEK and has to be opened first; inside it
   sits an element DD <len> 00:0F:AC 01 <key id> <wrapped GTK>. */
static int parse_gtk(const uint8_t *kd, int len, int encrypted, uint8_t *out, int out_cap,
                     int *out_len, uint8_t *key_id) {
    uint8_t plain[80];
    int i = 0;

    if (encrypted) {
        int opened = wifi_wpa2_unwrap(g_ptk.kek, kd, len, plain, (int)sizeof(plain));
        if (opened <= 0) return -1;
        kd = plain;
        len = opened;
    }
    while (i + 2 <= len) {
        uint8_t id = kd[i];
        uint8_t elen = kd[i + 1];
        if (i + 2 + (int)elen > len) break;
        if (id == 0xDD && elen > 5 + 8) {
            const uint8_t *p = kd + i + 2;
            if (p[0] == 0x00 && p[1] == 0x0F && p[2] == 0xAC && p[3] == 0x01) {
                int wlen = (int)elen - 5;
                int opened = wifi_wpa2_unwrap(g_ptk.kek, p + 5, wlen, out, out_cap);
                if (opened <= 0) return -1;
                if (key_id) *key_id = (uint8_t)(p[4] & 3u);
                if (out_len) *out_len = opened;
                return 0;
            }
        }
        i += 2 + (int)elen;
    }
    return -1;
}

/* ---- data path --------------------------------------------------------- */

void wifi_set_data_rx(wifi_data_rx_fn fn, void *ctx) {
    g_data_rx = fn;
    g_data_rx_ctx = ctx;
}

int wifi_data_ready(void) {
    return g_connected && g_step == WIFI_JOIN_DONE && raw_join_capable();
}

int wifi_group_key_id(void) { return g_gtk_key_id; }

uint64_t wifi_tx_packet_number(int key_id) {
    if (key_id < 0 || key_id > 3) return 0;
    return g_tx_pn[key_id];
}

uint64_t wifi_rx_packet_number(int key_id) {
    if (key_id < 0 || key_id > 3) return 0;
    return g_rx_pn[key_id];
}

int wifi_send_data(const uint8_t dst[WIFI_MAC_LEN], const uint8_t *payload, int len) {
    const uint8_t *key;
    uint64_t pn;
    int key_id;

    if (!dst || len < 0 || len > WIFI_DATA_MAX || (len > 0 && !payload)) return -1;
    if (!wifi_data_ready()) return -1;
    if (dst[0] & 0x01u) {
        if (g_gtk_key_id == 0 || g_gtk_len < 16) return -1;
        key = g_gtk;
        key_id = g_gtk_key_id;
    } else {
        key = g_ptk.tk;
        key_id = 0;
    }
    pn = g_tx_pn[key_id] + 1;
    put_header(g_tx_buf, 0x08, 0x41, g_join_bssid, g_sta, dst); /* ToDS + protected */
    if (wifi_ccmp_encrypt(key, g_tx_buf, HDR_LEN, pn, key_id, payload, len,
                          g_tx_buf + HDR_LEN) != 0)
        return -1;
    /* Commit the packet number before transmitting: a failed send may still
       have reached the air, and a PN must never be reused under one key. */
    g_tx_pn[key_id] = pn;
    if (send_frame(g_tx_buf, HDR_LEN + 8 + len + 8) != 0) return -1;
    return 0;
}

/* A protected data frame: pick the key from the CCMP header's key index, check
   the packet number for that key, decrypt and verify, then deliver. */
static int handle_data(const uint8_t *f, int len) {
    int hdr_len, key_id, plen;
    const uint8_t *ccmp;
    const uint8_t *key;
    uint64_t pn;

    if (!wifi_data_ready()) return 0;
    if (!(f[1] & 0x40u)) return 0; /* not protected */
    hdr_len = ((f[0] >> 4) & 0x08u) ? HDR_LEN + 2 : HDR_LEN; /* QoS adds 2 */
    if (len < hdr_len + 16) return 0;
    ccmp = f + hdr_len;
    if (!(ccmp[0] & 0x20u)) return 0; /* no extended IV: not CCMP */
    key_id = (ccmp[0] >> 6) & 3;
    if (key_id == 0) {
        key = g_ptk.tk;
    } else if (key_id == g_gtk_key_id && g_gtk_len >= 16) {
        key = g_gtk;
    } else {
        return 0; /* a key index we no longer hold */
    }
    plen = len - hdr_len - 16;
    if (plen > WIFI_DATA_MAX || plen > (int)sizeof(g_rx_buf)) return 0;
    pn = wifi_ccmp_pn(ccmp);
    if (pn <= g_rx_pn[key_id]) {
        join_fail("Data replay counter regression");
        return -1;
    }
    bytes_copy(g_rx_buf, ccmp + 8, plen);
    if (wifi_ccmp_decrypt(key, f, hdr_len, ccmp, g_rx_buf, plen, ccmp + 8 + plen) != 0) {
        join_fail("MIC mismatch on a data frame");
        return -1;
    }
    g_rx_pn[key_id] = pn;
    if (g_data_rx) {
        const uint8_t *src = (f[1] & 0x02u) ? f + 16 : f + 10; /* FromDS: A3 */
        g_data_rx(src, g_rx_buf, plen, g_data_rx_ctx);
    }
    return 0;
}

/* ---- received frames --------------------------------------------------- */

static int handle_auth(const uint8_t *f, int len) {
    unsigned status;
    if (len < 30) return -1;
    if (g_step != WIFI_JOIN_AUTH) return 0;
    status = rd_le16(f + 28);
    if (status != 0) {
        join_fail_code("Authentication rejected", status);
        return -1;
    }
    g_step = WIFI_JOIN_ASSOC;
    g_deadline_ms = g_now_ms + WIFI_JOIN_TIMEOUT_MS;
    if (send_assoc() != 0) {
        join_fail("Driver refused the association frame");
        return -1;
    }
    return 0;
}

static int handle_assoc_resp(const uint8_t *f, int len) {
    unsigned status;
    if (len < 30) return -1;
    if (g_step != WIFI_JOIN_ASSOC) return 0;
    status = rd_le16(f + 26);
    if (status != 0) {
        join_fail_code("Association rejected", status);
        return -1;
    }
    g_assoc_aid = (int)(rd_le16(f + 28) & 0x3FFFu);
    if (g_join_security == WIFI_SEC_OPEN) {
        link_up();
        return 0;
    }
    g_step = WIFI_JOIN_MSG1;
    g_deadline_ms = g_now_ms + WIFI_JOIN_TIMEOUT_MS;
    return 0;
}

static int handle_deauth(const uint8_t *f, int len, int disassoc) {
    unsigned reason = len >= 26 ? rd_le16(f + 24) : 0;
    char buf[72];
    int o;
    if (g_step == WIFI_JOIN_IDLE && !g_connected) return 0;
    if (reason == 14) {
        join_fail("MIC failure reported by the authenticator");
        return 0;
    }
    str_copy(buf, disassoc ? "Disassociated" : "Deauthenticated", sizeof(buf));
    o = str_len(buf);
    if (o < 56) { buf[o++] = ' '; buf[o++] = '('; }
    {
        char digits[12];
        int d = 0;
        if (reason == 0) digits[d++] = '0';
        while (reason > 0 && d < (int)sizeof(digits)) {
            digits[d++] = (char)('0' + (reason % 10));
            reason /= 10;
        }
        while (d > 0 && o < 68) buf[o++] = digits[--d];
    }
    if (o < 70) buf[o++] = ')';
    buf[o] = 0;
    join_fail(buf);
    return 0;
}

static int handle_eapol(const uint8_t *e, int elen) {
    uint16_t key_info;
    uint64_t replay;
    int kd_len;

    if (elen < EAPOL_KEY_MIN) {
        if (g_step != WIFI_JOIN_IDLE) join_fail("Truncated EAPOL-Key frame");
        return -1;
    }
    if (e[4] != 2) return 0; /* only the RSN descriptor is handled */
    key_info = rd_be16(e + 5);
    replay = rd_be64(e + 9);
    kd_len = (int)rd_be16(e + 97);
    if (99 + kd_len > elen) {
        if (g_step != WIFI_JOIN_IDLE) join_fail("Malformed EAPOL-Key frame");
        return -1;
    }

    if (g_step == WIFI_JOIN_MSG1) {
        if (!(key_info & 0x0008u) || !(key_info & 0x0080u) || (key_info & 0x0100u)) return 0;
        if (replay <= g_replay) {
            join_fail("Replay counter regression");
            return -1;
        }
        g_replay = replay;
        bytes_copy(g_anonce, e + 17, WIFI_NONCE_LEN);
        if (wifi_wpa2_ptk(g_pmk, g_join_bssid, g_sta, g_anonce, g_snonce, &g_ptk) != 0) {
            join_fail("PTK derivation failed");
            return -1;
        }
        g_step = WIFI_JOIN_MSG3;
        g_deadline_ms = g_now_ms + WIFI_JOIN_TIMEOUT_MS;
        if (send_eapol(0x010Au, replay, g_snonce, 0, 0) != 0) {
            join_fail("Driver refused the 4-way message 2");
            return -1;
        }
        return 0;
    }

    if (g_step == WIFI_JOIN_MSG3) {
        int have_gtk;
        if (!(key_info & 0x0008u) || !(key_info & 0x0080u) ||
            !(key_info & 0x0100u) || !(key_info & 0x0040u)) return 0;
        if (replay <= g_replay) {
            join_fail("Replay counter regression");
            return -1;
        }
        if (!wifi_wpa2_eapol_mic_ok(g_ptk.kck, e, elen)) {
            join_fail("MIC mismatch on 4-way message 3");
            return -1;
        }
        g_replay = replay;
        have_gtk = 0;
        if ((key_info & 0x1000u) && kd_len > 0) {
            uint8_t id = 0;
            if (parse_gtk(e + 99, kd_len, 1, g_gtk, (int)sizeof(g_gtk), &g_gtk_len,
                          &id) == 0) {
                g_gtk_key_id = (uint8_t)(id != 0 ? id : ((key_info >> 4) & 3u));
                have_gtk = g_gtk_key_id != 0;
            }
        }
        if (g_ops->install_key(g_ops->ctx, 1, g_ptk.tk, 16) != 0) {
            join_fail("Driver refused the pairwise key");
            return -1;
        }
        g_step = WIFI_JOIN_GROUP;
        g_deadline_ms = g_now_ms + WIFI_JOIN_TIMEOUT_MS;
        if (send_eapol(0x030Au, replay, 0, 0, 0) != 0) {
            join_fail("Driver refused the 4-way message 4");
            return -1;
        }
        if (have_gtk) {
            if (g_ops->install_key(g_ops->ctx, 0, g_gtk, g_gtk_len) != 0) {
                join_fail("Driver refused the group key");
                return -1;
            }
            link_up();
        }
        return 0;
    }

    if (g_step == WIFI_JOIN_GROUP) {
        uint16_t group_info;
        if ((key_info & 0x0008u) || !(key_info & 0x0100u) || !(key_info & 0x0200u)) return 0;
        if (replay <= g_replay) {
            join_fail("Replay counter regression");
            return -1;
        }
        if (!wifi_wpa2_eapol_mic_ok(g_ptk.kck, e, elen)) {
            join_fail("MIC mismatch on the group message");
            return -1;
        }
        g_replay = replay;
        {
            uint8_t id = 0;
            if (!(key_info & 0x1000u) || kd_len <= 0 ||
                parse_gtk(e + 99, kd_len, 1, g_gtk, (int)sizeof(g_gtk), &g_gtk_len,
                          &id) != 0) {
                join_fail("No group key in the group handshake");
                return -1;
            }
            g_gtk_key_id = (uint8_t)(id != 0 ? id : ((key_info >> 4) & 3u));
            if (g_gtk_key_id == 0) {
                join_fail("No group key index in the group handshake");
                return -1;
            }
        }
        group_info = (uint16_t)(0x0302u | (key_info & 0x0030u));
        if (send_eapol(group_info, replay, 0, 0, 0) != 0) {
            join_fail("Driver refused the group response");
            return -1;
        }
        if (g_ops->install_key(g_ops->ctx, 0, g_gtk, g_gtk_len) != 0) {
            join_fail("Driver refused the group key");
            return -1;
        }
        link_up();
        return 0;
    }

    /* GTK rekey: group-key message 1 while the link is up. The pairwise key and
       the link stay; only the group key and its key index change. */
    if (g_step == WIFI_JOIN_DONE && g_connected) {
        uint8_t fresh[32];
        int fresh_len = 0;
        uint8_t id = 0;
        uint16_t group_info;

        if ((key_info & 0x0008u) || !(key_info & 0x0100u) ||
            !(key_info & 0x0200u)) return 0;
        if (replay <= g_replay) {
            join_fail("Replay counter regression");
            return -1;
        }
        if (!wifi_wpa2_eapol_mic_ok(g_ptk.kck, e, elen)) {
            join_fail("MIC mismatch on a group rekey");
            return -1;
        }
        if (!(key_info & 0x1000u) || kd_len <= 0 ||
            parse_gtk(e + 99, kd_len, 1, fresh, (int)sizeof(fresh), &fresh_len, &id) != 0) {
            join_fail("No group key in the rekey");
            return -1;
        }
        if (id == 0) id = (uint8_t)((key_info >> 4) & 3u);
        if (id == 0 || id == g_gtk_key_id) {
            join_fail("Group rekey did not change the key index");
            return -1;
        }
        g_replay = replay;
        bytes_copy(g_gtk, fresh, fresh_len);
        g_gtk_len = fresh_len;
        g_gtk_key_id = id;
        g_rx_pn[id] = 0;
        g_tx_pn[id] = 0;
        if (g_ops->install_key(g_ops->ctx, 0, g_gtk, g_gtk_len) != 0) {
            join_fail("Driver refused the group key");
            return -1;
        }
        group_info = (uint16_t)(0x0302u | (key_info & 0x0030u));
        if (send_eapol(group_info, replay, 0, 0, 0) != 0) {
            join_fail("Driver refused the group response");
            return -1;
        }
        return 0;
    }

    return 0;
}

static int eapol_from_data(const uint8_t *f, int len, const uint8_t **out, int *out_len) {
    static const uint8_t snap[SNAP_LEN] = { 0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E };
    int hdr = (f[0] & 0x80u) ? 26 : HDR_LEN;
    const uint8_t *e;
    int body;

    if (len < hdr + SNAP_LEN + 4) return -1;
    for (int i = 0; i < SNAP_LEN; i++)
        if (f[hdr + i] != snap[i]) return -1;
    e = f + hdr + SNAP_LEN;
    if (e[1] != 3) return -1;
    body = (int)rd_be16(e + 2);
    if (4 + body > len - hdr - SNAP_LEN) return -1;
    *out = e;
    *out_len = 4 + body;
    return 0;
}

int wifi_rx_frame(const uint8_t *frame, int len, int8_t rssi) {
    uint8_t fc0;
    uint8_t type, subtype;

    if (!frame || len < HDR_LEN || !g_powered) return -1;
    fc0 = frame[0];
    type = (uint8_t)((fc0 >> 2) & 0x3u);
    subtype = (uint8_t)(fc0 >> 4);

    if (type == 0) {
        if (subtype == 0x08u || subtype == 0x05u) {
            struct wifi_bss bss;
            if (wifi_parse_beacon(frame, len, rssi, &bss) != 0) return -1;
            return add_or_update_bss(&bss);
        }
        if (subtype == 0x0Bu) return handle_auth(frame, len);
        if (subtype == 0x01u) return handle_assoc_resp(frame, len);
        if (subtype == 0x0Cu) return handle_deauth(frame, len, 0);
        if (subtype == 0x0Au) return handle_deauth(frame, len, 1);
        return 0;
    }
    if (type == 2) {
        const uint8_t *e = 0;
        int elen = 0;
        if (!(frame[1] & 0x40u) && eapol_from_data(frame, len, &e, &elen) == 0)
            return handle_eapol(e, elen);
        return handle_data(frame, len);
    }
    return 0;
}

int wifi_rx_beacon(const uint8_t *frame, int len, int8_t rssi) {
    return wifi_rx_frame(frame, len, rssi);
}

/* ---- adapter and scan -------------------------------------------------- */

int wifi_register_adapter(const struct wifi_drv_ops *ops) {
    if (!ops || !ops->scan) return -1;
    if (!ops->connect && !(ops->send_frame && ops->install_key)) return -1;
    g_ops = ops;
    g_state = WIFI_STATE_OFF;
    g_powered = 0;
    g_connected = 0;
    g_scan_count = 0;
    g_ssid[0] = 0;
    join_reset();
    str_copy(g_status, "WiFi off", sizeof(g_status));
    return 0;
}

void wifi_unregister_adapter(void) {
    g_ops = 0;
    g_powered = 0;
    g_connected = 0;
    g_scan_count = 0;
    g_ssid[0] = 0;
    join_reset();
    g_state = WIFI_STATE_NO_HW;
    str_copy(g_status, "No WiFi adapter", sizeof(g_status));
}

int wifi_present(void) { return g_ops != 0; }

const char *wifi_adapter_name(void) { return g_ops && g_ops->name ? g_ops->name : "none"; }

int wifi_set_mac(const uint8_t mac[WIFI_MAC_LEN]) {
    if (!mac) return -1;
    bytes_copy(g_sta, mac, WIFI_MAC_LEN);
    g_sta_set = 1;
    return 0;
}

int wifi_set_snonce(const uint8_t nonce[WIFI_NONCE_LEN]) {
    if (!nonce) return -1;
    bytes_copy(g_snonce, nonce, WIFI_NONCE_LEN);
    g_snonce_set = 1;
    return 0;
}

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
        join_reset();
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
    return -1;
}

/* ---- connect ----------------------------------------------------------- */

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
    bytes_zero(pmk, sizeof(pmk));

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

    if (raw_join_capable()) {
        int open = (bss->security == WIFI_SEC_OPEN);
        if (!open && !((bss->security & WIFI_SEC_WPA2_PSK) && (bss->security & WIFI_SEC_CCMP))) {
            set_error("Raw-frame join supports open or WPA2-PSK/CCMP networks");
            return -1;
        }
        if (!g_sta_set) {
            set_error("No station address for the raw join");
            return -1;
        }
        if (!open && !g_snonce_set) {
            set_error("No supplicant nonce for the raw join");
            return -1;
        }
        join_reset();
        bytes_copy(g_join_bssid, bss->bssid, 6);
        str_copy(g_join_ssid, bss->ssid, sizeof(g_join_ssid));
        g_join_ssid_len = bss->ssid_len;
        g_join_security = bss->security;
        g_join_sec_ie_len = bss->sec_ie_len;
        if (g_join_sec_ie_len > (uint8_t)sizeof(g_join_sec_ie)) g_join_sec_ie_len = 0;
        bytes_copy(g_join_sec_ie, bss->sec_ie, g_join_sec_ie_len);
        bytes_copy(g_pmk, pmk, sizeof(g_pmk));
        g_connected = 0;
        g_ssid[0] = 0;
        g_state = WIFI_STATE_JOINING;
        str_copy(g_status, "Joining", sizeof(g_status));
        g_step = WIFI_JOIN_AUTH;
        g_deadline_ms = g_now_ms + WIFI_JOIN_TIMEOUT_MS;
        if (send_auth() != 0) {
            join_fail("Driver refused the authenticate frame");
            return -1;
        }
        return 0;
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
    join_reset();
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
