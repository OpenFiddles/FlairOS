/* SPDX-License-Identifier: AGPL-3.0-only */
/* Test-Only H4 HCI Controller Responder (never linked into production FlairOS) */
#include "bt_mock_hci.h"
#include "../drivers/bt/bt_hci.h"
#include <string.h>

#define MOCK_RX_CAP 1024
static uint8_t m_rx[MOCK_RX_CAP];
static int m_head = 0, m_tail = 0;

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

static int mock_tx(const uint8_t *buf, uint16_t len, void *ctx) {
    (void)ctx;
    if (len < 4 || buf[0] != BT_H4_CMD) return -1;
    uint16_t op = (uint16_t)buf[1] | ((uint16_t)buf[2] << 8);
    if (op == BT_HCI_OP_RESET) {
        uint8_t p[4] = { 1, (uint8_t)(op & 0xFFu), (uint8_t)(op >> 8), 0 };
        push_evt(BT_HCI_RAW_CMD_COMPLETE, p, 4);
    } else if (op == BT_HCI_OP_INQUIRY) {
        static const struct { uint8_t bd[6]; int8_t rssi; uint8_t cls; const char *name; } devs[4] = {
            { {0x01,0x12,0x90,0x38,0xC1,0xA4}, -48, 1, "FlairPods Pro" },
            { {0x02,0x44,0x11,0xDD,0x3A,0xD8}, -56, 2, "FlairKey K1 HID" },
            { {0x03,0x00,0x77,0x19,0x99,0xF0}, -63, 1, "LivingRoom Bar" },
            { {0x04,0x18,0x22,0x8F,0x8E,0xC4}, -52, 2, "FlairRemote TV" }
        };
        for (int i = 0; i < 4; i++) {
            uint8_t p[36];
            p[0] = 1;
            memcpy(p + 1, devs[i].bd, 6);
            p[7] = (uint8_t)devs[i].rssi;
            p[8] = devs[i].cls;
            uint8_t nl = (uint8_t)strlen(devs[i].name);
            memcpy(p + 9, devs[i].name, nl);
            push_evt(BT_HCI_RAW_INQ_RESULT, p, (uint8_t)(9u + nl));
        }
        uint8_t done[1] = { 0 };
        push_evt(BT_HCI_RAW_INQ_COMPLETE, done, 1);
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
