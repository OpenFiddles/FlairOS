/* SPDX-License-Identifier: AGPL-3.0-only */
/* FlairOS native Bluetooth HCI H4 transport driver.
 *
 * Wire formats follow the Bluetooth Core Specification:
 *   H4          type(1) then the packet; a command is
 *               [type=0x01, opcode lo, opcode hi, plen, params...]
 *   Cmd Complete (0x0E)  num_cmd_packets(1), opcode(2 LE), return params
 *   Inquiry Result (0x02) num(1), then per response:
 *               bd(6) psrep(1) reserved(1) cod(3 LE) clock_offset(2)
 *   Inquiry Result with RSSI (0x22) appends rssi(1) per response
 *   Remote Name Complete (0x07) status(1), bd(6), name (NUL padded)
 *   Connection Complete (0x03) status(1), handle(2 LE), bd(6), link(1), enc(1)
 *   Disconnection Complete (0x05) status(1), handle(2 LE), reason(1)
 *
 * An Inquiry Result carries no device name; names only arrive through a
 * Remote Name Request, so the core asks for them explicitly.
 *
 * Attaching a transport is a platform/board job. With no transport the driver
 * reports "no controller" instead of pretending an adapter exists.
 */
#include "bt_hci.h"
#include "../../include/hal.h"

#define HCI_Q_CAP 16
#define HCI_RX_MAX 64
#define HCI_INQ_RESP_STRIDE      13
#define HCI_INQ_RESP_STRIDE_RSSI 14

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

static void hci_push(const struct bt_hci_event *ev) {
    int nxt = (g_q_tail + 1) % HCI_Q_CAP;
    if (nxt == g_q_head) return;
    g_q[g_q_tail] = *ev;
    g_q_tail = nxt;
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_bd_addr(const char *s, uint8_t bd[6]) {
    int bi = 5;
    if (!s) return -1;
    for (int i = 0; s[i] && bi >= 0;) {
        int hi = hex_digit(s[i++]);
        int lo = s[i] ? hex_digit(s[i++]) : -1;
        if (hi < 0 || lo < 0) return -1;
        bd[bi--] = (uint8_t)((hi << 4) | lo);
        if (s[i] == ':') i++;
    }
    return (bi == -1 && s[0]) ? 0 : -1;
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

/* Class of Device -> the device classes the UI knows about. A LAN access
   point, a computer or an uncategorised device is BT_DEV_OTHER rather than
   being labelled as something it is not. */
static uint8_t class_from_cod(uint32_t cod) {
    uint8_t major = (uint8_t)BT_COD_MAJOR(cod);
    if (major == BT_COD_MAJOR_AUDIO) return 1u;      /* BT_DEV_AUDIO */
    if (major == BT_COD_MAJOR_PERIPHERAL) return 2u; /* BT_DEV_HID */
    if (major == BT_COD_MAJOR_PHONE) return 4u;      /* BT_DEV_PHONE */
    return 5u;                                       /* BT_DEV_OTHER */
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

/* One Inquiry Result / Inquiry Result with RSSI event can report several
   devices back to back; walk the whole payload at the spec's stride. */
static void handle_inquiry_results(const uint8_t *p, uint8_t plen, int with_rssi) {
    int stride = with_rssi ? HCI_INQ_RESP_STRIDE_RSSI : HCI_INQ_RESP_STRIDE;
    uint8_t n, i;
    if (plen < 1) return;
    n = p[0];
    for (i = 0; i < n; i++) {
        const uint8_t *r = p + 1 + (int)i * stride;
        struct bt_hci_event ev;
        uint32_t cod;
        if (1 + ((int)i + 1) * stride > (int)plen) break;
        cod = (uint32_t)r[8] | ((uint32_t)r[9] << 8) | ((uint32_t)r[10] << 16);
        ev.type = BT_HCI_EVT_INQUIRY;
        ev.status = 0;
        ev.rssi = with_rssi ? (int8_t)r[13] : (int8_t)BT_RSSI_UNKNOWN;
        ev.dev_class = class_from_cod(cod);
        ev.handle = 0;
        fmt_bd_addr(r, ev.addr);
        ev.name[0] = 0;
        hci_push(&ev);
    }
}

/* Remote Name Request Complete: status(1), bd(6), then a NUL-padded name. */
static void handle_name_complete(const uint8_t *p, uint8_t plen) {
    struct bt_hci_event ev;
    int n, i;
    if (plen < 7) return;
    ev.type = BT_HCI_EVT_NAME;
    ev.status = p[0];
    ev.rssi = (int8_t)BT_RSSI_UNKNOWN;
    ev.dev_class = 0;
    ev.handle = 0;
    fmt_bd_addr(p + 1, ev.addr);
    n = (int)plen - 7;
    if (n > (int)sizeof(ev.name) - 1) n = (int)sizeof(ev.name) - 1;
    for (i = 0; i < n && p[7 + i]; i++) ev.name[i] = (char)p[7 + i];
    ev.name[i] = 0;
    hci_push(&ev);
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
                struct bt_hci_event ev = { BT_HCI_EVT_RESET_OK, 0, BT_RSSI_UNKNOWN, 0, 0, "", "" };
                hci_push(&ev);
            }
        } else if (evt_code == BT_HCI_RAW_INQ_RESULT) {
            handle_inquiry_results(buf, plen, 0);
        } else if (evt_code == BT_HCI_RAW_INQ_RESULT_RSSI) {
            handle_inquiry_results(buf, plen, 1);
        } else if (evt_code == BT_HCI_RAW_NAME_COMPLETE) {
            handle_name_complete(buf, plen);
        } else if (evt_code == BT_HCI_RAW_INQ_COMPLETE) {
            g_hci_scanning = 0;
            struct bt_hci_event ev = { BT_HCI_EVT_INQ_DONE, plen ? buf[0] : 0, BT_RSSI_UNKNOWN, 0, 0, "", "" };
            hci_push(&ev);
        } else if (evt_code == BT_HCI_RAW_CONN_COMPLETE && plen >= 9) {
            struct bt_hci_event ev = { BT_HCI_EVT_CONN_OK, buf[0], BT_RSSI_UNKNOWN, 0, 0, "", "" };
            ev.handle = (uint16_t)buf[1] | ((uint16_t)buf[2] << 8);
            g_active_handle = ev.handle;
            fmt_bd_addr(buf + 3, ev.addr);
            hci_push(&ev);
        } else if (evt_code == BT_HCI_RAW_DISC_COMPLETE && plen >= 4) {
            struct bt_hci_event ev = { BT_HCI_EVT_DISC_OK, buf[0], BT_RSSI_UNKNOWN, 0, 0, "", "" };
            ev.handle = (uint16_t)buf[1] | ((uint16_t)buf[2] << 8);
            hci_push(&ev);
        } else if (evt_code == BT_HCI_RAW_HW_ERROR) {
            g_hci_fault = plen ? buf[0] : 1;
            struct bt_hci_event ev = { BT_HCI_EVT_HW_ERROR, (uint8_t)g_hci_fault, BT_RSSI_UNKNOWN, 0, 0, "", "" };
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

int bt_hci_remote_name(const char *addr) {
    uint8_t params[10];
    if (!g_hci_powered || g_hci_fault || !g_hci_hw_ok) return -1;
    if (parse_bd_addr(addr, params) != 0) return -1;
    params[6] = 0x01u;                 /* Page_Scan_Repetition_Mode: R1 */
    params[7] = 0x00u;                 /* reserved */
    params[8] = 0x00u;                 /* Clock_Offset: unknown */
    params[9] = 0x00u;
    if (h4_send_cmd(BT_HCI_OP_REMOTE_NAME_REQ, params, 10) != 0) return -1;
    h4_poll_rx();
    return 0;
}

void bt_hci_inject_fault(uint8_t err_code) {
    g_hci_fault = err_code ? (int)err_code : 1;
    struct bt_hci_event ev = { BT_HCI_EVT_HW_ERROR, (uint8_t)g_hci_fault, BT_RSSI_UNKNOWN, 0, 0, "", "" };
    hci_push(&ev);
}

int bt_hci_read_event(struct bt_hci_event *out_evt) {
    h4_poll_rx();
    if (!out_evt || g_q_head == g_q_tail) return 0;
    *out_evt = g_q[g_q_head];
    g_q_head = (g_q_head + 1) % HCI_Q_CAP;
    return 1;
}
