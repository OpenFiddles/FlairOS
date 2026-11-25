#ifndef FLAIROS_TELEMETRY_CONTRACT_H
#define FLAIROS_TELEMETRY_CONTRACT_H

#include <stdint.h>

enum cc_subview {
    CC_VIEW_ROOT = 0,
    CC_VIEW_WIFI = 1,
    CC_VIEW_CELL = 2,
    CC_VIEW_BT   = 3
};

struct flair_telemetry_v2 {
    int valid;
    int battery_pct;
    int ac_power;
    int battery_temp_c10;
    int wifi_up;
    int wifi_rssi;
    int wifi_bars;
    int cell_up;
    int cell_bars;
    int cell_roaming;
    int bt_available;
    int bt_powered;
    int bt_connected;
    char wifi_ssid[32];
    char wifi_ip[24];
    char cell_carrier[24];
    char cell_rat[12];
    char bt_name[24];
    char source[24];
};

#endif
