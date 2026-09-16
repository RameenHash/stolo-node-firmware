# Stolo Control Protocol (SCP), version 1

SCP is how the Stolo app and `Tools/stolo_node_tool.py` configure a Stolo
Node beyond what the RNode KISS host protocol can express: who controls
the node, its WiFi, its Bluetooth pairing window, and radio changes that
are checked against the band the provisioning ROM says the radio was built
for. It is implemented in `StoloProtocol.h`, compiled in only with
`-DSTOLO_BUILD=1`, and is part of this GPL-3.0 firmware; the app and the
tool speak it as separate programs over a documented interface.

## Transports

| Transport | Carrier | Status |
|---|---|---|
| USB serial | KISS command byte `0x7A` (`CMD_STOLO`), the payload escaped like any KISS frame | implemented |
| BLE | a dedicated Stolo Control Service (CTRL / EVENT / BULK characteristics) | planned |
| WiFi | a control port beside the KISS TCP port | planned |

A stock RNode host never sees SCP: nothing is sent on `0x7A` until the
host has said HELLO, and stock RNS discards an unknown command byte to the
next `FEND`. The KISS byte stream carrying LoRa traffic is untouched.

## Frame

Inside the KISS payload: `[ver = 0x01][type][seq][body…]`. A reply carries
the request's `type | 0x80` and the same `seq`. Integers are big-endian.
Bodies that set things are TLV: `[tag][len][value]`; unknown tags are
ignored so an older node tolerates a newer host. An error is type `0x7F`:
`[code][message…]`.

| Error | Meaning |
|---|---|
| 1 | bad request (malformed, out of hardware range) |
| 2 | unauthorized (no owner enrolled, or this session did not authenticate) |
| 3 | out of band (outside the model's band plan — the write was NOT applied) |
| 4 | not supported on this board / SCP version |
| 5 | the config store could not be written |
| 6 | enrollment closed (the node is owned; open the boot-time window) |

## Session and authorization

- **HELLO (0x01)** → `[fw_len][fw…][rnode_maj][rnode_min][node_pub 32][flags][attest][owner_epoch u32][nonce 16][source]`.
  Flags: bit0 owner enrolled, bit1 this session authorized, bit2 enroll window open, bit3 config store OK. `attest` is 0 (advisory) until milestone F8. `source` is 0 USB / 1 BLE / 2 WiFi. HELLO issues a fresh nonce and clears authorization.
- **AUTH (0x02)** body: Ed25519 signature (64) by the owner key over `"stolo-auth-v1" || nonce || owner_epoch(u32be)`. Reply `[ok][role]`. Nonces are single-use.
- **ENROLL (0x03)** body: `owner_pub[32] || sig[64]`, the signature by the new owner key over `"stolo-enroll-v1" || node_pub || owner_pub` (proof of possession). Allowed when the node has **no owner** (factory-unowned), when the **boot-time enroll window** is open (button held ≥ 3 s while power is applied → 60 s), or from a session the **current owner** authenticated (a hand-over). Reply `[ok][owner_epoch u32]`. Enrolling bumps the epoch and clears the session.
- **FORGET_OWNER (0x04)** (authorized): the node becomes unowned; epoch bumps.

Everything that changes state (`SET_*`, `FORGET_OWNER`, and `ENROLL` on an owned node outside its window) requires an authenticated session.

## Radio

- **GET_RADIO (0x10)** → `freq u32, bw u32, sf u8, cr u8, txp u8, st_alock u16, lt_alock u16, radio_on u8, model u8, band_lo u32, band_hi u32, band_max_txp u8`. Airtime limits are hundredths of a percent. `band_*` are 0 when the model is not in the table.
- **SET_RADIO (0x11)** TLV: 1 freq u32, 2 bw u32, 3 sf u8, 4 cr u8, 5 txp u8, 6 st_alock u16, 7 lt_alock u16, 8 state u8. Only the fields present are judged, first against hardware ranges and then against the model's band (error 3, nothing applied); then applied through the same paths the legacy commands use, so every attached client sees the usual echoes. Reply: the GET_RADIO body, type `0x91`.

Band table (from `rnodeconf`): T-Beam Supreme `0xDB` 420–520 MHz / 22 dBm, `0xDC` 850–950 MHz / 22 dBm; T-Beam `0xE4`/`0xE9` at 17 dBm; T3S3 `0xA1`/`0xA6` at 22 and `0xA5`/`0xAA` at 17; RNode NG21 `0xA2`/`0xA7` at 17.

## WiFi

- **GET_WIFI (0x20)** → `supported u8, mode u8 (0 off, 1 station, 2 access point), channel u8, ssid_len, ssid…, psk_set u8, state u8 (0 off, 1 on, 2 connected), ip u32`. The passphrase is never returned.
- **SET_WIFI (0x21)** TLV: 1 mode, 2 ssid (≤ 32 bytes), 3 psk (≤ 32), 4 channel 1–14. Persists to the same EEPROM fields `rnodeconf` uses and (re)starts the WiFi remote. Reply: GET_WIFI body, type `0xA1`.

## Bluetooth

- **GET_BT (0x30)** → `supported u8, bt_state u8, bonds u8, pairing_open u8, window_s u16, enabled u8`.
- **SET_BT (0x31)** TLV: 1 pairing on/off, 2 debond-all, 3 window seconds (10–600, persisted), 4 BLE enabled on/off. Reply: GET_BT body, type `0xB1`.

## Faults

- **GET_FAULTS (0x40)** → `[count][reset reasons, oldest first]` (ESP-IDF `esp_reset_reason` values), the last eight boots.

## Legacy KISS and guests

Bytes entering the node are tagged with their source (USB, BLE, WiFi). A
WiFi session that has not authenticated is a **guest**: it may read the
radio, but a legacy KISS command that would change it is refused — for
the radio parameters by echoing the *current* value, so a stock RNS on the
guest side fails its own validation cleanly ("frequency mismatch") rather
than seeing silence, which its validator would pass; anything else that
changes state is dropped. USB and a bonded BLE phone are physical presence
and keep upstream's behaviour.

## Config store

`StoloStore.h`: one versioned blob (`schema 1`: node key pair, owner
public key + epoch, pairing window) in the ESP32 NVS namespace `stolo`,
written to alternating slots with a CRC-32 and an activation record. The
upstream EEPROM image (provisioning, radio and WiFi config) is never
touched, and survives a firmware flash — as does the store.
