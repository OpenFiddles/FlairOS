#ifndef FLAIROS_WIFI_CORE_H
#define FLAIROS_WIFI_CORE_H

#include <stdint.h>
#include "wifi_ieee80211.h"

/* FlairOS WiFi core.
 *
 * The core owns scan results, security classification and the PSK. A chip
 * driver registers ops and feeds received beacons in; everything that needs
 * the radio (power, scan, association, signal) is a driver callback. With no
 * driver registered the core reports "no adapter" and refuses to scan or
 * connect rather than inventing a link.
 *
 * Association and the 4-way handshake happen in the chip/firmware for a
 * full-MAC device; the driver reports the result with wifi_driver_report_link().
 * The WPA2 key derivation and MIC helpers in wpa2.h are available for chips
 * that leave the handshake to the host.
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

struct wifi_drv_ops {
    const char *name;
    int (*power)(void *ctx, int on);
    int (*scan)(void *ctx);
    /* security is the wifi_bss.security mask, so an open network and a
       WPA2-PSK network take different paths in the driver. */
    int (*connect)(void *ctx, const uint8_t bssid[6], const char *ssid,
                   const uint8_t pmk[32], uint32_t security);
    int (*disconnect)(void *ctx);
    int (*signal)(void *ctx);   /* 0-100, or -1 when the driver has no value */
    void *ctx;
};

int wifi_register_adapter(const struct wifi_drv_ops *ops);
void wifi_unregister_adapter(void);
int wifi_present(void);
const char *wifi_adapter_name(void);

int wifi_power(int on);
int wifi_powered(void);

int wifi_scan_start(void);
int wifi_scan_stop(void);
int wifi_scan_count(void);
const struct wifi_bss *wifi_scan_at(int idx);

/* Driver entry point: hand the core a received beacon/probe response. */
int wifi_rx_beacon(const uint8_t *frame, int len, int8_t rssi);

/* Connect to a scanned BSS. For a WPA/WPA2-PSK network the PMK is derived from
   the passphrase and the SSID here; open networks pass an all-zero PMK. */
int wifi_connect(int idx, const char *passphrase);
int wifi_disconnect(void);
/* Driver entry point: association and the 4-way handshake finished. */
void wifi_driver_report_link(int connected);

int wifi_connected(void);
int wifi_signal(void);
const char *wifi_ssid(void);
int wifi_state(void);
const char *wifi_state_text(void);
const char *wifi_last_error(void);

#endif
