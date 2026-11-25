#ifndef FLAIROS_NET_SERVICE_H
#define FLAIROS_NET_SERVICE_H

struct net_service_state {
    int loaded;
    int wifi_on;
    int wifi_bars;
    int wifi_rssi;
    int cell_on;
    int cell_bars;
    int cell_roaming;
    int airplane_mode;
    int battery_pct;
    int ac_power;
    int battery_temp_c10;
    int bt_available;
    int bt_powered;
    int bt_connected;
    char pkg_path[48];
    char carrier[24];
    char cell_rat[12];
    char ssid[32];
    char wifi_ip[24];
    char bt_name[24];
    char tel_source[24];
};

void net_service_init(void);
void net_service_sync(void);
void net_service_toggle_wifi(void);
void net_service_toggle_cell(void);
void net_service_toggle_airplane(void);
const struct net_service_state *net_service_state(void);

#endif
