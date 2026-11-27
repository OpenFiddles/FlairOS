#ifndef FLAIROS_BT_CORE_H
#define FLAIROS_BT_CORE_H

#include <stdint.h>

#define BT_MAX_DEVICES 12

#define BT_PROF_A2DP (1u << 0)
#define BT_PROF_HFP  (1u << 1)
#define BT_PROF_HID  (1u << 2)
#define BT_PROF_GATT (1u << 3)

enum bt_adapter_state {
    BT_STATE_OFF        = 0,
    BT_STATE_TURNING_ON = 1,
    BT_STATE_IDLE       = 2,
    BT_STATE_SCANNING   = 3,
    BT_STATE_PAIRING    = 4,
    BT_STATE_CONNECTING = 5,
    BT_STATE_CONNECTED   = 6,
    BT_STATE_ERROR       = 7,
    BT_STATE_UNAVAILABLE = 8
};

enum bt_dev_class {
    BT_DEV_AUDIO = 1,
    BT_DEV_HID   = 2,
    BT_DEV_LE    = 3,
    BT_DEV_PHONE = 4,
    BT_DEV_OTHER = 5
};

struct bt_device {
    int used;
    char addr[18];
    char name[24];
    int8_t rssi;
    enum bt_dev_class dev_class;
    uint32_t profiles;
    int paired;
    int connected;
    int battery_pct;
};

struct bt_adapter_status {
    enum bt_adapter_state state;
    int powered;
    int rfkill_blocked;
    int scanning;
    int active_idx;
    uint32_t reconnect_backoff;
    uint8_t last_error;
    char active_name[24];
    char status_text[48];
};

void bt_core_init(void);
int bt_core_power(int on);
int bt_core_toggle_power(void);
void bt_core_set_rfkill(int blocked);
int bt_core_scan_start(void);
int bt_core_scan_stop(void);
int bt_core_pair(int idx, uint32_t pin);
int bt_core_unpair(int idx);
int bt_core_connect(int idx);
int bt_core_disconnect(int idx);
void bt_core_inject_error(uint8_t err_code);
void bt_core_recover(void);
void bt_core_tick(void);

const struct bt_adapter_status *bt_core_status(void);
int bt_core_device_count(void);
const struct bt_device *bt_core_device_get(int idx);
const char *bt_core_state_name(enum bt_adapter_state st);

#endif
