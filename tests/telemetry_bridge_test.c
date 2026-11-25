#include <stdio.h>
#include <string.h>
#include "../net/bridge/host_bridge.h"
#include "../net/service/net_service.h"

#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

int main(void) {
    net_service_init();

    /* 1. Backwards-compatible STAT v1 parsing */
    host_bridge_inject_line("STAT 73 0 1");
    net_service_sync();
    const struct net_service_state *ns = net_service_state();
    CHECK(ns->battery_pct == 73 && ns->ac_power == 0 && ns->wifi_on == 1);
    CHECK(strcmp(ns->tel_source, "termux-bridge") == 0);

    /* 2. Full STAT2 v2 parsing (matches tests/fixtures/telemetry_sample.json) */
    host_bridge_inject_line(
        "STAT2 84 0 315 1 -54 4 1 3 0 1 1 1|FlairLab-5G|192.168.1.42|T-Mobile|5G|FlairPods Pro|fixture"
    );
    net_service_sync();
    ns = net_service_state();
    CHECK(ns->battery_pct == 84);
    CHECK(ns->ac_power == 0);
    CHECK(ns->battery_temp_c10 == 315);
    CHECK(ns->wifi_on == 1 && ns->wifi_bars == 4 && ns->wifi_rssi == -54);
    CHECK(strcmp(ns->ssid, "FlairLab-5G") == 0);
    CHECK(strcmp(ns->wifi_ip, "192.168.1.42") == 0);
    CHECK(ns->cell_on == 1 && ns->cell_bars == 3 && ns->cell_roaming == 0);
    CHECK(strcmp(ns->carrier, "T-Mobile") == 0);
    CHECK(strcmp(ns->cell_rat, "5G") == 0);
    CHECK(ns->bt_available == 1 && ns->bt_powered == 1 && ns->bt_connected == 1);
    CHECK(strcmp(ns->bt_name, "FlairPods Pro") == 0);
    CHECK(strcmp(ns->tel_source, "fixture") == 0);

    /* 3. Airplane mode must override bridged Wi-Fi/Cell while preserving battery/BT telemetry */
    net_service_toggle_airplane();
    host_bridge_inject_line(
        "STAT2 62 1 290 1 -60 3 1 4 1 1 0 0|OfficeWiFi|10.0.0.9|AT&T|LTE|None|termux-bridge"
    );
    net_service_sync();
    ns = net_service_state();
    CHECK(ns->airplane_mode == 1);
    CHECK(ns->wifi_on == 0 && ns->wifi_bars == 0);
    CHECK(ns->cell_on == 0 && ns->cell_bars == 0);
    CHECK(ns->battery_pct == 62 && ns->ac_power == 1);

    puts("Telemetry v2 bridge host test: all checks passed");
    return 0;
}
