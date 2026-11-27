/* SPDX-License-Identifier: AGPL-3.0-only */
/* WPA2-PSK key derivation and EAPOL-Key MIC verification.
 *
 * Freestanding C99: no libc, no heap, no global state. Every routine is a
 * direct transcription of the published definition:
 *   SHA-1      FIPS 180-4
 *   HMAC       RFC 2104
 *   PBKDF2     RFC 8018, used as 802.11 J.4.1 for the PMK
 *   PRF-512    IEEE 802.11-2020 J.4.2 for the PTK
 *   MIC        IEEE 802.11-2020 12.5.2 (HMAC-SHA1 truncated to 128 bits)
 */
#include "wpa2.h"

struct sha1_ctx {
    uint32_t h[5];
    uint8_t buf[64];
    int n;
    uint64_t bytes;
};

static uint32_t rol32(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

static void sha1_block(uint32_t h[5], const uint8_t *p) {
    uint32_t w[80];
    uint32_t a, b, c, d, e;
    int i;
    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
               ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
    }
    for (i = 16; i < 80; i++) w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4];
    for (i = 0; i < 80; i++) {
        uint32_t f, k, t;
        if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999u; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
        else { f = b ^ c ^ d; k = 0xCA62C1D6u; }
        t = rol32(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol32(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

static void sha1_init(struct sha1_ctx *c) {
    c->h[0] = 0x67452301u;
    c->h[1] = 0xEFCDAB89u;
    c->h[2] = 0x98BADCFEu;
    c->h[3] = 0x10325476u;
    c->h[4] = 0xC3D2E1F0u;
    c->n = 0;
    c->bytes = 0;
}

static void sha1_update(struct sha1_ctx *c, const uint8_t *p, int len) {
    c->bytes += (uint64_t)len;
    while (len > 0) {
        int take = 64 - c->n;
        if (take > len) take = len;
        for (int i = 0; i < take; i++) c->buf[c->n + i] = p[i];
        c->n += take;
        p += take;
        len -= take;
        if (c->n == 64) {
            sha1_block(c->h, c->buf);
            c->n = 0;
        }
    }
}

static void sha1_final(struct sha1_ctx *c, uint8_t out[20]) {
    uint64_t bits = c->bytes * 8u;
    int i;
    c->buf[c->n++] = 0x80;
    if (c->n > 56) {
        while (c->n < 64) c->buf[c->n++] = 0;
        sha1_block(c->h, c->buf);
        c->n = 0;
    }
    while (c->n < 56) c->buf[c->n++] = 0;
    for (i = 7; i >= 0; i--) c->buf[c->n++] = (uint8_t)(bits >> (i * 8));
    sha1_block(c->h, c->buf);
    for (i = 0; i < 5; i++) {
        out[i * 4] = (uint8_t)(c->h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(c->h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(c->h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)c->h[i];
    }
}

void wifi_sha1(const uint8_t *data, int len, uint8_t out[20]) {
    struct sha1_ctx c;
    sha1_init(&c);
    sha1_update(&c, data, len);
    sha1_final(&c, out);
}

/* HMAC over up to four segments, which is what the PRF and the MIC need
   without concatenating frames into a heap buffer. */
static void hmac_parts(const uint8_t *key, int key_len,
                       const uint8_t *s1, int n1, const uint8_t *s2, int n2,
                       const uint8_t *s3, int n3, const uint8_t *s4, int n4,
                       uint8_t out[20]) {
    uint8_t k[64], ipad[64], opad[64], inner[20];
    struct sha1_ctx c;
    int i;

    for (i = 0; i < 64; i++) k[i] = 0;
    if (key_len > 64) wifi_sha1(key, key_len, k);
    else for (i = 0; i < key_len; i++) k[i] = key[i];
    for (i = 0; i < 64; i++) {
        ipad[i] = (uint8_t)(k[i] ^ 0x36);
        opad[i] = (uint8_t)(k[i] ^ 0x5C);
    }

    sha1_init(&c);
    sha1_update(&c, ipad, 64);
    if (n1 > 0) sha1_update(&c, s1, n1);
    if (n2 > 0) sha1_update(&c, s2, n2);
    if (n3 > 0) sha1_update(&c, s3, n3);
    if (n4 > 0) sha1_update(&c, s4, n4);
    sha1_final(&c, inner);

    sha1_init(&c);
    sha1_update(&c, opad, 64);
    sha1_update(&c, inner, 20);
    sha1_final(&c, out);
}

void wifi_hmac_sha1(const uint8_t *key, int key_len, const uint8_t *data, int len,
                    uint8_t out[20]) {
    hmac_parts(key, key_len, data, len, 0, 0, 0, 0, 0, 0, out);
}

int wifi_pbkdf2_sha1(const uint8_t *pass, int pass_len, const uint8_t *salt, int salt_len,
                     int iterations, uint8_t *out, int out_len) {
    uint8_t u[20], t[20], block[64];
    uint32_t counter = 1;
    int produced = 0;

    if (!out || out_len <= 0 || iterations < 1) return -1;
    if (salt_len < 0 || salt_len > 60) return -1;

    while (produced < out_len) {
        int take = out_len - produced;
        int i, j;
        for (i = 0; i < salt_len; i++) block[i] = salt[i];
        block[salt_len] = (uint8_t)(counter >> 24);
        block[salt_len + 1] = (uint8_t)(counter >> 16);
        block[salt_len + 2] = (uint8_t)(counter >> 8);
        block[salt_len + 3] = (uint8_t)counter;

        hmac_parts(pass, pass_len, block, salt_len + 4, 0, 0, 0, 0, 0, 0, u);
        for (i = 0; i < 20; i++) t[i] = u[i];
        for (j = 1; j < iterations; j++) {
            hmac_parts(pass, pass_len, u, 20, 0, 0, 0, 0, 0, 0, u);
            for (i = 0; i < 20; i++) t[i] ^= u[i];
        }
        if (take > 20) take = 20;
        for (i = 0; i < take; i++) out[produced + i] = t[i];
        produced += take;
        counter++;
    }
    return 0;
}

int wifi_wpa2_pmk(const char *passphrase, const uint8_t *ssid, int ssid_len,
                  uint8_t pmk[WIFI_PMK_LEN]) {
    int pass_len = 0;
    if (!passphrase || !ssid || !pmk) return -1;
    if (ssid_len < 1 || ssid_len > 32) return -1;
    while (passphrase[pass_len]) pass_len++;
    if (pass_len < 8 || pass_len > 63) return -1;
    return wifi_pbkdf2_sha1((const uint8_t *)passphrase, pass_len, ssid, ssid_len,
                            4096, pmk, WIFI_PMK_LEN);
}

static int bytes_cmp(const uint8_t *a, const uint8_t *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

int wifi_wpa2_ptk(const uint8_t pmk[WIFI_PMK_LEN], const uint8_t ap[6], const uint8_t sta[6],
                  const uint8_t anonce[WIFI_NONCE_LEN], const uint8_t snonce[WIFI_NONCE_LEN],
                  struct wifi_ptk *out) {
    static const uint8_t label[] = "Pairwise key expansion";
    uint8_t b[6 + 6 + WIFI_NONCE_LEN + WIFI_NONCE_LEN];
    uint8_t block[WIFI_PTK_LEN];
    uint8_t zero = 0, counter = 0;
    int o = 0;

    if (!pmk || !ap || !sta || !anonce || !snonce || !out) return -1;

    if (bytes_cmp(ap, sta, 6) <= 0) {
        for (int i = 0; i < 6; i++) b[o++] = ap[i];
        for (int i = 0; i < 6; i++) b[o++] = sta[i];
    } else {
        for (int i = 0; i < 6; i++) b[o++] = sta[i];
        for (int i = 0; i < 6; i++) b[o++] = ap[i];
    }
    if (bytes_cmp(anonce, snonce, WIFI_NONCE_LEN) <= 0) {
        for (int i = 0; i < WIFI_NONCE_LEN; i++) b[o++] = anonce[i];
        for (int i = 0; i < WIFI_NONCE_LEN; i++) b[o++] = snonce[i];
    } else {
        for (int i = 0; i < WIFI_NONCE_LEN; i++) b[o++] = snonce[i];
        for (int i = 0; i < WIFI_NONCE_LEN; i++) b[o++] = anonce[i];
    }

    o = 0;
    while (o < WIFI_PTK_LEN) {
        uint8_t h[20];
        int take = WIFI_PTK_LEN - o;
        hmac_parts(pmk, WIFI_PMK_LEN, label, (int)sizeof(label) - 1,
                   &zero, 1, b, (int)sizeof(b), &counter, 1, h);
        if (take > 20) take = 20;
        for (int i = 0; i < take; i++) block[o + i] = h[i];
        o += take;
        counter++;
    }
    for (int i = 0; i < 16; i++) {
        out->kck[i] = block[i];
        out->kek[i] = block[16 + i];
        out->tk[i] = block[32 + i];
    }
    return 0;
}

void wifi_wpa2_mic(const uint8_t kck[16], const uint8_t *eapol, int len,
                   uint8_t mic[WIFI_EAPOL_MIC_LEN]) {
    uint8_t h[20];
    hmac_parts(kck, 16, eapol, len, 0, 0, 0, 0, 0, 0, h);
    for (int i = 0; i < WIFI_EAPOL_MIC_LEN; i++) mic[i] = h[i];
}

int wifi_wpa2_mic_check(const uint8_t kck[16], const uint8_t *eapol, int len,
                        const uint8_t mic[WIFI_EAPOL_MIC_LEN]) {
    uint8_t calc[20];
    uint8_t diff = 0;
    if (!kck || !eapol || !mic || len <= 0) return 0;
    wifi_wpa2_mic(kck, eapol, len, calc);
    for (int i = 0; i < WIFI_EAPOL_MIC_LEN; i++) diff |= (uint8_t)(calc[i] ^ mic[i]);
    return diff == 0;
}

int wifi_wpa2_eapol_mic_ok(const uint8_t kck[16], const uint8_t *eapol, int len) {
    uint8_t zeros[WIFI_EAPOL_MIC_LEN];
    uint8_t calc[20];
    uint8_t diff = 0;
    if (!kck || !eapol || len < WIFI_EAPOL_MIC_OFFSET + WIFI_EAPOL_MIC_LEN) return 0;
    for (int i = 0; i < WIFI_EAPOL_MIC_LEN; i++) zeros[i] = 0;
    hmac_parts(kck, 16,
               eapol, WIFI_EAPOL_MIC_OFFSET,
               zeros, WIFI_EAPOL_MIC_LEN,
               eapol + WIFI_EAPOL_MIC_OFFSET + WIFI_EAPOL_MIC_LEN,
               len - WIFI_EAPOL_MIC_OFFSET - WIFI_EAPOL_MIC_LEN,
               0, 0, calc);
    for (int i = 0; i < WIFI_EAPOL_MIC_LEN; i++)
        diff |= (uint8_t)(calc[i] ^ eapol[WIFI_EAPOL_MIC_OFFSET + i]);
    return diff == 0;
}
