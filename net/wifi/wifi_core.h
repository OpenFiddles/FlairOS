#ifndef FLAIROS_WIFI_CORE_H
#define FLAIROS_WIFI_CORE_H

#include <stdint.h>
#include "wifi_ieee80211.h"

/* FlairOS WiFi core.
 *
 * The core owns scan results, security classification and the PSK. A chip
 * driver registers ops and feeds received frames in; everything that needs the
 * radio (power, scan, signal, sending frames) is a driver callback. With no
 * driver registered the core reports "no adapter" and refuses to scan or
 * connect rather than inventing a link.
 *
 * Two kinds of driver are supported:
 *
 *  - A full-MAC device sets `connect` and runs association and the WPA2
 *    handshake in firmware. The core hands it the derived PMK and reports the
 *    link when the driver calls wifi_driver_report_link().
 *  - A raw-frame device sets `send_frame` and `install_key` instead. The core
 *    then drives the whole join itself: 802.11 open-system authenticate,
 *    associate with the BSS's security element, the WPA2 4-way handshake and
 *    the group-key handshake, installing the pairwise and group keys only once
 *    the matching step has been verified.
 *
 * The raw path needs a station MAC and 32 octets of supplicant entropy
 * (SNonce) from the platform, because the core has neither a MAC assignment
 * nor a random number generator.
 */
enum wifi_state {
    WIFI_STATE_OFF = 0,
    WIFI_STATE_NO_HW,
    WIFI_STATE_IDLE,
    WIFI_STATE_SCANNING,
    WIFI_STATE_JOINING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_ERROR
};

enum wifi_join_step {
    WIFI_JOIN_IDLE = 0,
    WIFI_JOIN_AUTH,
    WIFI_JOIN_ASSOC,
    WIFI_JOIN_MSG1,
    WIFI_JOIN_MSG3,
    WIFI_JOIN_GROUP,
    WIFI_JOIN_DONE
};

#define WIFI_JOIN_TIMEOUT_MS 1000u
#define WIFI_MAC_LEN 6
#define WIFI_NONCE_LEN 32

struct wifi_drv_ops {
    const char *name;
    int (*power)(void *ctx, int on);
    int (*scan)(void *ctx);
    /* Full-MAC path: security is the wifi_bss.security mask, so an open
       network and a WPA2-PSK network take different paths in the driver. */
    int (*connect)(void *ctx, const uint8_t bssid[6], const char *ssid,
                   const uint8_t pmk[32], uint32_t security);
    int (*disconnect)(void *ctx);
    int (*signal)(void *ctx);   /* 0-100, or -1 when the driver has no value */
    /* Raw-frame path: when both are set the core runs the join itself and
       `connect` is not used. `send_frame` takes a complete 802.11 frame. */
    int (*send_frame)(void *ctx, const uint8_t *frame, int len);
    int (*install_key)(void *ctx, int pairwise, const uint8_t *key, int key_len);
    void *ctx;
};

int wifi_register_adapter(const struct wifi_drv_ops *ops);
void wifi_unregister_adapter(void);
int wifi_present(void);
const char *wifi_adapter_name(void);
int wifi_raw_join(void);

/* Platform inputs for the raw-frame join. Both return -1 for invalid input. */
int wifi_set_mac(const uint8_t mac[WIFI_MAC_LEN]);
int wifi_set_snonce(const uint8_t nonce[WIFI_NONCE_LEN]);

int wifi_power(int on);
int wifi_powered(void);

int wifi_scan_start(void);
int wifi_scan_stop(void);
int wifi_scan_count(void);
const struct wifi_bss *wifi_scan_at(int idx);

/* Driver entry point for every received frame: beacon/probe response (fed to
   the scan table), authenticate/associate responses, deauthenticate,
   disassociate and EAPOL-Key frames (fed to the join). */
int wifi_rx_frame(const uint8_t *frame, int len, int8_t rssi);

/* Kept for callers that only hand beacons in. */
int wifi_rx_beacon(const uint8_t *frame, int len, int8_t rssi);

/* Advance the join clock. A step that has not answered within
   WIFI_JOIN_TIMEOUT_MS fails the join with a recorded reason. */
void wifi_tick(uint32_t now_ms);

/* Connect to a scanned BSS. For a WPA/WPA2-PSK network the PMK is derived from
   the passphrase and the SSID here; open networks pass an all-zero PMK. With a
   raw-frame driver this starts the core-driven join and returns once the
   authenticate request is sent. */
int wifi_connect(int idx, const char *passphrase);
int wifi_disconnect(void);
/* Driver entry point for the full-MAC path: association and handshake done. */
void wifi_driver_report_link(int connected);

int wifi_connected(void);
int wifi_signal(void);
const char *wifi_ssid(void);
int wifi_state(void);
const char *wifi_state_text(void);
const char *wifi_last_error(void);

/* Progress of a raw-frame join (diagnostics and tests). */
int wifi_join_step(void);
const char *wifi_join_step_name(void);
int wifi_assoc_aid(void);

#endif
