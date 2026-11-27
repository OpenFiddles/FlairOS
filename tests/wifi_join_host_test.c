/* Host test: the WiFi core drives a complete join itself.
 *
 * A mock authenticator speaks the frames back: beacon, authenticate response,
 * associate response, 4-way message 1 and 3, group-key message 1, and
 * deauthenticate/disassociate when a failure is being injected. The core's
 * PTK and every EAPOL MIC are compared against values computed with Python's
 * hashlib and the cryptography package (AES key wrap), i.e. independently of
 * this code. The group key is wrapped with the KEK and delivered in the
 * group-key handshake, so the core has to unwrap it to install it.
 */
#include <stdio.h>
#include <string.h>
#include "../net/wifi/wifi_core.h"
#include "../net/wifi/wpa2.h"

static int failures = 0;
#define EXPECT(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); failures++; } } while (0)

/* ---- independent expected values (Python hashlib + cryptography) ------- */

static const char *PMK_HEX = "f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e";
static const char *KCK_HEX = "71f0e7cd4de5afa1d16aab4dd929a42e";
static const char *KEK_HEX = "91c214d9908c7a8feb84ce1bedc6fc0c";
static const char *TK_HEX  = "7d776e7068433976b28a2d240b7c1f2f";
static const char *GTK_HEX = "404142434445464748494a4b4c4d4e4f";
static const char *INNER_WRAP_HEX = "bfd042b894536d05e5355cdf164877644a5a11d27568e639";
static const char *OUTER_WRAP_HEX =
    "89ada0c972318556cfee6394407408df2921dc37905532d6c94406085a56caf1ebfb89d93146960a";
static const char *MIC_MSG2_HEX = "4203714388361d7be30f7080be66537c";
static const char *MIC_MSG4_HEX = "184b393517b4d7b82ed13fe31d0318c4";
static const char *MIC_GROUP2_HEX = "81f5aa54882a8adf131234f7f620b218";
static const char *MIC_MSG3_HEX = "49e136c2809ac4a18dc4ef54ad1af514";
static const char *MIC_MSG3_NOGTK_HEX = "b5d8659fcb46f0c56dd080c25d39719a";
static const char *MIC_GROUP1_HEX = "4808762b64711a4e0e3041d11205c63c";

/* Data-path frames, all computed independently in Python with
   cryptography's AESCCM (CCMP + AES key wrap). The unicast frames use the
   pairwise TK, the group ones the GTK with its key index. */
static const char *TX_UNICAST_HEX =
    "0841000000112233445502000000000100aabbccddee00002001000000000000324effcd641aa519a04bf4db";
static const char *TX_UNICAST_PN2_HEX =
    "0841000000112233445502000000000100aabbccddee00002002000000000000c18a2d1a2dcef84a961f459f";
static const char *RX_UNICAST_HEX =
    "0842000002000000000100112233445500aabbccddee000020010000000000007f310166616b215766cfd4d9";
static const char *RX_UNICAST_BADMIC_HEX =
    "0842000002000000000100112233445500aabbccddee000020010000000000007f310166616b215766cfd4d8";
static const char *RX_GROUP_HEX =
    "0842000001005e000001001122334455001122334455000060010000000000000d4e98ef74a7ddc8c60deeec2e";
static const char *TX_GROUP_HEX =
    "0841000000112233445502000000000101005e00000100006001000000000000eb27809699fd3ea8d1d9511bf8";
static const char *GTK2_HEX = "505152535455565758595a5b5c5d5e5f";
static const char *RX_GROUP2_HEX =
    "0842000001005e0000010011223344550011223344550000a0010000000000002ffab1ce98e0ad3e4e6fe8c9c9";
static const char *TX_GROUP2_HEX =
    "0841000000112233445502000000000101005e0000010000a001000000000000e64208882f1dc4fed8a3cf5ec5";
/* Group-key message 1 carrying the new GTK (key index 2), replay 4. */
static const char *REKEY_HEX =
    "080200000200000000010011223344550011223344550000aaaa03000000888e020300870213220010000000000000000400000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000cae8c02d53b4e6d8e84f0cb35a26c18e0028432ac6d0d6a1addddaf310a82743bfafbcfcabbab0d48502fc07262c4762012aea20ecef3113580e";
static const char *REKEY_BADMIC_HEX =
    "080200000200000000010011223344550011223344550000aaaa03000000888e020300870213220010000000000000000400000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000cbe8c02d53b4e6d8e84f0cb35a26c18e0028432ac6d0d6a1addddaf310a82743bfafbcfcabbab0d48502fc07262c4762012aea20ecef3113580e";
static const char *REKEY_REPLAY3_HEX =
    "080200000200000000010011223344550011223344550000aaaa03000000888e020300870213220010000000000000000300000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000489f5365eab03aeed281b168ba08d1890028432ac6d0d6a1addddaf310a82743bfafbcfcabbab0d48502fc07262c4762012aea20ecef3113580e";
static const char *GROUP2_AFTER_REKEY_HEX =
    "080100000011223344550200000000010011223344550000aaaa03000000888e0203005f02032200100000000000000004000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000008bb2546e529238465688dd0f24c4c0dd0000";

static const uint8_t AP[6] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 };
static const uint8_t STA[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 };
static const uint8_t DST[6] = { 0x00, 0xaa, 0xbb, 0xcc, 0xdd, 0xee };
static const uint8_t GRP[6] = { 0x01, 0x00, 0x5e, 0x00, 0x00, 0x01 };

static uint8_t anonce[32];
static uint8_t snonce[32];
static uint8_t gtk[16];
static struct wifi_ptk ptk;
static uint8_t key_data[40];
static int key_data_len;

/* ---- helpers ----------------------------------------------------------- */

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

static int hex_to(uint8_t *out, int max, const char *hex) {
    int n = 0;
    while (hex[0] && hex[1] && n < max) {
        out[n++] = (uint8_t)((hex_digit(hex[0]) << 4) | hex_digit(hex[1]));
        hex += 2;
    }
    return n;
}

static int hex_eq(const uint8_t *buf, int len, const char *hex) {
    for (int i = 0; i < len; i++) {
        uint8_t v = (uint8_t)((hex_digit(hex[i * 2]) << 4) | hex_digit(hex[i * 2 + 1]));
        if (buf[i] != v) return 0;
    }
    return 1;
}

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

static uint64_t be64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | (uint64_t)p[i];
    return v;
}

/* ---- mock raw-frame driver -------------------------------------------- */

static uint8_t last[512];
static int last_len = 0;
static int sends = 0;
static int disc_calls = 0;
static int pair_installs = 0, group_installs = 0;
static uint8_t pair_key[16], group_key[32];
static int group_key_len = 0;
static int pair_step = -1, group_step = -1;
static int pair_connected = -1, group_connected = -1;
static int msg4_sent = 0;

static int mock_power(void *ctx, int on) { (void)ctx; (void)on; return 0; }
static int mock_scan(void *ctx) { (void)ctx; return 0; }
static int mock_signal(void *ctx) { (void)ctx; return 72; }

static int mock_disconnect(void *ctx) {
    (void)ctx;
    disc_calls++;
    return 0;
}

static int mock_send(void *ctx, const uint8_t *frame, int len) {
    (void)ctx;
    if (len > 0 && len <= (int)sizeof(last)) {
        for (int i = 0; i < len; i++) last[i] = frame[i];
        last_len = len;
        sends++;
        return 0;
    }
    return -1;
}

static int mock_install(void *ctx, int pairwise, const uint8_t *key, int key_len) {
    (void)ctx;
    if (key_len < 0 || key_len > (int)sizeof(group_key)) return -1;
    if (pairwise) {
        pair_installs++;
        pair_step = wifi_join_step();
        pair_connected = wifi_connected();
        for (int i = 0; i < 16 && i < key_len; i++) pair_key[i] = key[i];
    } else {
        group_installs++;
        group_step = wifi_join_step();
        group_connected = wifi_connected();
        for (int i = 0; i < key_len; i++) group_key[i] = key[i];
        group_key_len = key_len;
    }
    return 0;
}

static const struct wifi_drv_ops raw_ops = {
    "raw test adapter", mock_power, mock_scan, 0 /* no firmware connect */,
    mock_disconnect, mock_signal, mock_send, mock_install, 0
};

static uint8_t rx_payload[1600];
static int rx_len = -1;
static uint8_t rx_src[6];

static void on_data(const uint8_t src[6], const uint8_t *payload, int len, void *ctx) {
    (void)ctx;
    for (int i = 0; i < 6; i++) rx_src[i] = src[i];
    rx_len = len > (int)sizeof(rx_payload) ? (int)sizeof(rx_payload) : len;
    for (int i = 0; i < rx_len; i++) rx_payload[i] = payload[i];
}

static int emit_hex(const char *hex) {
    uint8_t buf[512];
    int n = hex_to(buf, (int)sizeof(buf), hex);
    return wifi_rx_frame(buf, n, -40);
}

static int sent_is(const char *hex) {
    int n = (int)(strlen(hex) / 2);
    return n == last_len && hex_eq(last, n, hex);
}

static int payload_is(const char *text) {
    int n = (int)strlen(text);
    if (rx_len != n) return 0;
    for (int i = 0; i < n; i++)
        if (rx_payload[i] != (uint8_t)text[i]) return 0;
    return 1;
}

static void attempt_reset(void) {
    last_len = 0;
    rx_len = -1;
    sends = 0;
    disc_calls = 0;
    pair_installs = group_installs = 0;
    group_key_len = 0;
    pair_step = group_step = -1;
    pair_connected = group_connected = -1;
    msg4_sent = 0;
    for (int i = 0; i < 16; i++) pair_key[i] = 0;
    for (int i = 0; i < 32; i++) group_key[i] = 0;
}

/* ---- frames ------------------------------------------------------------ */

static void put_header(uint8_t *f, uint8_t fc0, uint8_t fc1, const uint8_t *da,
                       const uint8_t *sa, const uint8_t *bssid) {
    int o = 0;
    f[o++] = fc0; f[o++] = fc1; f[o++] = 0; f[o++] = 0;
    for (int i = 0; i < 6; i++) f[o++] = da[i];
    for (int i = 0; i < 6; i++) f[o++] = sa[i];
    for (int i = 0; i < 6; i++) f[o++] = bssid[i];
    f[o++] = 0; f[o++] = 0;
}

static const uint8_t rsn_psk_ccmp[] = {
    48, 18, 1, 0, 0x00, 0x0F, 0xAC, 4, 1, 0, 0x00, 0x0F, 0xAC, 4, 1, 0, 0x00, 0x0F, 0xAC, 2
};

static int build_beacon(uint8_t *f) {
    int o = 0;
    const char *ssid = "IEEE";
    int slen = 4;

    put_header(f, 0x80, 0x00, AP, AP, AP);
    o = 24;
    for (int i = 0; i < 8; i++) f[o++] = 0;   /* timestamp */
    f[o++] = 0x64; f[o++] = 0x00;             /* interval */
    f[o++] = 0x11; f[o++] = 0x00;             /* capability: ESS + privacy */
    f[o++] = 0; f[o++] = (uint8_t)slen;
    for (int i = 0; i < slen; i++) f[o++] = (uint8_t)ssid[i];
    f[o++] = 3; f[o++] = 1; f[o++] = 6;       /* DS: channel 6 */
    for (int i = 0; i < (int)sizeof(rsn_psk_ccmp); i++) f[o++] = rsn_psk_ccmp[i];
    return o;
}

static void send_auth_resp(uint16_t status) {
    uint8_t f[64];
    put_header(f, 0xB0, 0x00, STA, AP, AP);
    f[24] = 0; f[25] = 0;                  /* open system */
    f[26] = 2; f[27] = 0;                  /* sequence 2 */
    f[28] = (uint8_t)(status & 0xFF);
    f[29] = (uint8_t)(status >> 8);
    wifi_rx_frame(f, 30, -40);
}

static void send_assoc_resp(uint16_t status) {
    uint8_t f[64];
    put_header(f, 0x10, 0x00, STA, AP, AP);
    f[24] = 0x11; f[25] = 0x04;            /* capability */
    f[26] = (uint8_t)(status & 0xFF);
    f[27] = (uint8_t)(status >> 8);
    f[28] = 0x01; f[29] = 0xC0;            /* AID 1 */
    wifi_rx_frame(f, 30, -40);
}

static void send_deauth(uint16_t reason, int disassoc) {
    uint8_t f[64];
    put_header(f, disassoc ? 0xA0 : 0xC0, 0x00, STA, AP, AP);
    f[24] = (uint8_t)(reason & 0xFF);
    f[25] = (uint8_t)(reason >> 8);
    wifi_rx_frame(f, 26, -40);
}

/* Build an EAPOL-Key frame from the authenticator. The MIC is computed with
   the mock's KCK unless the frame carries no MIC bit; `corrupt` flips it. */
static int build_eapol(uint8_t *f, uint16_t key_info, uint64_t replay,
                       const uint8_t *nonce, const uint8_t *kd, int kd_len, int corrupt) {
    static const uint8_t snap[8] = { 0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E };
    uint8_t mic[16];
    uint8_t *e = f + 32;
    int body = 95 + kd_len;

    put_header(f, 0x08, 0x02, STA, AP, AP);
    for (int i = 0; i < 8; i++) f[24 + i] = snap[i];
    e[0] = 2; e[1] = 3;
    e[2] = (uint8_t)(body >> 8); e[3] = (uint8_t)body;
    e[4] = 2;
    e[5] = (uint8_t)(key_info >> 8); e[6] = (uint8_t)key_info;
    e[7] = 0; e[8] = 16;
    for (int i = 0; i < 8; i++) e[9 + i] = (uint8_t)(replay >> (8 * (7 - i)));
    for (int i = 0; i < 32; i++) e[17 + i] = nonce ? nonce[i] : 0;
    for (int i = 0; i < 16; i++) e[49 + i] = 0;
    for (int i = 0; i < 8; i++) e[65 + i] = 0;
    for (int i = 0; i < 8; i++) e[73 + i] = 0;
    for (int i = 0; i < 16; i++) e[81 + i] = 0;
    e[97] = (uint8_t)(kd_len >> 8); e[98] = (uint8_t)kd_len;
    for (int i = 0; i < kd_len; i++) e[99 + i] = kd[i];
    if (key_info & 0x0100u) {
        wifi_wpa2_mic(ptk.kck, e, 99 + kd_len, mic);
        if (corrupt) mic[0] ^= 0x01;
        for (int i = 0; i < 16; i++) e[81 + i] = mic[i];
    }
    return 32 + 99 + kd_len;
}

static void send_msg1(uint64_t replay) {
    uint8_t f[256];
    int len = build_eapol(f, 0x008A, replay, anonce, 0, 0, 0);
    wifi_rx_frame(f, len, -40);
}

static void send_msg3(uint64_t replay, int corrupt, int with_gtk) {
    uint8_t f[256];
    int len = build_eapol(f, 0x13CA, replay, anonce,
                          with_gtk ? key_data : 0, with_gtk ? key_data_len : 0, corrupt);
    if (!corrupt && replay == 2) /* the canonical 3/4 used in the success path */
        EXPECT(hex_eq(f + 32 + 81, 16, with_gtk ? MIC_MSG3_HEX : MIC_MSG3_NOGTK_HEX));
    wifi_rx_frame(f, len, -40);
}

static void send_group1(uint64_t replay, int corrupt) {
    uint8_t f[256];
    int len = build_eapol(f, 0x1312, replay, 0, key_data, key_data_len, corrupt);
    if (!corrupt && replay == 3)
        EXPECT(hex_eq(f + 32 + 81, 16, MIC_GROUP1_HEX));
    wifi_rx_frame(f, len, -40);
}

/* ---- captured frames from the core ------------------------------------ */

static const uint8_t *captured_eapol(int *len) {
    int hdr;
    if (last_len < 32) return 0;
    if ((last[0] & 0x0Cu) != 0x08u) return 0;
    hdr = (last[0] & 0x80u) ? 26 : 24;
    if (last_len < hdr + 8 + 4) return 0;
    if (last[hdr] != 0xAA || last[hdr + 6] != 0x88 || last[hdr + 7] != 0x8E) return 0;
    if (last[hdr + 9] != 3) return 0; /* EAPOL-Key */
    *len = 4 + (int)be16(last + hdr + 10);
    return last + hdr + 8;
}

/* ---- join steps -------------------------------------------------------- */

static int start_join(void) {
    uint8_t b[256];
    int len = build_beacon(b);
    wifi_rx_frame(b, len, -40);
    return wifi_connect(0, "password");
}

static int check_auth_req(void) {
    EXPECT(last_len == 30);
    EXPECT(last[0] == 0xB0);
    EXPECT(last[24] == 0 && last[25] == 0);      /* open system */
    EXPECT(last[26] == 1 && last[27] == 0);      /* sequence 1 */
    return 0;
}

static int check_assoc_req(void) {
    int i;
    int found_rsn = 0, found_ssid = 0;
    EXPECT(last[0] == 0x00);
    EXPECT((last[24] & 0x10) != 0);              /* privacy asserted */
    EXPECT(last[26] == 0x0A && last[27] == 0x00); /* listen interval 10 */
    i = 28;                                      /* after capability + listen interval */
    while (i + 2 <= last_len) {
        uint8_t id = last[i], elen = last[i + 1];
        if (i + 2 + elen > last_len) break;
        if (id == 0 && elen == 4 && memcmp(last + i + 2, "IEEE", 4) == 0) found_ssid = 1;
        if (id == 48 && elen == rsn_psk_ccmp[1] &&
            memcmp(last + i, rsn_psk_ccmp, (size_t)elen + 2) == 0) found_rsn = 1;
        i += 2 + elen;
    }
    EXPECT(found_ssid);
    EXPECT(found_rsn);
    return 0;
}

static int check_msg2(void) {
    int len = 0;
    const uint8_t *e = captured_eapol(&len);
    EXPECT(e != 0);
    if (!e) return -1;
    EXPECT(len == 99);
    EXPECT(be16(e + 5) == 0x010A);               /* pairwise + MIC */
    EXPECT(be64(e + 9) == 1);                    /* replay matches message 1 */
    EXPECT(memcmp(e + 17, snonce, 32) == 0);     /* SNonce */
    EXPECT(be16(e + 97) == 0);
    EXPECT(hex_eq(e + 81, 16, MIC_MSG2_HEX));
    EXPECT(wifi_wpa2_eapol_mic_ok(ptk.kck, e, len));
    return 0;
}

static int check_msg4(void) {
    int len = 0;
    const uint8_t *e = captured_eapol(&len);
    EXPECT(e != 0);
    if (!e) return -1;
    EXPECT(len == 99);
    EXPECT(be16(e + 5) == 0x030A);               /* pairwise + MIC + secure */
    EXPECT(be64(e + 9) == 2);
    for (int i = 0; i < 32; i++) EXPECT(e[17 + i] == 0);
    EXPECT(hex_eq(e + 81, 16, MIC_MSG4_HEX));
    EXPECT(wifi_wpa2_eapol_mic_ok(ptk.kck, e, len));
    return 0;
}

static int check_group2(void) {
    int len = 0;
    const uint8_t *e = captured_eapol(&len);
    EXPECT(e != 0);
    if (!e) return -1;
    EXPECT(len == 99);
    EXPECT(be16(e + 5) == 0x0312);               /* group + MIC + secure, key id 1 */
    EXPECT(be64(e + 9) == 3);
    EXPECT(hex_eq(e + 81, 16, MIC_GROUP2_HEX));
    EXPECT(wifi_wpa2_eapol_mic_ok(ptk.kck, e, len));
    return 0;
}

/* One complete successful join, asserting every stage. Returns 0 when the
   core reports connected. */
static int full_join(const char *label) {
    if (start_join() != 0) {
        printf("FAIL %s: connect refused (%s)\n", label, wifi_last_error());
        failures++;
        return -1;
    }
    EXPECT(wifi_join_step() == WIFI_JOIN_AUTH);
    EXPECT(wifi_connected() == 0);
    check_auth_req();

    send_auth_resp(0);
    EXPECT(wifi_join_step() == WIFI_JOIN_ASSOC);
    check_assoc_req();

    send_assoc_resp(0);
    EXPECT(wifi_join_step() == WIFI_JOIN_MSG1);
    EXPECT(wifi_assoc_aid() == 1);

    send_msg1(1);
    EXPECT(wifi_join_step() == WIFI_JOIN_MSG3);
    EXPECT(wifi_connected() == 0);
    check_msg2();

    send_msg3(2, 0, 0);
    EXPECT(wifi_join_step() == WIFI_JOIN_GROUP);
    EXPECT(wifi_connected() == 0);
    check_msg4();
    msg4_sent = 1;
    EXPECT(pair_installs == 1);
    EXPECT(pair_step == WIFI_JOIN_MSG3);        /* only after 3/4 was verified */
    EXPECT(pair_connected == 0);
    EXPECT(hex_eq(pair_key, 16, TK_HEX));

    send_group1(3, 0);
    check_group2();
    EXPECT(group_installs == 1);                /* pairwise then group, once each */
    EXPECT(group_step == WIFI_JOIN_GROUP);      /* only after the group 1/2 arrived */
    EXPECT(group_connected == 0);
    EXPECT(hex_eq(group_key, group_key_len, GTK_HEX));
    EXPECT(msg4_sent == 1);

    EXPECT(wifi_connected() == 1);
    EXPECT(wifi_state() == WIFI_STATE_CONNECTED);
    EXPECT(strcmp(wifi_ssid(), "IEEE") == 0);
    EXPECT(wifi_signal() == 72);
    return 0;
}

/* Every failure must end disconnected, idle-step, with a reason recorded and
   the driver told to disconnect; a fresh attempt must then succeed. */
static void expect_failure(const char *label, const char *reason_part, int disc_before) {
    EXPECT(!wifi_connected());
    EXPECT(wifi_join_step() == WIFI_JOIN_IDLE);
    EXPECT(strstr(wifi_last_error(), reason_part) != 0);
    EXPECT(disc_calls == disc_before + 1);
    if (!wifi_connected() && strstr(wifi_last_error(), reason_part) && disc_calls == disc_before + 1) {
        /* expected */
    } else {
        printf("  (failure path %s: state=%s reason=\"%s\" disconnects=%d)\n",
               label, wifi_state_text(), wifi_last_error(), disc_calls);
    }
}

int main(void) {
    uint8_t pmk[32];
    uint8_t inner[32];
    int inner_len;
    int outer_len = 0;
    int disc;

    /* Platform data: station MAC and supplicant entropy. */
    for (int i = 0; i < 32; i++) anonce[i] = (uint8_t)i;
    for (int i = 0; i < 32; i++) snonce[i] = (uint8_t)(0x20 + i);
    for (int i = 0; i < 16; i++) gtk[i] = (uint8_t)(0x40 + i);

    /* 1. Crypto against independently computed values. */
    EXPECT(wifi_wpa2_pmk("password", (const uint8_t *)"IEEE", 4, pmk) == 0);
    EXPECT(hex_eq(pmk, 32, PMK_HEX));
    EXPECT(wifi_wpa2_ptk(pmk, AP, STA, anonce, snonce, &ptk) == 0);
    EXPECT(hex_eq(ptk.kck, 16, KCK_HEX));
    EXPECT(hex_eq(ptk.kek, 16, KEK_HEX));
    EXPECT(hex_eq(ptk.tk, 16, TK_HEX));

    /* The authenticator wraps the GTK with the KEK, encapsulated in a GTK
       KDE and padded to the 8-octet boundary RFC 3394 requires. */
    inner_len = wifi_wpa2_wrap(ptk.kek, gtk, 16, inner, (int)sizeof(inner));
    EXPECT(inner_len == 24);
    EXPECT(hex_eq(inner, inner_len, INNER_WRAP_HEX));
    key_data[0] = 0xDD;
    key_data[1] = (uint8_t)(5 + inner_len);
    key_data[2] = 0x00; key_data[3] = 0x0F; key_data[4] = 0xAC; key_data[5] = 0x01;
    key_data[6] = 0x01; /* key id 1 */
    for (int i = 0; i < inner_len; i++) key_data[7 + i] = inner[i];
    key_data_len = 7 + inner_len;
    while ((key_data_len % 8) != 0) key_data[key_data_len++] = 0;
    {
        uint8_t outer[64];
        outer_len = wifi_wpa2_wrap(ptk.kek, key_data, key_data_len, outer, (int)sizeof(outer));
        EXPECT(outer_len == 40);
        if (outer_len == 40) {
            memcpy(key_data, outer, 40);
            key_data_len = 40;
        }
    }
    EXPECT(hex_eq(key_data, key_data_len, OUTER_WRAP_HEX));

    /* AES key wrap primitives against the FIPS-197 and RFC 3394 vectors. */
    {
        uint8_t k[16], p[16], c[16];
        struct wifi_aes128 aes;
        hex_to(k, 16, "000102030405060708090a0b0c0d0e0f");
        hex_to(p, 16, "00112233445566778899aabbccddeeff");
        wifi_aes128_setkey(&aes, k);
        wifi_aes128_encrypt_block(&aes, p, c);
        EXPECT(hex_eq(c, 16, "69c4e0d86a7b0430d8cdb78070b4c55a"));
        wifi_aes128_decrypt_block(&aes, c, p);
        EXPECT(hex_eq(p, 16, "00112233445566778899aabbccddeeff"));
        EXPECT(inner_len == 24);
    }

    /* 2. Adapter and platform inputs. */
    EXPECT(wifi_register_adapter(&raw_ops) == 0);
    EXPECT(wifi_raw_join() == 1);
    EXPECT(wifi_power(1) == 0);
    EXPECT(wifi_scan_start() == 0);
    {
        uint8_t b[256];
        int len = build_beacon(b);
        EXPECT(wifi_rx_frame(b, len, -40) == 0);
    }
    EXPECT(wifi_scan_stop() == 0);
    EXPECT(wifi_scan_count() == 1);

    EXPECT(wifi_connect(0, "password") != 0);
    EXPECT(strstr(wifi_last_error(), "station address") != 0);
    EXPECT(wifi_set_mac(STA) == 0);
    EXPECT(wifi_connect(0, "password") != 0);
    EXPECT(strstr(wifi_last_error(), "nonce") != 0);
    EXPECT(wifi_set_snonce(snonce) == 0);

    /* 3. A full, successful join. */
    attempt_reset();
    EXPECT(full_join("success") == 0);
    EXPECT(pair_installs == 1 && group_installs == 1);

    /* 3b. An authenticator that delivers the GTK in message 3 completes with
       4/4: both keys are installed before the link is reported. */
    attempt_reset();
    EXPECT(start_join() == 0);
    send_auth_resp(0);
    send_assoc_resp(0);
    send_msg1(1);
    send_msg3(2, 0, 1);
    EXPECT(pair_installs == 1);
    EXPECT(group_installs == 1);
    EXPECT(pair_connected == 0 && group_connected == 0);
    EXPECT(group_step == WIFI_JOIN_GROUP);
    EXPECT(hex_eq(group_key, group_key_len, GTK_HEX));
    EXPECT(wifi_connected() == 1);
    EXPECT(wifi_disconnect() == 0);
    EXPECT(wifi_connected() == 0);
    EXPECT(wifi_join_step() == WIFI_JOIN_IDLE);

    /* 4. Failure paths. Each starts disconnected, records why, calls the
       driver's disconnect op, and is followed by a successful retry. */
    {
        /* authenticate rejected */
        attempt_reset();
        EXPECT(start_join() == 0);
        disc = disc_calls;
        send_auth_resp(1);
        expect_failure("auth", "Authentication rejected", disc);
        attempt_reset();
        EXPECT(full_join("retry after auth") == 0);
    }
    {
        /* association rejected */
        attempt_reset();
        EXPECT(start_join() == 0);
        send_auth_resp(0);
        disc = disc_calls;
        send_assoc_resp(17);
        expect_failure("assoc", "Association rejected", disc);
        attempt_reset();
        EXPECT(full_join("retry after assoc") == 0);
    }
    {
        /* the authenticator reports a MIC failure after 2/4 */
        attempt_reset();
        EXPECT(start_join() == 0);
        send_auth_resp(0);
        send_assoc_resp(0);
        send_msg1(1);
        disc = disc_calls;
        send_deauth(14, 0);
        expect_failure("mic msg2", "MIC failure", disc);
        attempt_reset();
        EXPECT(full_join("retry after msg2 mic") == 0);
    }
    {
        /* the authenticator reports a MIC failure after 4/4 */
        attempt_reset();
        EXPECT(start_join() == 0);
        send_auth_resp(0);
        send_assoc_resp(0);
        send_msg1(1);
        send_msg3(2, 0, 0);
        disc = disc_calls;
        send_deauth(14, 0);
        expect_failure("mic msg4", "MIC failure", disc);
        attempt_reset();
        EXPECT(full_join("retry after msg4 mic") == 0);
    }
    {
        /* 3/4 arrives with a bad MIC */
        attempt_reset();
        EXPECT(start_join() == 0);
        send_auth_resp(0);
        send_assoc_resp(0);
        send_msg1(1);
        disc = disc_calls;
        send_msg3(2, 1, 0);
        expect_failure("mic msg3", "MIC mismatch on 4-way message 3", disc);
        EXPECT(pair_installs == 0); /* nothing installed from an unverified message */
        attempt_reset();
        EXPECT(full_join("retry after msg3 mic") == 0);
    }
    {
        /* replay counter regression on 3/4 */
        attempt_reset();
        EXPECT(start_join() == 0);
        send_auth_resp(0);
        send_assoc_resp(0);
        send_msg1(1);
        disc = disc_calls;
        send_msg3(1, 0, 0);
        expect_failure("replay msg3", "Replay counter regression", disc);
        attempt_reset();
        EXPECT(full_join("retry after replay msg3") == 0);
    }
    {
        /* group message 1 with a bad MIC */
        attempt_reset();
        EXPECT(start_join() == 0);
        send_auth_resp(0);
        send_assoc_resp(0);
        send_msg1(1);
        send_msg3(2, 0, 0);
        disc = disc_calls;
        send_group1(3, 1);
        expect_failure("mic group1", "MIC mismatch on the group message", disc);
        EXPECT(group_installs == 0);
        attempt_reset();
        EXPECT(full_join("retry after group mic") == 0);
    }
    {
        /* group replay regression */
        attempt_reset();
        EXPECT(start_join() == 0);
        send_auth_resp(0);
        send_assoc_resp(0);
        send_msg1(1);
        send_msg3(2, 0, 0);
        disc = disc_calls;
        send_group1(2, 0);
        expect_failure("replay group", "Replay counter regression", disc);
        attempt_reset();
        EXPECT(full_join("retry after replay group") == 0);
    }
    {
        /* deauthenticate mid-handshake */
        attempt_reset();
        EXPECT(start_join() == 0);
        send_auth_resp(0);
        disc = disc_calls;
        send_deauth(3, 0);
        expect_failure("deauth", "Deauthenticated", disc);
        attempt_reset();
        EXPECT(full_join("retry after deauth") == 0);
    }
    {
        /* disassociate mid-handshake */
        attempt_reset();
        EXPECT(start_join() == 0);
        send_auth_resp(0);
        send_assoc_resp(0);
        disc = disc_calls;
        send_deauth(8, 1);
        expect_failure("disassoc", "Disassociated", disc);
        attempt_reset();
        EXPECT(full_join("retry after disassoc") == 0);
    }
    {
        /* no answer at all within the step timeout */
        attempt_reset();
        EXPECT(start_join() == 0);
        disc = disc_calls;
        wifi_tick(1001);
        expect_failure("timeout", "Join timeout", disc);
        attempt_reset();
        EXPECT(full_join("retry after timeout") == 0);
    }

    /* 5. CCM against the published RFC 3610 Packet Vector #1. */
    {
        uint8_t key[16], nonce[13], aad[8], plain[23], out[31], back[31];
        hex_to(key, 16, "c0c1c2c3c4c5c6c7c8c9cacbcccdcecf");
        hex_to(nonce, 13, "00000003020100a0a1a2a3a4a5");
        hex_to(aad, 8, "0001020304050607");
        EXPECT(hex_to(plain, 23, "08090a0b0c0d0e0f101112131415161718191a1b1c1d1e") == 23);
        EXPECT(wifi_ccm_encrypt(key, nonce, aad, 8, plain, 23, out) == 0);
        EXPECT(hex_eq(out, 31,
                      "588c979a61c663d2f066d0c2c0f989806d5f6b61dac38417e8d12cfdf926e0"));
        for (int i = 0; i < 31; i++) back[i] = out[i];
        EXPECT(wifi_ccm_decrypt(key, nonce, aad, 8, back, 23, back + 23) == 0);
        EXPECT(hex_eq(back, 23, "08090a0b0c0d0e0f101112131415161718191a1b1c1d1e"));
        back[30] ^= 0x01; /* corrupt the MIC: the tag check must reject it */
        EXPECT(wifi_ccm_decrypt(key, nonce, aad, 8, back, 23, back + 23) != 0);
    }

    /* 6. Data path: a freshly joined link carries CCMP traffic both ways. */
    wifi_set_data_rx(on_data, 0);
    attempt_reset();
    EXPECT(full_join("data path") == 0);
    EXPECT(wifi_data_ready() == 1);
    EXPECT(wifi_group_key_id() == 1);

    /* Outgoing unicast under the pairwise TK, packet number 1 then 2. */
    EXPECT(wifi_send_data(DST, (const uint8_t *)"ping", 4) == 0);
    EXPECT(sent_is(TX_UNICAST_HEX));
    EXPECT(wifi_tx_packet_number(0) == 1);
    EXPECT(wifi_send_data(DST, (const uint8_t *)"ping", 4) == 0);
    EXPECT(sent_is(TX_UNICAST_PN2_HEX));
    EXPECT(wifi_tx_packet_number(0) == 2);

    /* Incoming unicast: authenticated, decrypted and delivered. */
    rx_len = -1;
    EXPECT(emit_hex(RX_UNICAST_HEX) == 0);
    EXPECT(payload_is("pong"));
    EXPECT(rx_src[0] == 0x00 && rx_src[5] == 0xee); /* original transmitter */
    EXPECT(wifi_rx_packet_number(0) == 1);

    /* A group-addressed frame arrives under the GTK and its key index. */
    rx_len = -1;
    EXPECT(emit_hex(RX_GROUP_HEX) == 0);
    EXPECT(payload_is("hello"));
    EXPECT(wifi_rx_packet_number(1) == 1);

    /* And a group-addressed frame leaves under the GTK. */
    EXPECT(wifi_send_data(GRP, (const uint8_t *)"world", 5) == 0);
    EXPECT(sent_is(TX_GROUP_HEX));
    EXPECT(wifi_tx_packet_number(1) == 1);

    /* 7. GTK rekey: key index 1 -> 2, pairwise key and the link untouched. */
    EXPECT(emit_hex(REKEY_HEX) == 0);
    EXPECT(wifi_connected() == 1);
    EXPECT(wifi_join_step() == WIFI_JOIN_DONE);
    EXPECT(wifi_group_key_id() == 2);
    EXPECT(pair_installs == 1);
    EXPECT(group_installs == 2);
    EXPECT(group_connected == 1); /* installed on a live link */
    EXPECT(hex_eq(group_key, group_key_len, GTK2_HEX));
    EXPECT(hex_eq(pair_key, 16, TK_HEX));
    EXPECT(sent_is(GROUP2_AFTER_REKEY_HEX)); /* group-key message 2 */

    /* Traffic continues under the new key; the pairwise counter is untouched. */
    rx_len = -1;
    EXPECT(emit_hex(RX_GROUP2_HEX) == 0);
    EXPECT(payload_is("again"));
    EXPECT(wifi_rx_packet_number(2) == 1);
    EXPECT(wifi_rx_packet_number(0) == 1);
    EXPECT(wifi_send_data(GRP, (const uint8_t *)"fresh", 5) == 0);
    EXPECT(sent_is(TX_GROUP2_HEX));
    EXPECT(wifi_tx_packet_number(2) == 1);
    EXPECT(wifi_disconnect() == 0);

    /* 8. Data-path failures: each ends disconnected and then retries cleanly. */
    {
        attempt_reset();
        EXPECT(full_join("data mic") == 0);
        disc = disc_calls;
        EXPECT(emit_hex(RX_UNICAST_BADMIC_HEX) == -1);
        expect_failure("data mic", "MIC mismatch on a data frame", disc);
        attempt_reset();
        EXPECT(full_join("retry after data mic") == 0);
        EXPECT(wifi_disconnect() == 0);
    }
    {
        attempt_reset();
        EXPECT(full_join("data replay") == 0);
        EXPECT(emit_hex(RX_UNICAST_HEX) == 0);
        disc = disc_calls;
        EXPECT(emit_hex(RX_UNICAST_HEX) == -1);
        expect_failure("data replay", "Data replay counter regression", disc);
        attempt_reset();
        EXPECT(full_join("retry after data replay") == 0);
        EXPECT(wifi_disconnect() == 0);
    }
    {
        attempt_reset();
        EXPECT(full_join("rekey mic") == 0);
        disc = disc_calls;
        EXPECT(emit_hex(REKEY_BADMIC_HEX) == -1);
        expect_failure("rekey mic", "MIC mismatch on a group rekey", disc);
        attempt_reset();
        EXPECT(full_join("retry after rekey mic") == 0);
        EXPECT(wifi_disconnect() == 0);
    }
    {
        attempt_reset();
        EXPECT(full_join("rekey replay") == 0);
        disc = disc_calls;
        EXPECT(emit_hex(REKEY_REPLAY3_HEX) == -1);
        expect_failure("rekey replay", "Replay counter regression", disc);
        attempt_reset();
        EXPECT(full_join("retry after rekey replay") == 0);
        EXPECT(wifi_disconnect() == 0);
    }

    /* 9. Explicit disconnect resets the join too. */
    EXPECT(wifi_disconnect() == 0);
    EXPECT(wifi_connected() == 0);
    EXPECT(wifi_join_step() == WIFI_JOIN_IDLE);

    if (failures == 0) {
        puts("WiFi core-driven join test: all checks passed");
        return 0;
    }
    printf("WiFi core-driven join test: %d check(s) failed\n", failures);
    return 1;
}
