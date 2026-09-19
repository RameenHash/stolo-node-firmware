# Stolo Control Protocol (SCP), version 2

SCP is how the Stolo app and `Tools/stolo_node_tool.py` configure a Stolo
Node beyond what the RNode KISS host protocol can express: who controls
the node, its WiFi, its Bluetooth pairing window, and radio changes that
are checked against the band the provisioning ROM says the radio was built
for. It is implemented in `StoloProtocol.h`, compiled in only with
`-DSTOLO_BUILD=1`, and is part of this GPL-3.0 firmware; the app and the
tool speak it as separate programs over a documented interface.

Version 2 (2026-09-19) replaces version 1 after the authority review: a
v1 client is answered `not supported` ("SCP version 2 required").

## The authority contract

**A Bluetooth bond (or a USB cable) grants access. The owner key grants
configuration authority.** Legacy KISS and SCP enforce one policy,
default-deny, on sensitive reads as well as mutations.

| Role | Who | May |
|---|---|---|
| **Owner** | A session that completed AUTH on *this* connection, for the *current* ownership generation | Everything. |
| **Compat** | A factory-unowned node with a physically present host: USB, or a BLE link that is encrypted and bonded. Never WiFi. Ends at the first ENROLL. | Upstream behaviour, so a stock client can configure a brand-new radio. |
| **Guest** | Everyone else, including every bonded phone on an owned node and every USB host on an owned node | Data frames; SCP HELLO, GET_RADIO, GET_FAULTS; legacy telemetry and identity reads (`CMD_STAT_*`, `FW_VERSION`, `PLATFORM`, `MCU`, `BOARD`, `DETECT`, `READY`, `HASHES`, `DEV_HASH`, `DEV_SIG`, `RANDOM`, `BLINK`). A legacy radio-parameter write is answered with the **current** value (a stock RNS then fails its own validation cleanly). Everything else is dropped — including `CFG_READ` and `ROM_READ`, which carry the WiFi credential. |

A session is bound to one connection. It ends, and every authority with
it, on: BLE disconnect, WiFi client close or read timeout, a change of
input source, the host's `CMD_LEAVE` or `CMD_RESET`, ten minutes of
silence from an authenticated session, a second HELLO, and any change of
ownership (ENROLL, FORGET_OWNER, or a store commit that moves the epoch).

**Physical recovery** (replacing the owner of an owned node) is the
boot-time enrollment window: hold the button while power is applied
(≥ 3 s) for a 60 s window. F8 makes the window visible on the display. A
successful ENROLL from any path wipes the previous owner's BLE bonds. USB
attachment alone authorizes nothing on an owned node.

**Store recovery.** If the config store cannot be read (corrupt slots, a
schema this firmware does not know, or an ownership rollback below the
recorded epoch floor) the node reports `store_ok = 0` in HELLO and refuses
every configuration change with error 7. It never mints a fresh unowned
identity over a store it could not understand.

## Transports

| Transport | Carrier | Status |
|---|---|---|
| USB serial | KISS command byte `0x7A` (`CMD_STOLO`), the payload escaped like any KISS frame | implemented |
| BLE | a dedicated Stolo Control Service (CTRL / EVENT / BULK characteristics) | planned |
| WiFi | a control port beside the KISS TCP port, over an authenticated secure channel | planned |

A stock RNode host never sees SCP: nothing is sent on `0x7A` until the
host has said HELLO, and stock RNS discards an unknown command byte to the
next `FEND`. The KISS byte stream carrying LoRa traffic is untouched.

## Frame

Inside the KISS payload: `[ver = 0x02][type][seq][body…]`. A reply carries
the request's `type | 0x80` and the same `seq`. Integers are big-endian.
Bodies that set things are TLV: `[tag][len][value]`; the body must be a
whole number of complete TLVs (a truncated entry or trailing bytes reject
the whole request, nothing applied). Unknown tags are ignored and reported
as not applied. An error is type `0x7F`: `[code][message…]`.

| Error | Meaning |
|---|---|
| 1 | bad request (malformed, out of hardware range) — nothing was applied |
| 2 | unauthorized (no owner enrolled, no live nonce, or this session is not the owner) |
| 3 | out of band (outside the model's band plan — the write was NOT applied) |
| 4 | not supported on this board / SCP version |
| 5 | the config store could not be written — nothing changed |
| 6 | enrollment closed (the node is owned; open the boot-time window) |
| 7 | store unreadable: the node is in recovery and refuses configuration |

## Session and authorization

- **HELLO (0x01)** → `[fw_len][fw…][rnode_maj][rnode_min][node_pub 32][flags][attest][owner_epoch u32][nonce 16][source]`.
  Flags: bit0 owner enrolled, bit1 this session is the owner, bit2 enroll window open, bit3 config store OK, bit4 compat mode (unowned + present host). `attest` is 0 (advisory) until F8. `source` is 0 USB / 1 BLE / 2 WiFi. HELLO starts a session on this connection: a fresh single-use nonce and no authority.
- **AUTH (0x02)** body: Ed25519 signature (64) by the owner key over
  `"stolo-auth-v2" || node_pub[32] || nonce[16] || owner_epoch(u32be) || source(u8)`.
  `node_pub` stops a challenge for node A being answered for node B under the same owner key; `source` binds the answer to the carrier it was issued on. Reply `[ok][role]` and, on success, a fresh `nonce[16]` so a following ENROLL (hand-over) needs no HELLO. The signed nonce is spent either way; after a rejected AUTH there is no live nonce until the next HELLO.
- **ENROLL (0x03)** body: `owner_pub[32] || sig[64]`, the signature by the new owner key over
  `"stolo-enroll-v2" || node_pub[32] || owner_pub[32] || nonce[16] || owner_epoch(u32be)`
  — a fresh proof of possession that cannot be replayed later. Allowed when the node is **unowned and the host is present** (compat), inside the **boot-time window**, or from the **current owner's session** (a hand-over). The new state is committed to the store *before* it becomes live; on a store failure nothing changes. Success bumps the epoch, wipes BLE bonds and ends every session. Reply `[ok][owner_epoch u32]`.
- **FORGET_OWNER (0x04)** (owner): the node becomes unowned; epoch bumps; bonds are wiped; the session ends.

Everything that changes state requires the owner (or compat on an unowned node) **and** a readable store.

## Radio

- **GET_RADIO (0x10)** (any session) → `freq u32, bw u32, sf u8, cr u8, txp u8, st_alock u16, lt_alock u16, radio_on u8, model u8, band_lo u32, band_hi u32, band_max_txp u8`. Airtime limits are hundredths of a percent. `band_*` are 0 when the model is not in the table.
- **SET_RADIO (0x11)** TLV: 1 freq u32, 2 bw u32, 3 sf u8, 4 cr u8, 5 txp u8, 6 st_alock u16, 7 lt_alock u16, 8 state u8. Only the fields present are judged, first against hardware ranges and then against the model's band (error 3, nothing applied); then applied through the same paths the legacy commands use, so every attached client sees the usual echoes. Reply: the GET_RADIO body followed by an `applied` byte (bit `tag-1` set for each tag that was applied), type `0x91`.

**One radio policy.** The model band table (from `rnodeconf`: T-Beam Supreme `0xDB` 420–520 MHz / 22 dBm, `0xDC` 850–950 MHz / 22 dBm; T-Beam `0xE4`/`0xE9` at 17 dBm; T3S3 `0xA1`/`0xA6` at 22 and `0xA5`/`0xAA` at 17; RNode NG21 `0xA2`/`0xA7` at 17) is consulted by SCP SET_RADIO, by the legacy `CMD_FREQUENCY` and `CMD_TXPOWER` handlers (a refused write echoes the unchanged value), and by `startRadio()`, which refuses to enable the radio while the effective frequency or power is outside the band — whichever path wrote it, including the saved boot configuration. A model that is not in the table gets no frequency or power writes at all.

## WiFi

- **GET_WIFI (0x20)** (owner / compat) → `supported u8, mode u8 (0 off, 1 station, 2 access point), channel u8, ssid_len, ssid…, psk_set u8, state u8 (0 off, 1 on, 2 connected), ip u32`. The passphrase is never returned.
- **SET_WIFI (0x21)** TLV: 1 mode, 2 ssid (≤ 32 bytes), 3 psk (≤ 32), 4 channel 1–14. The whole request is parsed and validated first; then the EEPROM fields `rnodeconf` uses are written; the reply is sent; and the WiFi remote is (re)started **last**, because that restart can drop the very connection carrying the request. Reply: GET_WIFI body + `applied`, type `0xA1`.

## Bluetooth

- **GET_BT (0x30)** (owner / compat) → `supported u8, bt_state u8, bonds u8, pairing_open u8, window_s u16, enabled u8`.
- **SET_BT (0x31)** TLV: 1 pairing on/off, 2 debond-all, 3 window seconds (10–600, persisted), 4 BLE enabled on/off. Parsed and validated first; the window is committed to the store; the pairing flag is applied; the reply is sent; and the link-ending actions — disable, debond — happen **last**. Reply: GET_BT body + `applied`, type `0xB1`.

## Faults

- **GET_FAULTS (0x40)** (any session) → `[count][reset reasons, oldest first]` (ESP-IDF `esp_reset_reason` values), the last eight boots.

## Config store

`StoloStore.h`: one versioned blob (`schema 1`: node key pair, owner public
key + epoch, pairing window) in the ESP32 NVS namespace `stolo`, written to
alternating slots with a CRC-32 and an activation record, plus an
**ownership epoch floor** kept outside the slots. Every change is prepared
on a copy and committed with `stolo_store_commit()`: written to the inactive
slot, read back, activated, the floor raised, and only then made live. A
slot whose epoch is below the floor is a rollback and is never loaded. The
upstream EEPROM image (provisioning, radio and WiFi config) is never
touched, and survives a firmware flash — as does the store. The node key is
generated at first boot with the bootloader entropy source enabled around
the fill, because no RF subsystem is running yet.

## What is verified, and how

Host tests in `Tests/host/` (`make test-host`) drive the **real**
`StoloProtocol.h` dispatcher and `StoloStore.h` with hardware, storage and
Ed25519 stubs: default-deny for guests over KISS and SCP (including the
credential-carrying dumps), compat mode on an unowned node, session
binding to connection and epoch, the v2 transcripts, nonce freshness,
transactional SET_*, store failure leaving RAM untouched, the single radio
policy, and store recovery. That is **software verified**. Physical
disconnect and reconnect, BLE security state, timing and power failure
remain **device qualified** on the bench and are tracked on the roadmap's
testing queue.
