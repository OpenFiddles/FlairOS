#include <stdio.h>
#include <string.h>
#include "../net/bt/bt_core.h"
#include "../drivers/bt/bt_hci.h"
#include "bt_mock_hci.h"

#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

int main(void) {
    bt_core_init();
    CHECK(bt_core_status()->state == BT_STATE_OFF);
    /* Without hardware HCI attached, power on must honestly report NO HW */
    CHECK(bt_core_power(1) != 0);
    CHECK(bt_core_status()->state == BT_STATE_UNAVAILABLE);

    /* Attach test-only H4 HCI responder to test full protocol state machine */
    bt_mock_hci_attach();

    /* 1. RFKill blocks power on */
    bt_core_set_rfkill(1);
    CHECK(bt_core_power(1) != 0);
    CHECK(bt_core_status()->state == BT_STATE_ERROR);
    bt_core_set_rfkill(0);
    bt_core_recover();
    CHECK(bt_core_status()->state == BT_STATE_IDLE && bt_core_status()->powered == 1);

    /* 2. Inquiry / Scan reports four devices out of two batched events. The
       class of device decides the class, and the names can only come from the
       Remote Name Request the core issues for each unnamed result. */
    CHECK(bt_core_scan_start() == 0);
    CHECK(bt_core_device_count() == 4);
    const struct bt_device *d0 = bt_core_device_get(0);
    const struct bt_device *d1 = bt_core_device_get(1);
    const struct bt_device *d2 = bt_core_device_get(2);
    const struct bt_device *d3 = bt_core_device_get(3);
    CHECK(d0 != 0 && strcmp(d0->name, "FlairPods Pro") == 0);
    CHECK(d0->dev_class == BT_DEV_AUDIO);
    CHECK((d0->profiles & BT_PROF_A2DP) != 0);
    CHECK(d0->rssi == BT_RSSI_UNKNOWN); /* Inquiry Result (0x02) has no RSSI */
    CHECK(d1 != 0 && strcmp(d1->name, "FlairKey K1 HID") == 0);
    CHECK(d1->dev_class == BT_DEV_HID);
    CHECK(d2 != 0 && strcmp(d2->name, "FlairPhone 12") == 0);
    CHECK(d2->dev_class == BT_DEV_PHONE);
    CHECK(d2->rssi == -52); /* Inquiry Result with RSSI (0x22) */
    CHECK(d3 != 0 && strcmp(d3->name, "Office AP 5G") == 0);
    CHECK(d3->dev_class == BT_DEV_OTHER); /* a LAN access point, not an audio/HID/LE device */
    CHECK(d3->profiles == 0);
    CHECK(d3->rssi == -63);

    /* 3. Pairing PIN validation & Connect/Disconnect */
    CHECK(bt_core_pair(0, 9999u) != 0);
    CHECK(bt_core_pair(0, 4242u) == 0);
    CHECK(bt_core_device_get(0)->paired == 1);
    CHECK(bt_core_connect(0) == 0);
    CHECK(bt_core_status()->state == BT_STATE_CONNECTED);
    CHECK(strcmp(bt_core_status()->active_name, "FlairPods Pro") == 0);

    /* 4. Switching connection disconnects previous device */
    CHECK(bt_core_connect(1) == 0);
    CHECK(bt_core_device_get(0)->connected == 0);
    CHECK(bt_core_device_get(1)->connected == 1);
    CHECK(bt_core_disconnect(1) == 0);
    CHECK(bt_core_status()->state == BT_STATE_IDLE);

    /* 5. Hardware fault injection & HCI reset recovery */
    bt_core_inject_error(0x1F);
    CHECK(bt_core_status()->state == BT_STATE_ERROR && bt_core_status()->reconnect_backoff == 2u);
    bt_core_recover();
    CHECK(bt_core_status()->state == BT_STATE_IDLE && bt_core_status()->reconnect_backoff == 0);

    /* 6. Unpair & Power off */
    CHECK(bt_core_unpair(0) == 0);
    CHECK(bt_core_device_get(0)->paired == 0);
    CHECK(bt_core_power(0) == 0);
    CHECK(bt_core_status()->state == BT_STATE_OFF);

    puts("Native Bluetooth HCI + Core state machine test: all checks passed");
    return 0;
}
