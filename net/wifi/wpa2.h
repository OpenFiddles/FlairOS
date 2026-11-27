#ifndef FLAIROS_WIFI_WPA2_H
#define FLAIROS_WIFI_WPA2_H

#include <stdint.h>

#define WIFI_PMK_LEN 32
#define WIFI_PTK_LEN 48   /* KCK(16) + KEK(16) + TK(16) for CCMP-128 */
#define WIFI_NONCE_LEN 32
#define WIFI_EAPOL_MIC_LEN 16

/* Offset of the MIC inside a complete EAPOL-Key frame: 802.1X header (4),
   descriptor type (1), key info (2), key length (2), replay counter (8),
   nonce (32), IV (16), RSC (8), key ID (8). */
#define WIFI_EAPOL_MIC_OFFSET 81

struct wifi_ptk {
    uint8_t kck[16];
    uint8_t kek[16];
    uint8_t tk[16];
};

/* SHA-1, HMAC-SHA1 and PBKDF2-HMAC-SHA1 are here because WPA2-PSK is defined
   in terms of them (IEEE 802.11-2020, J.4.1 and J.4.2). They are ordinary
   primitives, checked against RFC 3174, RFC 2202 and RFC 6070 in the test. */
void wifi_sha1(const uint8_t *data, int len, uint8_t out[20]);
void wifi_hmac_sha1(const uint8_t *key, int key_len, const uint8_t *data, int len,
                    uint8_t out[20]);
int wifi_pbkdf2_sha1(const uint8_t *pass, int pass_len, const uint8_t *salt, int salt_len,
                     int iterations, uint8_t *out, int out_len);

/* PMK = PBKDF2(HMAC-SHA1, passphrase, SSID, 4096, 256 bits).
   Returns -1 when the passphrase is not 8..63 octets. */
int wifi_wpa2_pmk(const char *passphrase, const uint8_t *ssid, int ssid_len,
                  uint8_t pmk[WIFI_PMK_LEN]);

/* PTK = PRF-512(PMK, "Pairwise key expansion",
                 min(AP,STA) || max(AP,STA) || min(ANonce,SNonce) || max(ANonce,SNonce)).
   The first 48 octets are KCK, KEK and TK for CCMP-128. */
int wifi_wpa2_ptk(const uint8_t pmk[WIFI_PMK_LEN], const uint8_t ap[6], const uint8_t sta[6],
                  const uint8_t anonce[WIFI_NONCE_LEN], const uint8_t snonce[WIFI_NONCE_LEN],
                  struct wifi_ptk *out);

/* MIC = HMAC-SHA1(KCK, EAPOL frame) truncated to 16 octets. The caller passes
   the frame with its MIC field already zeroed. */
void wifi_wpa2_mic(const uint8_t kck[16], const uint8_t *eapol, int len,
                   uint8_t mic[WIFI_EAPOL_MIC_LEN]);
int wifi_wpa2_mic_check(const uint8_t kck[16], const uint8_t *eapol, int len,
                        const uint8_t mic[WIFI_EAPOL_MIC_LEN]);

/* Verify the MIC carried at WIFI_EAPOL_MIC_OFFSET of a real EAPOL-Key frame;
   the stored MIC is treated as zero while hashing. */
int wifi_wpa2_eapol_mic_ok(const uint8_t kck[16], const uint8_t *eapol, int len);

/* AES-128 (FIPS 197) and AES Key Wrap (RFC 3394), needed to unwrap the GTK
   that an authenticator encrypts with the KEK in message 3 and the group-key
   handshake. The S-box is derived from the field definition instead of a
   table, so it is checked against the FIPS test vector rather than trusted. */
struct wifi_aes128 {
    uint8_t rk[176]; /* 11 round keys */
};

void wifi_aes128_setkey(struct wifi_aes128 *a, const uint8_t key[16]);
void wifi_aes128_encrypt_block(const struct wifi_aes128 *a, const uint8_t in[16],
                               uint8_t out[16]);
void wifi_aes128_decrypt_block(const struct wifi_aes128 *a, const uint8_t in[16],
                               uint8_t out[16]);

/* `out_cap` must be at least len + 8 for wrap, len - 8 for unwrap. */
int wifi_wpa2_wrap(const uint8_t kek[16], const uint8_t *plain, int len,
                   uint8_t *out, int out_cap);
int wifi_wpa2_unwrap(const uint8_t kek[16], const uint8_t *wrapped, int len,
                     uint8_t *out, int out_cap);

#endif
