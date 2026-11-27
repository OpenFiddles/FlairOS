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

/* ---- AES-128 (FIPS 197) ------------------------------------------------ */

static uint8_t aes_sbox[256];
static uint8_t aes_isbox[256];
static int aes_tables_ready = 0;

static uint8_t gf_mul(uint8_t a, uint8_t b) {
    uint8_t p = 0;
    for (int i = 0; i < 8; i++) {
        if (b & 1u) p ^= a;
        uint8_t hi = (uint8_t)(a & 0x80u);
        a = (uint8_t)(a << 1);
        if (hi) a ^= 0x1Bu;
        b = (uint8_t)(b >> 1);
    }
    return p;
}

/* Multiplicative inverse in GF(2^8); 0 maps to 0 by definition. */
static uint8_t gf_inv(uint8_t a) {
    uint8_t r = 1, base = a;
    int e = 254;
    if (a == 0) return 0;
    while (e > 0) {
        if (e & 1) r = gf_mul(r, base);
        base = gf_mul(base, base);
        e >>= 1;
    }
    return r;
}

static uint8_t rol8(uint8_t x, int n) {
    return (uint8_t)((uint8_t)(x << n) | (uint8_t)(x >> (8 - n)));
}

/* The affine transform of FIPS 197 5.1.1 applied to the field inverse. */
static void aes_tables(void) {
    int i;
    if (aes_tables_ready) return;
    for (i = 0; i < 256; i++) {
        uint8_t x = gf_inv((uint8_t)i);
        aes_sbox[i] = (uint8_t)(x ^ rol8(x, 1) ^ rol8(x, 2) ^ rol8(x, 3) ^ rol8(x, 4) ^ 0x63u);
    }
    for (i = 0; i < 256; i++) aes_isbox[aes_sbox[i]] = (uint8_t)i;
    aes_tables_ready = 1;
}

void wifi_aes128_setkey(struct wifi_aes128 *a, const uint8_t key[16]) {
    uint8_t rcon = 0x01u;
    int i, j;
    if (!a || !key) return;
    aes_tables();
    for (i = 0; i < 16; i++) a->rk[i] = key[i];
    for (i = 4; i < 44; i++) {
        uint8_t t[4];
        for (j = 0; j < 4; j++) t[j] = a->rk[4 * (i - 1) + j];
        if ((i % 4) == 0) {
            uint8_t first = t[0];
            t[0] = (uint8_t)(aes_sbox[t[1]] ^ rcon);
            t[1] = aes_sbox[t[2]];
            t[2] = aes_sbox[t[3]];
            t[3] = aes_sbox[first];
            rcon = gf_mul(rcon, 0x02u);
        }
        for (j = 0; j < 4; j++) a->rk[4 * i + j] = (uint8_t)(a->rk[4 * (i - 4) + j] ^ t[j]);
    }
}

static void add_round_key(uint8_t s[16], const uint8_t *rk) {
    for (int i = 0; i < 16; i++) s[i] ^= rk[i];
}

static void sub_bytes(uint8_t s[16]) {
    for (int i = 0; i < 16; i++) s[i] = aes_sbox[s[i]];
}

static void inv_sub_bytes(uint8_t s[16]) {
    for (int i = 0; i < 16; i++) s[i] = aes_isbox[s[i]];
}

static void shift_rows(uint8_t s[16]) {
    uint8_t t;
    t = s[1]; s[1] = s[5]; s[5] = s[9]; s[9] = s[13]; s[13] = t;
    t = s[2]; s[2] = s[10]; s[10] = t;
    t = s[6]; s[6] = s[14]; s[14] = t;
    t = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = s[3]; s[3] = t;
}

static void inv_shift_rows(uint8_t s[16]) {
    uint8_t t;
    t = s[13]; s[13] = s[9]; s[9] = s[5]; s[5] = s[1]; s[1] = t;
    t = s[2]; s[2] = s[10]; s[10] = t;
    t = s[6]; s[6] = s[14]; s[14] = t;
    t = s[3]; s[3] = s[7]; s[7] = s[11]; s[11] = s[15]; s[15] = t;
}

static void mix_columns(uint8_t s[16]) {
    for (int c = 0; c < 4; c++) {
        uint8_t *p = s + 4 * c;
        uint8_t a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3];
        p[0] = (uint8_t)(gf_mul(a0, 2) ^ gf_mul(a1, 3) ^ a2 ^ a3);
        p[1] = (uint8_t)(a0 ^ gf_mul(a1, 2) ^ gf_mul(a2, 3) ^ a3);
        p[2] = (uint8_t)(a0 ^ a1 ^ gf_mul(a2, 2) ^ gf_mul(a3, 3));
        p[3] = (uint8_t)(gf_mul(a0, 3) ^ a1 ^ a2 ^ gf_mul(a3, 2));
    }
}

static void inv_mix_columns(uint8_t s[16]) {
    for (int c = 0; c < 4; c++) {
        uint8_t *p = s + 4 * c;
        uint8_t a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3];
        p[0] = (uint8_t)(gf_mul(a0, 14) ^ gf_mul(a1, 11) ^ gf_mul(a2, 13) ^ gf_mul(a3, 9));
        p[1] = (uint8_t)(gf_mul(a0, 9) ^ gf_mul(a1, 14) ^ gf_mul(a2, 11) ^ gf_mul(a3, 13));
        p[2] = (uint8_t)(gf_mul(a0, 13) ^ gf_mul(a1, 9) ^ gf_mul(a2, 14) ^ gf_mul(a3, 11));
        p[3] = (uint8_t)(gf_mul(a0, 11) ^ gf_mul(a1, 13) ^ gf_mul(a2, 9) ^ gf_mul(a3, 14));
    }
}

void wifi_aes128_encrypt_block(const struct wifi_aes128 *a, const uint8_t in[16],
                               uint8_t out[16]) {
    uint8_t s[16];
    int round;
    if (!a || !in || !out) return;
    for (int i = 0; i < 16; i++) s[i] = in[i];
    add_round_key(s, a->rk);
    for (round = 1; round <= 9; round++) {
        sub_bytes(s);
        shift_rows(s);
        mix_columns(s);
        add_round_key(s, a->rk + 16 * round);
    }
    sub_bytes(s);
    shift_rows(s);
    add_round_key(s, a->rk + 160);
    for (int i = 0; i < 16; i++) out[i] = s[i];
}

void wifi_aes128_decrypt_block(const struct wifi_aes128 *a, const uint8_t in[16],
                               uint8_t out[16]) {
    uint8_t s[16];
    int round;
    if (!a || !in || !out) return;
    for (int i = 0; i < 16; i++) s[i] = in[i];
    add_round_key(s, a->rk + 160);
    for (round = 9; round >= 1; round--) {
        inv_shift_rows(s);
        inv_sub_bytes(s);
        add_round_key(s, a->rk + 16 * round);
        inv_mix_columns(s);
    }
    inv_shift_rows(s);
    inv_sub_bytes(s);
    add_round_key(s, a->rk);
    for (int i = 0; i < 16; i++) out[i] = s[i];
}

/* ---- AES Key Wrap (RFC 3394) ------------------------------------------- */

static void u64_store(uint8_t *p, uint64_t v) {
    for (int i = 7; i >= 0; i--) {
        p[i] = (uint8_t)(v & 0xFFu);
        v >>= 8;
    }
}

static uint64_t u64_load(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | (uint64_t)p[i];
    return v;
}

int wifi_wpa2_wrap(const uint8_t kek[16], const uint8_t *plain, int len,
                   uint8_t *out, int out_cap) {
    struct wifi_aes128 aes;
    uint64_t a = 0xA6A6A6A6A6A6A6A6ull;
    int n, i, j;

    if (!kek || !plain || !out || len < 16 || (len % 8) != 0 || out_cap < len + 8)
        return -1;
    n = len / 8;
    wifi_aes128_setkey(&aes, kek);
    for (i = 0; i < len; i++) out[8 + i] = plain[i];

    for (j = 0; j < 6; j++) {
        for (i = 1; i <= n; i++) {
            uint8_t b[16];
            u64_store(b, a);
            for (int k = 0; k < 8; k++) b[8 + k] = out[8 * i + k];
            wifi_aes128_encrypt_block(&aes, b, b);
            a = u64_load(b) ^ (uint64_t)(n * j + i);
            for (int k = 0; k < 8; k++) out[8 * i + k] = b[8 + k];
        }
    }
    u64_store(out, a);
    return len + 8;
}

int wifi_wpa2_unwrap(const uint8_t kek[16], const uint8_t *wrapped, int len,
                     uint8_t *out, int out_cap) {
    struct wifi_aes128 aes;
    uint64_t a;
    int n, i, j;

    if (!kek || !wrapped || !out || len < 24 || (len % 8) != 0 || out_cap < len - 8)
        return -1;
    n = (len / 8) - 1;
    wifi_aes128_setkey(&aes, kek);
    a = u64_load(wrapped);
    for (i = 1; i <= n; i++)
        for (int k = 0; k < 8; k++) out[8 * (i - 1) + k] = wrapped[8 * i + k];

    for (j = 5; j >= 0; j--) {
        for (i = n; i >= 1; i--) {
            uint8_t b[16];
            u64_store(b, a ^ (uint64_t)(n * j + i));
            for (int k = 0; k < 8; k++) b[8 + k] = out[8 * (i - 1) + k];
            wifi_aes128_decrypt_block(&aes, b, b);
            a = u64_load(b);
            for (int k = 0; k < 8; k++) out[8 * (i - 1) + k] = b[8 + k];
        }
    }
    if (a != 0xA6A6A6A6A6A6A6A6ull) return -1;
    return len - 8;
}
