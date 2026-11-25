/* SPDX-License-Identifier: AGPL-3.0-only */
/* FlairOS Native Bluetooth HCI H4 Transport Driver (Zero-Mock Production Driver) */
#include "bt_hci.h"
#include "../../include/hal.h"

#define HCI_Q_CAP 16
#define HCI_RX_MAX 64

static struct bt_hci_event g_q[HCI_Q_CAP];
static int g_q_head = 0, g_q_tail = 0;
static int g_hci_inited = 0;
static int g_hci_hw_ok = 0;
static int g_hci_powered = 0;
static int g_hci_fault = 0;
static int g_hci_scanning = 0;
static uint16_t g_active_handle = 0x0001u;

static bt_hci_tx_fn g_tx_fn = 0;
static bt_hci_rx_fn g_rx_fn = 0;
static void *g_io_ctx = 0;

static void hci_scpy(char *d, const char *s, int cap) {
    int i = 0;
    while (s && s[i] && i + 1 < cap) { d[i] = s[i]; i++; }
    if (cap > 0) d[i] = 0;
}

static void hci_push(const struct bt_hci_event *ev) {
    int nxt = (g_q_tail + 1) % HCI_Q_CAP;
    if (nxt == g_q_head) return;
    g_q[g_q_tail] = *ev;
    g_q_tail = nxt;
}

static int parse_bd_addr(const char *s, uint8_t bd[6]) {
    if (!s) return -1;
    int bi = 5;
    for (int i = 0; s[i] && bi >= 0;) {
        uint8_t hi = 0, lo = 0;
        char c0 = s[i++], c1 = s[i++];
        if (!c1) return -1;
        hi = (uint8_t)((c0 >= '0' && c0 <= '9') ? c0 - '0' : ((c0 | 32) - 'a' + 10));
        lo = (uint8_t)((c1 >= '0' && c1 <= '9') ? c1 - '0' : ((c1 | 32) - 'a' + 10));
        bd[bi--] = (uint8_t)((hi << 4) | lo);
        if (s[i] == ':') i++;
    }
    return (bi == -1) ? 0 : -1;
}

static void fmt_bd_addr(const uint8_t bd[6], char out[18]) {
    static const char hex[] = "0123456789ABCDEF";
    int o = 0;
    for (int i = 5; i >= 0; i--) {
        out[o++] = hex[(bd[i] >> 4) & 0xFu];
        out[o++] = hex[bd[i] & 0xFu];
        if (i > 0) out[o++] = ':';
    }
    out[o] = 0;
}

static int h4_send_cmd(uint16_t opcode, const uint8_t *params, uint8_t plen) {
    uint8_t pkt[4 + 32];
    if (plen > 32) return -1;
    pkt[0] = BT_H4_CMD;
    pkt[1] = (uint8_t)(opcode & 0xFFu);
    pkt[2] = (uint8_t)((opcode >> 8) & 0xFFu);
    pkt[3] = plen;
    for (uint8_t i = 0; i < plen; i++) pkt[4 + i] = params[i];
    if (g_tx_fn) return g_tx_fn(pkt, (uint16_t)(4u + plen), g_io_ctx);
    /* No hardware HCI UART attached unless registered by platform/board driver */
    return -1;
}

static void h4_poll_rx(void) {
    if (!g_rx_fn) return;
    uint8_t buf[HCI_RX_MAX];
    for (int guard = 0; guard < 16; guard++) {
        uint8_t ptype = 0;
        if (g_rx_fn(&ptype, g_io_ctx) != 1) break;
        if (ptype != BT_H4_EVT) continue;
        uint8_t evt_code = 0, plen = 0;
        if (g_rx_fn(&evt_code, g_io_ctx) != 1) break;
        if (g_rx_fn(&plen, g_io_ctx) != 1) break;
        if (plen > HCI_RX_MAX) plen = HCI_RX_MAX;
        for (uint8_t i = 0; i < plen; i++) {
            if (g_rx_fn(&buf[i], g_io_ctx) != 1) { plen = i; break; }
        }
        if (evt_code == BT_HCI_RAW_CMD_COMPLETE && plen >= 4) {
            uint16_t op = (uint16_t)buf[1] | ((uint16_t)buf[2] << 8);
            uint8_t st = buf[3];
            if (op == BT_HCI_OP_RESET && st == 0) {
                g_hci_hw_ok = 1;
                struct bt_hci_event ev = { BT_HCI_EVT_RESET_OK, 0, 0, 0, 0, "", "" };
                hci_push(&ev);
            }
        } else if (evt_code == BT_HCI_RAW_INQ_RESULT && plen >= 9) {
            struct bt_hci_event ev = { BT_HCI_EVT_INQUIRY, 0, (int8_t)buf[7], buf[8], 0, "", "" };
            fmt_bd_addr(buf + 1, ev.addr);
            if (plen > 9) {
                int nlen = (int)plen - 9;
                if (nlen >= (int)sizeof(ev.name)) nlen = (int)sizeof(ev.name) - 1;
                for (int i = 0; i < nlen; i++) ev.name[i] = (char)buf[9 + i];
                ev.name[nlen] = 0;
            }
            hci_push(&ev);
        } else if (evt_code == BT_HCI_RAW_INQ_COMPLETE) {
            g_hci_scanning = 0;
            struct bt_hci_event ev = { BT_HCI_EVT_INQ_DONE, plen ? buf[0] : 0, 0, 0, 0, "", "" };
            hci_push(&ev);
        } else if (evt_code == BT_HCI_RAW_CONN_COMPLETE && plen >= 9) {
            struct bt_hci_event ev = { BT_HCI_EVT_CONN_OK, buf[0], 0, 0, 0, "", "" };
            ev.handle = (uint16_t)buf[1] | ((uint16_t)buf[2] << 8);
            g_active_handle = ev.handle;
            fmt_bd_addr(buf + 3, ev.addr);
            hci_push(&ev);
        } else if (evt_code == BT_HCI_RAW_DISC_COMPLETE && plen >= 4) {
            struct bt_hci_event ev = { BT_HCI_EVT_DISC_OK, buf[0], 0, 0, 0, "", "" };
            ev.handle = (uint16_t)buf[1] | ((uint16_t)buf[2] << 8);
            hci_push(&ev);
        } else if (evt_code == BT_HCI_RAW_HW_ERROR) {
            g_hci_fault = plen ? buf[0] : 1;
            struct bt_hci_event ev = { BT_HCI_EVT_HW_ERROR, (uint8_t)g_hci_fault, 0, 0, 0, "", "" };
            hci_push(&ev);
        }
    }
}

void bt_hci_set_transport(bt_hci_tx_fn tx, bt_hci_rx_fn rx, void *ctx) {
    g_tx_fn = tx;
    g_rx_fn = rx;
    g_io_ctx = ctx;
}

void bt_hci_init(void) {
    if (g_hci_inited) return;
    g_hci_inited = 1;
    g_q_head = g_q_tail = 0;
    g_hci_hw_ok = 0;
    g_hci_powered = 0;
    g_hci_fault = 0;
    g_hci_scanning = 0;
}

int bt_hci_hw_present(void) { return g_hci_hw_ok; }

int bt_hci_reset(void) {
    bt_hci_init();
    g_q_head = g_q_tail = 0;
    g_hci_fault = 0;
    g_hci_scanning = 0;
    g_hci_hw_ok = 0;
    if (h4_send_cmd(BT_HCI_OP_RESET, 0, 0) != 0) return -1;
    h4_poll_rx();
    return g_hci_hw_ok ? 0 : -1;
}

int bt_hci_set_power(int on) {
    bt_hci_init();
    if (g_hci_fault) return -1;
    if (!on) {
        g_hci_powered = 0;
        g_hci_scanning = 0;
        return 0;
    }
    if (!g_hci_hw_ok && bt_hci_reset() != 0) return -1;
    g_hci_powered = 1;
    return 0;
}

int bt_hci_is_powered(void) { return g_hci_powered; }

int bt_hci_start_inquiry(void) {
    bt_hci_init();
    if (!g_hci_powered || g_hci_fault || !g_hci_hw_ok) return -1;
    /* LAP=0x9E8B33 (GIAC), Inquiry_Length=8 (~10.24s), Num_Responses=8 */
    const uint8_t params[5] = { 0x33u, 0x8Bu, 0x9Eu, 0x08u, 0x08u };
    if (h4_send_cmd(BT_HCI_OP_INQUIRY, params, 5) != 0) return -1;
    g_hci_scanning = 1;
    h4_poll_rx();
    return 0;
}

int bt_hci_stop_inquiry(void) {
    g_hci_scanning = 0;
    if (!g_hci_hw_ok) return 0;
    return h4_send_cmd(BT_HCI_OP_INQUIRY_CANCEL, 0, 0);
}

int bt_hci_create_conn(const char *addr) {
    if (!g_hci_powered || g_hci_fault || !g_hci_hw_ok) return -1;
    uint8_t params[6];
    if (parse_bd_addr(addr, params) != 0) return -1;
    if (h4_send_cmd(BT_HCI_OP_CREATE_CONN, params, 6) != 0) return -1;
    h4_poll_rx();
    return 0;
}

int bt_hci_disconnect(const char *addr) {
    (void)addr;
    if (!g_hci_hw_ok) return -1;
    uint8_t params[3] = {
        (uint8_t)(g_active_handle & 0xFFu),
        (uint8_t)((g_active_handle >> 8) & 0xFFu),
        0x13u /* Remote User Terminated Connection */
    };
    if (h4_send_cmd(BT_HCI_OP_DISCONNECT, params, 3) != 0) return -1;
    h4_poll_rx();
    return 0;
}

void bt_hci_inject_fault(uint8_t err_code) {
    g_hci_fault = err_code ? (int)err_code : 1;
    struct bt_hci_event ev = { BT_HCI_EVT_HW_ERROR, (uint8_t)g_hci_fault, 0, 0, 0, "", "" };
    hci_push(&ev);
}

int bt_hci_read_event(struct bt_hci_event *out_evt) {
    h4_poll_rx();
    if (!out_evt || g_q_head == g_q_tail) return 0;
    *out_evt = g_q[g_q_head];
    g_q_head = (g_q_head + 1) % HCI_Q_CAP;
    return 1;
}
