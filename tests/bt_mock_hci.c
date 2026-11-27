/* SPDX-License-Identifier: AGPL-3.0-only */
/* Test-Only H4 HCI Controller Responder (never linked into production FlairOS)
 *
 * The packets follow the Bluetooth Core Specification layout, so the driver is
 * exercised against the real wire format rather than a private one:
 *   - Inquiry Result (0x02) batches two devices with 13-byte responses and no
 *     RSSI, Inquiry Result with RSSI (0x22) batches two more with 14-byte
 *     responses.
 *   - Remote Name Request Complete (0x07) answers each name request, because
 *     inquiry results never carry names.
 */
#include "bt_mock_hci.h"
#include "../drivers/bt/bt_hci.h"
#include <string.h>

#define MOCK_RX_CAP 1024
static uint8_t m_rx[MOCK_RX_CAP];
static int m_head = 0, m_tail = 0;

struct mock_dev {
    uint8_t bd[6];
    uint8_t cod[3];   /* little-endian class of device */
    int8_t rssi;
    int with_rssi;
    const char *name;
};

static const struct mock_dev mock_devs[4] = {
    /* CoD 0x240404: major class audio/video */
    { {0xA4,0xC1,0x38,0x90,0x12,0x01}, {0x04,0x04,0x24}, -128, 0, "FlairPods Pro" },
    /* CoD 0x000540: major class peripheral (HID) */
    { {0xD8,0x3A,0xDD,0x11,0x44,0x02}, {0x40,0x05,0x00}, -128, 0, "FlairKey K1 HID" },
    /* CoD 0x00020C: major class phone */
    { {0xF0,0x99,0x19,0x77,0x00,0x03}, {0x0C,0x02,0x00}, -52, 1, "FlairPhone 12" },
    /* CoD 0x000300: major class LAN/network access point */
    { {0xC4,0x8E,0x8F,0x22,0x18,0x04}, {0x00,0x03,0x00}, -63, 1, "Office AP 5G" }
};

static void push_byte(uint8_t b) {
    int nxt = (m_tail + 1) % MOCK_RX_CAP;
    if (nxt != m_head) { m_rx[m_tail] = b; m_tail = nxt; }
}

static void push_evt(uint8_t code, const uint8_t *payload, uint8_t plen) {
    push_byte(BT_H4_EVT);
    push_byte(code);
    push_byte(plen);
    for (uint8_t i = 0; i < plen; i++) push_byte(payload[i]);
}

static void push_inquiry_result(int with_rssi) {
    uint8_t p[1 + 2 * 15];
    int o = 0, n = 0;
    p[o++] = 2; /* two responses in this event */
    for (int i = 0; i < 4; i++) {
        const struct mock_dev *d = &mock_devs[i];
        if (d->with_rssi != with_rssi) continue;
        memcpy(p + o, d->bd, 6); o += 6;
        p[o++] = 0x01; /* page scan repetition mode */
        p[o++] = 0x00; /* reserved */
        memcpy(p + o, d->cod, 3); o += 3;
        p[o++] = 0x00; /* clock offset */
        p[o++] = 0x00;
        if (with_rssi) p[o++] = (uint8_t)d->rssi;
        n++;
    }
    p[0] = (uint8_t)n;
    push_evt(with_rssi ? BT_HCI_RAW_INQ_RESULT_RSSI : BT_HCI_RAW_INQ_RESULT, p, (uint8_t)o);
}

static int mock_tx(const uint8_t *buf, uint16_t len, void *ctx) {
    (void)ctx;
    if (len < 4 || buf[0] != BT_H4_CMD) return -1;
    uint16_t op = (uint16_t)buf[1] | ((uint16_t)buf[2] << 8);
    if (op == BT_HCI_OP_RESET) {
        uint8_t p[4] = { 1, (uint8_t)(op & 0xFFu), (uint8_t)(op >> 8), 0 };
        push_evt(BT_HCI_RAW_CMD_COMPLETE, p, 4);
    } else if (op == BT_HCI_OP_INQUIRY) {
        push_inquiry_result(0);
        push_inquiry_result(1);
        uint8_t done[1] = { 0 };
        push_evt(BT_HCI_RAW_INQ_COMPLETE, done, 1);
    } else if (op == BT_HCI_OP_REMOTE_NAME_REQ && len >= 10) {
        uint8_t p[7 + 24];
        int o = 0;
        p[o++] = 0; /* status */
        memcpy(p + o, buf + 4, 6); o += 6;
        for (int i = 0; i < 4; i++) {
            if (memcmp(buf + 4, mock_devs[i].bd, 6) == 0) {
                uint8_t nl = (uint8_t)strlen(mock_devs[i].name);
                memcpy(p + o, mock_devs[i].name, nl);
                o += nl;
                break;
            }
        }
        push_evt(BT_HCI_RAW_NAME_COMPLETE, p, (uint8_t)o);
    } else if (op == BT_HCI_OP_CREATE_CONN && len >= 10) {
        uint8_t p[9] = { 0, 0x01u, 0x00u };
        memcpy(p + 3, buf + 4, 6);
        push_evt(BT_HCI_RAW_CONN_COMPLETE, p, 9);
    } else if (op == BT_HCI_OP_DISCONNECT && len >= 7) {
        uint8_t p[4] = { 0, buf[4], buf[5], 0x16u };
        push_evt(BT_HCI_RAW_DISC_COMPLETE, p, 4);
    }
    return 0;
}

static int mock_rx(uint8_t *out_byte, void *ctx) {
    (void)ctx;
    if (m_head == m_tail) return 0;
    *out_byte = m_rx[m_head];
    m_head = (m_head + 1) % MOCK_RX_CAP;
    return 1;
}

void bt_mock_hci_attach(void) {
    m_head = m_tail = 0;
    bt_hci_set_transport(mock_tx, mock_rx, 0);
}
