# Wireless: Bluetooth and WiFi

This page describes what the wireless code does today, what it deliberately
refuses to do, and where a board or chip driver plugs in. Anything marked *not
implemented* is not reachable through the current build, tests or UI.

## Bluetooth

`drivers/bt/bt_hci.c` is a real HCI H4 transport driver: it speaks the
Bluetooth Core Specification packet formats over a byte-level
send/receive pair, and `net/bt/bt_core.c` is the adapter state machine and
device table on top of it.

Implemented commands: Reset, Inquiry, Inquiry Cancel, Create Connection,
Disconnect, Remote Name Request.

Decoded events: Command Complete, Inquiry Result (0x02), Inquiry Result with
RSSI (0x22), Remote Name Request Complete (0x07), Connection Complete,
Disconnection Complete, Hardware Error. Inquiry Result events report devices in
batches, and the parser walks the whole payload at the specification's stride
(13 octets for 0x02, 14 for 0x22) rather than assuming one device per packet.
The class of device is decoded into audio, HID, phone or "other"; a LAN access
point is not relabelled as an audio device.

Inquiry results carry no device name. The core issues a Remote Name Request for
each unnamed result, and names arrive through the 0x07 event. When the
controller reports no RSSI, the device keeps `BT_RSSI_UNKNOWN` rather than a
fabricated number.

**Hardware binding.** No board registers a UART transport with
`bt_hci_set_transport()` yet, so the driver honestly reports "No BT
Controller" (`BT_STATE_UNAVAILABLE`) instead of pretending an adapter exists.
The host test attaches a controller whose packets are built to the same wire
layout (`tests/bt_mock_hci.c`); it is test-only and is never linked into the OS.

**Not implemented:** L2CAP, SDP, RFCOMM, A2DP, HFP, HID host, GATT/ATT, LE
scanning and connection, SCO, Secure Simple Pairing and link-key storage.
`bt_core_pair()` only records a paired flag for the UI; it does not exchange
keys.

## WiFi

`net/wifi/` is the 802.11 driver core and supplicant cryptography:

- `wifi_ieee80211.c` parses complete beacon (0x80) and probe response (0x50)
  frames: SSID, DS Parameter Set channel, and the RSN (48) and WPA vendor (221,
  OUI 00:50:F2) information elements. The security mask distinguishes open,
  WEP, WPA-PSK, WPA2-PSK, WPA2-Enterprise, TKIP and CCMP. A truncated element
  ends the walk; nothing is guessed.
- `wpa2.c` implements SHA-1, HMAC-SHA1, PBKDF2-HMAC-SHA1, the 802.11 PRF, the
  WPA2-PSK PMK (`PBKDF2(passphrase, SSID, 4096, 256)`), the PTK
  (KCK/KEK/TK), and EAPOL-Key MIC generation and verification.
- `wifi_core.c` owns the adapter interface, the scan table, the connect state
  and the honest "no adapter" path. A driver registers
  `struct wifi_drv_ops`; beacons arrive through `wifi_rx_beacon()`; `connect`
  derives the PSK and hands it to the driver with the network's security mask;
  the link is reported connected only when the driver says so through
  `wifi_driver_report_link()`.

Open networks are joined without a PSK. WPA2-Enterprise and WEP are refused
with a named reason instead of attempted partially.

**Not implemented:** 802.11 authentication/association frame exchange, the
4-way handshake state machine (the chip is expected to run it for a full-MAC
device), CCMP/TKIP data encryption, 802.1X/EAP, regulatory-domain handling,
power save, and any chip driver (there is no PCI, USB or SDIO WiFi device
driver, and no radio firmware loading yet). No driver means no scan results and
no link, which is what the core reports.

**Desktop integration.** The status bar reads wireless link state through
`link_read()` in `kernel/gfx/status.c`, which calls `net_device_ops.signal()`
for any registered `net_device`. A WiFi driver that registers a net_device with
a `signal` callback therefore shows signal strength without UI changes.

## Tests

`make test` runs both host tests:

- `tests/bt_host_test.c` drives the HCI driver with specification-shaped
  packets: batched inquiry results of both kinds, class-of-device mapping,
  names from Remote Name Request, connection and disconnection, and hardware
  fault recovery.
- `tests/wifi_host_test.c` checks SHA-1 and HMAC-SHA1 against RFC 3174 and
  RFC 2202, PBKDF2 against RFC 6070, the WPA2 PSK for the published
  "IEEE"/"password" value, PTK and MIC values produced independently with
  Python's `hashlib`, beacon parsing for WPA2-PSK/CCMP, WPA-PSK/TKIP,
  enterprise, open, WEP and hidden SSIDs, and the adapter state machine
  including every refusal path.

Neither test proves behaviour on real radios. Nothing here has been run against
a physical Bluetooth controller or WiFi chip.
