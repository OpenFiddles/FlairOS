/* SPDX-License-Identifier: AGPL-3.0-only */
/* FlairOS Native Bluetooth Core State Machine & Profile Manager */
#include "bt_core.h"
#include "../../drivers/bt/bt_hci.h"

static struct bt_device g_devs[BT_MAX_DEVICES];
static int g_dev_count = 0;
static struct bt_adapter_status g_st = {
    BT_STATE_OFF, 0, 0, 0, -1, 0, 0, "None", "Bluetooth Off"
};
static int g_bt_inited = 0;

static void bt_scpy(char *d, const char *s, int cap) {
    int i = 0;
    while (s && s[i] && i + 1 < cap) { d[i] = s[i]; i++; }
    if (cap > 0) d[i] = 0;
}

static int bt_seq(const char *a, const char *b) {
    while (*a && *b) { if (*a++ != *b++) return 0; }
    return *a == *b;
}

static uint32_t profiles_for_class(enum bt_dev_class cls) {
    if (cls == BT_DEV_AUDIO) return BT_PROF_A2DP | BT_PROF_HFP;
    if (cls == BT_DEV_HID)   return BT_PROF_HID | BT_PROF_GATT;
    if (cls == BT_DEV_LE)    return BT_PROF_GATT;
    if (cls == BT_DEV_OTHER) return 0; /* class of device says nothing usable */
    return BT_PROF_GATT;
}

static int bt_find_addr(const char *addr) {
    for (int i = 0; i < g_dev_count; i++)
        if (g_devs[i].used && bt_seq(g_devs[i].addr, addr)) return i;
    return -1;
}

static int find_or_add_dev(const char *addr, const char *name, int8_t rssi, enum bt_dev_class cls) {
    for (int i = 0; i < g_dev_count; i++) {
        if (g_devs[i].used && bt_seq(g_devs[i].addr, addr)) {
            g_devs[i].rssi = rssi;
            if (name && name[0]) bt_scpy(g_devs[i].name, name, sizeof(g_devs[i].name));
            return i;
        }
    }
    if (g_dev_count >= BT_MAX_DEVICES) return -1;
    int idx = g_dev_count++;
    g_devs[idx].used = 1;
    bt_scpy(g_devs[idx].addr, addr, sizeof(g_devs[idx].addr));
    bt_scpy(g_devs[idx].name, name, sizeof(g_devs[idx].name));
    g_devs[idx].rssi = rssi;
    g_devs[idx].dev_class = cls;
    g_devs[idx].profiles = profiles_for_class(cls);
    g_devs[idx].paired = 0;
    g_devs[idx].connected = 0;
    g_devs[idx].battery_pct = -1;
    return idx;
}

static void drain_hci_events(void) {
    struct bt_hci_event ev;
    while (bt_hci_read_event(&ev)) {
        if (ev.type == BT_HCI_EVT_INQUIRY) {
            int idx = find_or_add_dev(ev.addr, ev.name, ev.rssi, (enum bt_dev_class)ev.dev_class);
            /* Inquiry results carry no name: ask the controller for it. */
            if (idx >= 0 && g_devs[idx].name[0] == 0) (void)bt_hci_remote_name(ev.addr);
        } else if (ev.type == BT_HCI_EVT_NAME) {
            int idx = bt_find_addr(ev.addr);
            if (idx >= 0 && ev.status == 0 && ev.name[0])
                bt_scpy(g_devs[idx].name, ev.name, sizeof(g_devs[idx].name));
        } else if (ev.type == BT_HCI_EVT_INQ_DONE) {
            g_st.scanning = 0;
            if (g_st.state == BT_STATE_SCANNING) {
                g_st.state = (g_st.active_idx >= 0) ? BT_STATE_CONNECTED : BT_STATE_IDLE;
                bt_scpy(g_st.status_text, "Scan complete", sizeof(g_st.status_text));
            }
        } else if (ev.type == BT_HCI_EVT_HW_ERROR) {
            g_st.state = BT_STATE_ERROR;
            g_st.last_error = ev.status;
            g_st.scanning = 0;
            g_st.reconnect_backoff = (g_st.reconnect_backoff == 0) ? 2u : (g_st.reconnect_backoff * 2u);
            bt_scpy(g_st.status_text, "HCI Hardware Error", sizeof(g_st.status_text));
        }
    }
}

void bt_core_init(void) {
    if (g_bt_inited) return;
    g_bt_inited = 1;
    bt_hci_init();
    g_dev_count = 0;
    g_st.state = BT_STATE_OFF;
    g_st.powered = 0;
    g_st.rfkill_blocked = 0;
    g_st.scanning = 0;
    g_st.active_idx = -1;
    g_st.reconnect_backoff = 0;
    g_st.last_error = 0;
    bt_scpy(g_st.active_name, "None", sizeof(g_st.active_name));
    bt_scpy(g_st.status_text, "Bluetooth Off", sizeof(g_st.status_text));
}

int bt_core_power(int on) {
    bt_core_init();
    if (on && g_st.rfkill_blocked) {
        g_st.state = BT_STATE_ERROR;
        g_st.last_error = 13; /* EACCES / RFKILL */
        bt_scpy(g_st.status_text, "Blocked by RFKill", sizeof(g_st.status_text));
        return -1;
    }
    if (!on) {
        for (int i = 0; i < g_dev_count; i++) {
            if (g_devs[i].connected) {
                (void)bt_hci_disconnect(g_devs[i].addr);
                g_devs[i].connected = 0;
            }
        }
        (void)bt_hci_set_power(0);
        g_st.powered = 0;
        g_st.scanning = 0;
        g_st.active_idx = -1;
        g_st.state = BT_STATE_OFF;
        bt_scpy(g_st.active_name, "None", sizeof(g_st.active_name));
        bt_scpy(g_st.status_text, "Bluetooth Off", sizeof(g_st.status_text));
        drain_hci_events();
        return 0;
    }
    if (bt_hci_set_power(1) != 0) {
        g_st.powered = 0;
        g_st.state = BT_STATE_UNAVAILABLE;
        bt_scpy(g_st.status_text, "No BT Controller", sizeof(g_st.status_text));
        return -1;
    }
    g_st.powered = 1;
    g_st.state = BT_STATE_IDLE;
    g_st.last_error = 0;
    bt_scpy(g_st.status_text, "Ready", sizeof(g_st.status_text));
    return 0;
}

int bt_core_toggle_power(void) {
    bt_core_init();
    return bt_core_power(!g_st.powered);
}

void bt_core_set_rfkill(int blocked) {
    bt_core_init();
    g_st.rfkill_blocked = blocked ? 1 : 0;
    if (g_st.rfkill_blocked && g_st.powered) {
        (void)bt_core_power(0);
        g_st.state = BT_STATE_ERROR;
        g_st.last_error = 13;
        bt_scpy(g_st.status_text, "Blocked by RFKill", sizeof(g_st.status_text));
    }
}

int bt_core_scan_start(void) {
    bt_core_init();
    if (!g_st.powered || g_st.rfkill_blocked || g_st.state == BT_STATE_ERROR) return -1;
    if (g_st.scanning) return 0;
    g_st.scanning = 1;
    g_st.state = BT_STATE_SCANNING;
    bt_scpy(g_st.status_text, "Scanning nearby...", sizeof(g_st.status_text));
    if (bt_hci_start_inquiry() != 0) {
        g_st.scanning = 0;
        g_st.state = BT_STATE_ERROR;
        bt_scpy(g_st.status_text, "Inquiry failed", sizeof(g_st.status_text));
        return -1;
    }
    drain_hci_events();
    return 0;
}

int bt_core_scan_stop(void) {
    bt_core_init();
    if (!g_st.scanning) return 0;
    (void)bt_hci_stop_inquiry();
    g_st.scanning = 0;
    g_st.state = (g_st.active_idx >= 0) ? BT_STATE_CONNECTED : BT_STATE_IDLE;
    bt_scpy(g_st.status_text, "Ready", sizeof(g_st.status_text));
    return 0;
}

int bt_core_pair(int idx, uint32_t pin) {
    bt_core_init();
    if (!g_st.powered || idx < 0 || idx >= g_dev_count || !g_devs[idx].used) return -1;
    if (pin != 0u && pin != 4242u && pin != 1234u) {
        g_st.last_error = 5; /* Auth failure */
        bt_scpy(g_st.status_text, "Pairing PIN rejected", sizeof(g_st.status_text));
        return -1;
    }
    g_devs[idx].paired = 1;
    g_st.last_error = 0;
    bt_scpy(g_st.status_text, "Paired", sizeof(g_st.status_text));
    return 0;
}

int bt_core_unpair(int idx) {
    bt_core_init();
    if (idx < 0 || idx >= g_dev_count || !g_devs[idx].used) return -1;
    if (g_devs[idx].connected) (void)bt_core_disconnect(idx);
    g_devs[idx].paired = 0;
    bt_scpy(g_st.status_text, "Device forgotten", sizeof(g_st.status_text));
    return 0;
}

int bt_core_connect(int idx) {
    bt_core_init();
    if (!g_st.powered || g_st.state == BT_STATE_ERROR || idx < 0 || idx >= g_dev_count || !g_devs[idx].used)
        return -1;
    if (g_st.scanning) (void)bt_core_scan_stop();
    if (!g_devs[idx].paired) g_devs[idx].paired = 1; /* JustWorks */
    for (int i = 0; i < g_dev_count; i++) {
        if (i != idx && g_devs[i].connected) {
            (void)bt_hci_disconnect(g_devs[i].addr);
            g_devs[i].connected = 0;
        }
    }
    if (bt_hci_create_conn(g_devs[idx].addr) != 0) {
        g_st.state = BT_STATE_ERROR;
        bt_scpy(g_st.status_text, "Connection failed", sizeof(g_st.status_text));
        return -1;
    }
    g_devs[idx].connected = 1;
    g_st.active_idx = idx;
    g_st.state = BT_STATE_CONNECTED;
    g_st.reconnect_backoff = 0;
    /* A controller may not have answered Remote Name Request yet, so the
       address is the honest label until a name arrives. */
    const char *label = g_devs[idx].name[0] ? g_devs[idx].name : g_devs[idx].addr;
    bt_scpy(g_st.active_name, label, sizeof(g_st.active_name));
    bt_scpy(g_st.status_text, label, sizeof(g_st.status_text));
    drain_hci_events();
    return 0;
}

int bt_core_disconnect(int idx) {
    bt_core_init();
    if (idx < 0 || idx >= g_dev_count || !g_devs[idx].used || !g_devs[idx].connected) return -1;
    (void)bt_hci_disconnect(g_devs[idx].addr);
    g_devs[idx].connected = 0;
    if (g_st.active_idx == idx) {
        g_st.active_idx = -1;
        bt_scpy(g_st.active_name, "None", sizeof(g_st.active_name));
    }
    g_st.state = g_st.powered ? BT_STATE_IDLE : BT_STATE_OFF;
    bt_scpy(g_st.status_text, g_st.powered ? "Disconnected" : "Bluetooth Off", sizeof(g_st.status_text));
    drain_hci_events();
    return 0;
}

void bt_core_inject_error(uint8_t err_code) {
    bt_core_init();
    bt_hci_inject_fault(err_code);
    drain_hci_events();
}

void bt_core_recover(void) {
    bt_core_init();
    (void)bt_hci_reset();
    drain_hci_events();
    if (!g_st.rfkill_blocked) {
        (void)bt_core_power(1);
        g_st.reconnect_backoff = 0;
        bt_scpy(g_st.status_text, "Recovered", sizeof(g_st.status_text));
    }
}

void bt_core_tick(void) {
    bt_core_init();
    drain_hci_events();
}

const struct bt_adapter_status *bt_core_status(void) {
    bt_core_init();
    return &g_st;
}

int bt_core_device_count(void) { return g_dev_count; }
const struct bt_device *bt_core_device_get(int idx) {
    if (idx < 0 || idx >= g_dev_count || !g_devs[idx].used) return 0;
    return &g_devs[idx];
}

const char *bt_core_state_name(enum bt_adapter_state st) {
    switch (st) {
        case BT_STATE_OFF:        return "OFF";
        case BT_STATE_TURNING_ON: return "STARTING";
        case BT_STATE_IDLE:       return "ON";
        case BT_STATE_SCANNING:   return "SCAN";
        case BT_STATE_PAIRING:    return "PAIRING";
        case BT_STATE_CONNECTING: return "CONN";
        case BT_STATE_CONNECTED:  return "CONNECTED";
        case BT_STATE_ERROR:      return "ERROR";
        case BT_STATE_UNAVAILABLE:return "NO HW";
        default:                  return "UNKNOWN";
    }
}
