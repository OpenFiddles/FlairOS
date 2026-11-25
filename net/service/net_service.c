/* SPDX-License-Identifier: AGPL-3.0-only */
/* NetService.airp: System Wi-Fi, Cellular 5G, Bluetooth Telemetry & Power Service */
#include "net_service.h"
#include "../bridge/host_bridge.h"

static struct net_service_state g_net = {
    0, 0, 0, 0, 0, 0, 0, 0, -1, 1, 0, 0, 0, 0,
    "/system/priv-app/NetService.airp",
    "", "", "", "", "", "hardware"
};

static void ns_scpy(char *d, const char *s, int cap) {
    int i = 0;
    while (s && s[i] && i + 1 < cap) { d[i] = s[i]; i++; }
    if (cap > 0) d[i] = 0;
}

void net_service_init(void) {
    if (g_net.loaded) return;
    g_net.loaded = 1;
    host_bridge_init();
}

void net_service_sync(void) {
    net_service_init();
    host_bridge_poll();
    const struct host_telemetry *tel = host_bridge_telemetry();
    if (tel->valid) {
        g_net.battery_pct = tel->battery_pct;
        g_net.ac_power = tel->ac_power;
        g_net.battery_temp_c10 = tel->battery_temp_c10;
        g_net.bt_available = tel->bt_available;
        g_net.bt_powered = tel->bt_powered;
        g_net.bt_connected = tel->bt_connected;
        ns_scpy(g_net.bt_name, tel->bt_name, sizeof(g_net.bt_name));
        ns_scpy(g_net.tel_source, tel->source, sizeof(g_net.tel_source));
        if (!g_net.airplane_mode) {
            g_net.wifi_on = tel->wifi_up;
            g_net.wifi_bars = tel->wifi_up ? tel->wifi_bars : 0;
            g_net.wifi_rssi = tel->wifi_rssi;
            ns_scpy(g_net.ssid, tel->wifi_ssid, sizeof(g_net.ssid));
            ns_scpy(g_net.wifi_ip, tel->wifi_ip, sizeof(g_net.wifi_ip));
            g_net.cell_on = tel->cell_up;
            g_net.cell_bars = tel->cell_up ? tel->cell_bars : 0;
            g_net.cell_roaming = tel->cell_roaming;
            ns_scpy(g_net.carrier, tel->cell_carrier, sizeof(g_net.carrier));
            ns_scpy(g_net.cell_rat, tel->cell_rat, sizeof(g_net.cell_rat));
        }
    }
}

void net_service_toggle_wifi(void) {
    if (g_net.airplane_mode) return;
    const struct host_telemetry *tel = host_bridge_telemetry();
    if (!tel->valid && !tel->wifi_up) {
        g_net.wifi_on = 0;
        g_net.wifi_bars = 0;
        return;
    }
    g_net.wifi_on = !g_net.wifi_on;
    g_net.wifi_bars = (g_net.wifi_on && tel->wifi_up) ? tel->wifi_bars : 0;
}

void net_service_toggle_cell(void) {
    if (g_net.airplane_mode) return;
    const struct host_telemetry *tel = host_bridge_telemetry();
    if (!tel->valid && !tel->cell_up) {
        g_net.cell_on = 0;
        g_net.cell_bars = 0;
        return;
    }
    g_net.cell_on = !g_net.cell_on;
    g_net.cell_bars = (g_net.cell_on && tel->cell_up) ? tel->cell_bars : 0;
}

void net_service_toggle_airplane(void) {
    g_net.airplane_mode = !g_net.airplane_mode;
    if (g_net.airplane_mode) {
        g_net.wifi_on = 0; g_net.wifi_bars = 0;
        g_net.cell_on = 0; g_net.cell_bars = 0;
    } else {
        const struct host_telemetry *tel = host_bridge_telemetry();
        g_net.wifi_on = (tel->valid && tel->wifi_up) ? 1 : 0;
        g_net.wifi_bars = g_net.wifi_on ? tel->wifi_bars : 0;
        g_net.cell_on = (tel->valid && tel->cell_up) ? 1 : 0;
        g_net.cell_bars = g_net.cell_on ? tel->cell_bars : 0;
    }
}

const struct net_service_state *net_service_state(void) {
    net_service_init();
    return &g_net;
}
