#ifndef FLAIROS_BT_HCI_H
#define FLAIROS_BT_HCI_H

#include <stdint.h>

/* HCI H4 UART Packet Types */
#define BT_H4_CMD 0x01u
#define BT_H4_ACL 0x02u
#define BT_H4_EVT 0x04u

/* Standard Bluetooth SIG HCI Opcodes */
#define BT_HCI_OP_INQUIRY        0x0401u
#define BT_HCI_OP_INQUIRY_CANCEL 0x0402u
#define BT_HCI_OP_CREATE_CONN    0x0405u
#define BT_HCI_OP_DISCONNECT     0x0406u
#define BT_HCI_OP_RESET          0x0C03u

/* Standard HCI Event Codes */
#define BT_HCI_RAW_INQ_RESULT    0x02u
#define BT_HCI_RAW_INQ_COMPLETE  0x01u
#define BT_HCI_RAW_CONN_COMPLETE 0x03u
#define BT_HCI_RAW_DISC_COMPLETE 0x05u
#define BT_HCI_RAW_CMD_COMPLETE  0x0Eu
#define BT_HCI_RAW_HW_ERROR      0x10u

/* Decoded Driver Events */
#define BT_HCI_EVT_NONE       0u
#define BT_HCI_EVT_RESET_OK   1u
#define BT_HCI_EVT_INQUIRY    2u
#define BT_HCI_EVT_INQ_DONE   3u
#define BT_HCI_EVT_CONN_OK    4u
#define BT_HCI_EVT_DISC_OK    5u
#define BT_HCI_EVT_HW_ERROR   6u

struct bt_hci_event {
    uint8_t type;
    uint8_t status; /* 0 = OK, nonzero = HCI error code */
    int8_t rssi;
    uint8_t dev_class; /* 1=Audio, 2=HID, 3=LE, 4=Phone */
    uint16_t handle;
    char addr[18];
    char name[24];
};

typedef int (*bt_hci_tx_fn)(const uint8_t *buf, uint16_t len, void *ctx);
typedef int (*bt_hci_rx_fn)(uint8_t *out_byte, void *ctx);

void bt_hci_set_transport(bt_hci_tx_fn tx, bt_hci_rx_fn rx, void *ctx);
void bt_hci_init(void);
int bt_hci_hw_present(void);
int bt_hci_reset(void);
int bt_hci_set_power(int on);
int bt_hci_is_powered(void);
int bt_hci_start_inquiry(void);
int bt_hci_stop_inquiry(void);
int bt_hci_create_conn(const char *addr);
int bt_hci_disconnect(const char *addr);
void bt_hci_inject_fault(uint8_t err_code);
int bt_hci_read_event(struct bt_hci_event *out_evt);

#endif
