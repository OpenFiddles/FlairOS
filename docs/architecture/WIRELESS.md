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
  ends the walk; nothing is guessed. The security element is also kept verbatim
  (`sec_ie`/`sec_ie_len`, RSN preferred) so an association request can echo the
  BSS's own parameters.
- `wpa2.c` implements SHA-1, HMAC-SHA1, PBKDF2-HMAC-SHA1, the 802.11 PRF, the
  WPA2-PSK PMK (`PBKDF2(passphrase, SSID, 4096, 256)`), the PTK
  (KCK/KEK/TK), EAPOL-Key MIC generation and verification, AES-128 (FIPS 197),
  RFC 3394 AES key wrap/unwrap for the group key, and CCM (RFC 3610) plus the
  802.11 CCMP nonce/AAD construction used for data frames.
- `wifi_core.c` owns the adapter interface, the scan table, the connect state
  and the honest "no adapter" path. A driver registers `struct wifi_drv_ops`.
  There are two kinds of driver, and `wifi_raw_join()` says which is in use:

  * Full-MAC drivers set `connect`. The core derives the PSK, hands it over with
    the security mask, and the link is reported connected only when the driver
    calls `wifi_driver_report_link()`.
  * Raw-frame drivers set `send_frame` and `install_key` instead (and need no
    `connect`). The core then runs the whole join itself:

    1. open-system authenticate request/response (status checked),
    2. association request carrying the SSID, supported rates and the BSS's
       RSN element, response's status and AID recorded,
    3. the WPA2 4-way handshake: 1/4 supplies the ANonce, 2/4 carries the
       SNonce and is MIC'd with the derived PTK, 3/4's MIC is verified before
       the pairwise key is installed and 4/4 is sent,
    4. the group-key handshake: 1/2's MIC is verified, the encrypted key data is
       unwrapped with the KEK, the GTK is installed and 2/2 is sent.

  The link is reported connected only after both keys are installed. Every
  EAPOL-Key must carry a replay counter above the last one accepted. A rejected
  authenticate/associate status, a MIC failure (detected here, or reported by
  the authenticator as a deauthenticate reason 14), a replay regression, a
  deauthenticate/disassociate frame, or a step that gets no answer within
  `WIFI_JOIN_TIMEOUT_MS` ends the join: the core clears its nonces, PTK, GTK and
  replay counter, records the reason in `wifi_last_error()`, reports
  disconnected and calls the driver's `disconnect` op. A retry starts from a
  cleared state.

The station MAC and the 32-octet SNonce come from the platform
(`wifi_set_mac()`, `wifi_set_snonce()`), because the core has no MAC assignment
and no random number generator; a raw join without them fails with a named
reason. Open networks are joined without a PSK and need no SNonce.

**Data path.** On a raw-frame link the core also carries traffic once the keys
are in. `wifi_send_data()` builds a ToDS data frame and CCMP-protects it:
with the pairwise TK for a unicast destination, or with the current GTK and
its key index for a group-addressed one. `wifi_rx_frame()` treats a protected
data frame as CCMP input, choosing the key from the CCMP header's key index,
rejecting a packet number that is not above the last accepted one for that key,
verifying the MIC before anything is delivered, and handing the payload to the
callback registered with `wifi_set_data_rx()`. Packet numbers are tracked
separately for the pairwise key and each group key index, in both directions.

A group-key message 1 arriving on a live link is a GTK rekey: its MIC and
replay counter are checked, the new GTK is unwrapped with the KEK, the driver
is asked to install it, the key index is bumped, the new key's packet numbers
start from zero, and the group-key message 2 goes back. The pairwise key and
the link stay up. A bad rekey MIC, a replay regression or a rekey that does not
change the key index tears the link down like any other join failure.

A driver with only `connect` keeps the old behaviour: `wifi_data_ready()` is
false, `wifi_send_data()` refuses, and nothing is decrypted.

WPA2-Enterprise and WEP are refused with a named reason instead of attempted
partially, as are WPA-PSK/TKIP networks on the raw-frame path.

**Not implemented:** TKIP, 802.1X/EAP, fragmentation and aggregation, A4 (WDS)
and QoS transmit (a QoS header can be decrypted, but the core always sends a
plain data frame), block-ack, regulatory-domain handling, power save, and any
chip driver (there is no PCI, USB or SDIO WiFi device driver, and no radio
firmware loading yet). No driver means no scan results and no link, which is
what the core reports.

**Desktop integration.** The status bar reads wireless link state through
`link_read()` in `kernel/gfx/status.c`, which calls `net_device_ops.signal()`
for any registered `net_device`. A WiFi driver that registers a net_device with
a `signal` callback therefore shows signal strength without UI changes.

## Tests

`make test` runs three host tests:

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
- `tests/wifi_join_host_test.c` drives the core-driven join against a mock
  authenticator that speaks the frames back. The PTK (KCK/KEK/TK), the GTK and
  every EAPOL MIC are compared with values computed independently in Python
  (`hashlib` and `cryptography`'s AES), the GTK KDE is wrapped with the KEK, and
  the test asserts the driver is asked to install the pairwise key only after
  3/4 verifies, the group key only after the group handshake, and that the link
  is reported connected only after the last key. Rejected authenticate and
  associate status, MIC failures on 2/4, 3/4, 4/4 and the group message, replay
  regressions, deauthenticate, disassociate and a step timeout each end
  disconnected with the reason recorded and the disconnect op called, and each
  is followed by a successful retry that proves no state leaked. It also checks
  the CCM primitive against RFC 3610 Packet Vector #1, then carries data: the
  outgoing CCMP frames (unicast under the TK, group under the GTK) and the
  incoming ones are compared byte for byte with frames built in Python with
  AESCCM, the per-key packet numbers are asserted, a GTK rekey bumps the key
  index to 2 with traffic continuing under it, and a data MIC failure, a data
  replay regression, a bad rekey MIC and a rekey replay each end disconnected,
  record the reason, call the disconnect op and are followed by a clean retry.

None of the tests proves behaviour on real radios. Nothing here has been run
against a physical Bluetooth controller or WiFi chip, so frame exchange and
key installation are verified against the mock and not against hardware.
