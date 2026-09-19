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
| **Compat** | Decided once-only factory compat, enabled by default (`STOLO_ENABLE_FACTORY_COMPAT=1`): readable store, unowned, persistent `ever_enrolled` false, and USB or encrypted/bonded BLE. Never WiFi. Ends at the first ENROLL. | Upstream behaviour, so a stock client can configure a brand-new radio. |
| **Guest** | Everyone else, including every bonded phone on an owned node and every USB host on an owned node | Data frames; SCP HELLO, GET_RADIO, GET_FAULTS; legacy telemetry and identity reads (`CMD_STAT_*`, `FW_VERSION`, `PLATFORM`, `MCU`, `BOARD`, `DETECT`, `READY`, `HASHES`, `DEV_HASH`, `RANDOM`, `BLINK`). A legacy radio-parameter write is answered with the **current** value (a stock RNS then fails its own validation cleanly). Everything else is dropped — including `CFG_READ` and `ROM_READ`, which carry the WiFi credential. |

Connection boundaries abort both KISS parsers, their SCP receive buffers,
and the unfinished NUS/serial data frame and shared serial input FIFO.
BLE callbacks clear the CTRL queue and post a boundary flag: authority is
revoked immediately and parser/session teardown happens in the firmware loop.
CTRL HELLO and SCP LEAVE reset authority without truncating an in-flight NUS
frame; legacy payload/mutation checks still use the resulting role. Source changes
flush queued old-source bytes before admitting new-source bytes. Legacy
handlers re-check the role at payload processing and mutation boundaries.
A session ends on BLE disconnect, WiFi close/timeout, input-source change,
`CMD_LEAVE` or SCP LEAVE, an authorized `CMD_RESET`, a second HELLO, or ownership change.
Idle expiry is 30 seconds for USB and ten minutes for BLE, checked before
incoming traffic refreshes activity. Hosts keeping USB authority alive must
send a request such as GET_RADIO every 10 seconds; HELLO clears authority.
The Python tool sends and flushes CMD_LEAVE before closing the serial handle.

USB-CDC: TinyUSB builds register CONNECTED/DISCONNECTED and DTR-low line-state
events. The default ESP32-S3 hardware CDC/JTAG build registers CONNECTED and
BUS_RESET, but its API has no process-close event; the 30-second idle fallback
therefore still permits a close/reopen inheritance interval if LEAVE was not
sent and the driver reports no boundary. **Bench qualification must establish
which events macOS/Linux/Windows emit for close/open, DTR changes, unplug and
reset; immediate process-bound authority is not yet device qualified.**

**Decided: once-only factory compat.** A never-enrolled node with a readable
store accepts a present host (USB or encrypted/bonded BLE) until its first
ENROLL. `STOLO_ENABLE_FACTORY_COMPAT` stays on by default; setting it to 0
requires SCP enrollment from first boot. The persistent `ever_enrolled`
marker means FORGET_OWNER never reopens compat, including after restart.
FORGET_OWNER retains WiFi credentials; the now-unowned node is locked to SCP
enrollment for configuration. Older records infer enrollment history from
owner/epoch; the flag occupies a formerly reserved schema-1 byte.

**Physical recovery** (replacing the owner of an owned node) is the
boot-time enrollment window: hold the button while power is applied
(≥ 3 s) for a 60 s window. Three info-LED flashes announce the open window. Display builds show
`ENROLL WINDOW` with remaining seconds, cleared on expiry or successful ENROLL. A
successful ENROLL from any path wipes the previous owner's BLE bonds. USB
attachment alone authorizes nothing on an owned node.

**Store recovery.** If the config store cannot be read (corrupt slots, a
schema this firmware does not know, or an ownership rollback below the
recorded epoch floor) the node reports `store_ok = 0` in HELLO and refuses
ordinary configuration changes with error 7. Opening the enrollment window
alone does not repair storage and ENROLL still refuses that state. Explicit
**SCP_RESCUE** is the exception: while RECOVERY and the physical window are
both active, it deliberately replaces the unreadable identity with a new,
unowned identity, consumes the window and wipes BLE bonds after reply/drain.
Six info-LED flashes announce the destructive operation before its writes.
The lower OLED band shows `RESCUE` with a countdown while RECOVERY and the
window are active, `NEW IDENTITY` for 5 seconds after successful RESCUE, and
`STORE ERROR / HOLD BTN / AT BOOT` persistently when RECOVERY has no window.
A pairing PIN takes priority over every notice; otherwise the existing
radio/airtime panel is used. Notice state is read-only to the renderer.
The new node stays SCP-enrollment-only (unknown history means ever-enrolled).
Existing app identity bindings must be discarded and the new public key
reviewed before enrollment. No automatic identity replacement is attempted on
ambiguous storage. OLED announcements and LED visibility need bench qualification.

## Transports

| Transport | Carrier | Status |
|---|---|---|
| USB serial | KISS command byte `0x7A` (`CMD_STOLO`), the payload escaped like any KISS frame | implemented |
| BLE | KISS `0x7A` on the dedicated CTRL/EVENT service below; also accepted on NUS | implemented; encrypted/MITM link required; device qualification pending |
| WiFi | raw KISS TCP currently carries public HELLO / GET_RADIO / GET_FAULTS only | AUTH, ENROLL, RESCUE, SETs, FORGET and sensitive reads return error 4; secure control port planned |

### BLE Stolo Control Service

A second primary service on the same single-host BLE server as NUS:

| Role | UUID | Properties | Permissions |
|---|---|---|---|
| Service | `d8b6a9ad-bf50-45f0-997c-2b54d82bec2d` | Primary service | — |
| CTRL, phone → node | `d8b6a9ad-bf50-45f0-997c-2b54d82bec2e` | Write with response only | `WRITE_ENC_MITM` |
| EVENT, node → phone | `d8b6a9ad-bf50-45f0-997c-2b54d82bec2f` | Notify only | `READ_ENC_MITM`; CCCD has **both** `READ_ENC_MITM` and `WRITE_ENC_MITM` |
| BULK | `d8b6a9ad-bf50-45f0-997c-2b54d82bec30` | Reserved; **not created** | OTA is out of scope |

Only NUS is advertised. Discover the control service after connection and
subscribe to EVENT before writing CTRL. The EVENT CCCD permissions are explicit:
the pinned core's default BLE2902 descriptor is not secured by its parent
characteristic's permissions. Read permission alone would not permit the
subscription write.

The bytes are the USB carrier unchanged: `FEND 0x7A <escaped SCP> FEND`.
CTRL reassembles across ATT writes with its own parser/buffer; NUS keeps the
serial KISS parser. A callback queues CTRL bytes under a critical section;
the firmware loop consumes at most 64 bytes per tick and runs the shared
SCP dispatcher. It never runs flash writes or link-ending operations in the
BLE callback. Non-`0x7A` frames, invalid escapes and oversized carrier frames
are discarded to the next FEND and counted by the saturating, boot-local
`stolo_ctrl_parser.faults` diagnostic (not part of the reset-history GET_FAULTS
wire body). RX overflow discards buffered bytes and resets reassembly; the
client must retry after its request timeout. An ATT write response is **not**
an SCP application acknowledgement.

A scoped per-request reply sink sends all CTRL replies/errors to EVENT;
USB/NUS requests keep the serial sink. EVENT frames use a separate output
buffer, chunked to the negotiated ATT MTU minus 3, and are never placed on
NUS TX. Pending output is abandoned when the BLE link generation changes.
The event-send helper routes future unsolicited SCP events to EVENT when a
BLE session exists; no unsolicited event types are introduced here.

**One authority session per BLE link (D1).** AUTH on CTRL authorizes legacy
KISS mutations on NUS from that same encrypted link. HELLO on either channel
revokes prior authority. Disconnect, source change, ownership change, timeout
or LEAVE ends it for both. SCP `0x7A` on NUS remains accepted (D2), with replies
on NUS. BULK remains reserved (D3). These follow the brief's recommended
assumptions. A bonded phone on an **owned** node is a guest until AUTH;
never-enrolled factory nodes retain the decided compat exception.

An app's Connections helper can HELLO/AUTH on CTRL, rebuild RNS over NUS,
then send **SCP LEAVE (0x06)** and close its loopback control socket. Sending
legacy KISS CMD_LEAVE on CTRL is invalid because CTRL accepts only `0x7A`.
Closing that local socket alone is invisible to the firmware: it does not
end the shared BLE session. The app must serialize its configuration sessions
and send SCP LEAVE in cleanup, including failure paths.

**USB is the local trust boundary**: possession of the cable/host grants access, not owner authority. Protect the local port from hostile processes; v2 transcripts alone do not authenticate a subsequent raw WiFi SET.

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
| 5 | persistence did not complete; partial durable writes are possible, query/reconnect before retry |
| 6 | enrollment closed (the node is owned; open the boot-time window) |
| 7 | unreadable store or unavailable random generator; ordinary configuration refused; unreadable storage has a separate explicit rescue flow |

## Session and authorization

- **HELLO (0x01)** → `[fw_len][fw…][rnode_maj][rnode_min][node_pub 32][flags][capabilities][owner_epoch u32][nonce 16][source]`.
  Flags: bit0 owner enrolled, bit1 this session is the owner, bit2 enroll window open, bit3 config store OK, bit4 factory compat (build enabled + never enrolled + unowned + present host). `capabilities` replaces the former attestation byte at the same wire offset: bit0 = display present in this build, bit1 = enrollment window display implemented; remaining bits are reserved/zero. Display builds report `0x03`, headless builds `0x00`. These bits are advisory build capabilities, not proof of a working screen, physical presence, or cryptographic attestation. The host tool reports `capabilities`, `display_present`, and `enroll_window_display`. `source` is 0 USB / 1 BLE / 2 WiFi. HELLO starts a session on this connection: a fresh single-use nonce and no authority.
- **LEAVE (0x06)** empty body → `[ok = 1]`. Ends the shared authority/challenge without disconnecting BLE or interrupting an in-flight NUS frame when sent on CTRL. Requires HELLO, not owner authority. A nonempty body returns error 1. Legacy CMD_LEAVE stays supported on USB/NUS.
- **AUTH (0x02)** body: Ed25519 signature (64) by the owner key over
  `"stolo-auth-v2" || node_pub[32] || nonce[16] || owner_epoch(u32be) || source(u8)`.
  `node_pub` stops a challenge for node A being answered for node B under the same owner key; `source` binds the answer to the carrier it was issued on. Reply `[ok][role]` and, on success, a fresh `nonce[16]` so a following ENROLL (hand-over) needs no HELLO. The signed nonce is spent either way; after a rejected AUTH there is no live nonce until the next HELLO.
- **ENROLL (0x03)** body: `owner_pub[32] || sig[64]`, the signature by the new owner key over
  `"stolo-enroll-v2" || node_pub[32] || owner_pub[32] || nonce[16] || owner_epoch(u32be)`
  — a fresh proof of possession that cannot be replayed later. Allowed when the node is **unowned and the host is present** (compat), inside the **boot-time window**, or from the **current owner's session** (a hand-over). The new state follows the checked slot/floor/activation protocol below. A failure is not acknowledged as success; restart may complete the new ownership generation after a raised floor. Success bumps the epoch and ends the session; the reply is written and drained before bond removal is requested. Reply `[ok][owner_epoch u32]`.
- **FORGET_OWNER (0x04)** (owner): the node becomes unowned; epoch bumps; `ever_enrolled` remains set. Reply `[ok]`, then drain, then bond removal. The session ends; compat does not return.
- **RESCUE (0x05)** body: literal ASCII `RESCUE`. Requires RECOVERY **and** the physical boot window, over USB or encrypted BLE. Reply `[ok][new_node_pub 32]`; new identity, unowned, SCP-enrollment-only. Interrupted rescue remains in RECOVERY until another explicit physical rescue. CLI: `stolo_node_tool.py rescue --replace-identity`.

Ordinary configuration needs owner or enabled factory compat and a readable store. ENROLL and explicit RESCUE have the separate presence/window rules above.

## Radio

- **GET_RADIO (0x10)** (any session) → `freq u32, bw u32, sf u8, cr u8, txp u8, st_alock u16, lt_alock u16, radio_on u8, model u8, band_lo u32, band_hi u32, band_max_txp u8`. Airtime limits are hundredths of a percent. `band_*` are 0 when the model is not in the table.
- **SET_RADIO (0x11)** TLV: 1 freq u32, 2 bw u32, 3 sf u8, 4 cr u8, 5 txp u8, 6 st_alock u16, 7 lt_alock u16, 8 state u8. Only the fields present are judged, first against hardware ranges and then against the model's band (error 3, nothing applied); then applied through the same paths the legacy commands use, so every attached client sees the usual echoes. Reply: the GET_RADIO body followed by an `applied` byte (bit `tag-1` set for each tag that was applied), type `0x91`.

**One radio policy.** The model band table (from `rnodeconf`: T-Beam Supreme `0xDB` 420–520 MHz / 22 dBm, `0xDC` 850–950 MHz / 22 dBm; T-Beam `0xE4`/`0xE9` at 17 dBm; T3S3 `0xA1`/`0xA6` at 22 and `0xA5`/`0xAA` at 17; RNode NG21 `0xA2`/`0xA7` at 17) is consulted by SCP SET_RADIO, by the legacy `CMD_FREQUENCY` and `CMD_TXPOWER` handlers (a refused write echoes the unchanged value), and by `startRadio()`, which refuses to enable the radio while the effective frequency or power is outside the band — whichever path wrote it, including the saved boot configuration. A model that is not in the table gets no frequency or power writes at all.

## WiFi

- **GET_WIFI (0x20)** (owner / compat) → `supported u8, mode u8 (0 off, 1 station, 2 access point), channel u8, ssid_len, ssid…, psk_set u8, state u8 (0 off, 1 on, 2 connected), ip u32`. The passphrase is never returned.
- **SET_WIFI (0x21)** TLV: 1 mode, 2 ssid (≤ 32 bytes), 3 psk (≤ 32), 4 channel 1–14. The whole request is parsed and validated first (SSID/PSK exclude NUL and EEPROM's 0xFF sentinel); each EEPROM byte commit is checked. Reply: GET_WIFI layout, with mode/channel/SSID/psk_set taken from the accepted, persisted next settings, followed by `accepted_mask` (bit `tag-1`) and `flags` (bit0 runtime pending), type `0xA1`. State/IP are observations from before pending application, not a promise of connectivity. After reply/drain, `wifi_remote_init` reloads and applies settings. A follow-up GET returns effective observations.

## Bluetooth

- **GET_BT (0x30)** (owner / compat) → `supported u8, bt_state u8, bonds u8, pairing_open u8, window_s u16, enabled u8`.
- **SET_BT (0x31)** TLV: 1 pairing on/off, 2 debond-all, 3 window seconds (10–600, persisted), 4 BLE enabled on/off. Parsed and validated first; the pairing window and enabled preference are persisted with checked results. Pairing-open while connected, or combined with disable, is rejected (error 1). In particular, a phone cannot open pairing over its active CTRL link; setting the persisted window duration alone is allowed. Disconnect first and use the existing physical pairing flow, or configure pairing over USB while BLE is disconnected. Reply: GET_BT layout with accepted enabled/pairing/bond-count targets, followed by `accepted_mask` and `flags` (bit0 runtime pending), type `0xB1`. `bt_state` is the pre-application observation. GET's enabled field reflects effective runtime state. Reply/drain precede enable/disable/pairing/debond actions. A later GET checks effective state; bond removal is asynchronous.

## Faults

- **GET_FAULTS (0x40)** (any session) → `[count][reset reasons, oldest first]` (ESP-IDF `esp_reset_reason` values), the last eight boots.

## Persistence and restart contract

Schema 1 retains its record size: node keys, owner/epoch, pairing window,
`ever_enrolled` in a previously reserved byte, generation and CRC-32 in NVS
namespace `stolo`. Write order: **inactive slot → verify readback → checked
`epochfl` floor → checked `act` activation → live RAM**. The floor is raised
before activation. If activation fails, live configuration access enters
RECOVERY; a failed SET/ENROLL must not be interpreted as no persistent change.

| Interruption | Restart outcome (valid readable records) |
|---|---|
| Before/new-slot write fails | Old active slot/floor; an unreadable partial slot instead means RECOVERY |
| New slot written/read back, floor not raised | Old activated owner, including floor-write failure |
| Floor raised, activation not written | Old owner is below floor; load new slot and repair activation (checked), else RECOVERY |
| Activation written, RAM/reply not reached | New owner loads; host may not have received success |
| Before a floor existed (upgrade) | Highest valid epoch, then generation, establishes a checked floor and activation before loading |
| Missing activation with an existing floor | RECOVERY |
| Any existing oversized, truncated, unknown-schema or unreadable slot; bad metadata | RECOVERY, even if another slot is readable |

Only absence of **both slot keys, activation, floor and rescue marker** is
factory-new; key existence and `getBytesLength` are checked separately from
`getBytes`. Failure to open NVS enters RECOVERY. Explicit rescue first writes
a marker, removes the four store keys with checked results, commits the fresh
identity and removes the marker. A marker left at any interruption keeps the
node in RECOVERY; the host can retry with a new physical window.

WiFi credentials and the BT enabled byte remain in upstream EEPROM. Each
`EEPROM.commit()` result is checked, including retrying a dirty cached byte.
These writes are **not atomic across fields**, and the BT window/NVS plus
enabled/EEPROM update is not one durable transaction. Error 5 can leave earlier
fields persisted; no success reply is sent for that failed request. Reconnect,
GET effective state, and resubmit the complete desired settings after a failure.
Radio runtime application and flash power-loss behavior still require bench work.

## Transport drain and entropy build prerequisites

For SCP ENROLL, FORGET_OWNER, RESCUE, SET_BT and SET_WIFI, reply writes precede
an explicit source drain, and drain precedes link-ending actions. BLE uses
`bt_flush()` for NUS or immediate notification submission for EVENT, followed by a bounded 100 ms controller grace period. Notifications provide
no peer acknowledgement here; this is an attempted drain, **not delivery proof**.
WiFi uses `connection.flush()` (the pinned ESP32 API flushes receive buffering;
TCP output acceptance is not peer receipt), and USB uses `Serial.flush()`.
The old blanket statement that debond always follows a delivered reply was
incorrect: source ordering is tested, actual delivery needs bench qualification.

`stolo_cfg_defaults` seeds mbedTLS HMAC-DRBG/SHA-256 from 48 bytes collected with
`stolo_boot_entropy_fill` on each boot, before BLE/WiFi/ADC initialization. The
bootloader entropy source is disabled before peripheral startup. Runtime nonces
and rescue identities use that seeded DRBG without toggling boot entropy,
including USB while both RF stacks are disabled. Hardware entropy quality and
physical timing are not established by host fakes. See the [ESP-IDF 4.4 RNG
prerequisites](https://docs.espressif.com/projects/esp-idf/en/v4.4/esp32s3/api-reference/system/random.html).
`make prep-esp32` pins the Arduino core, requested libraries and their installed
dependencies; NRF's Git library remains pinned to its commit.

## Legacy handler classification

This table covers every `command == CMD_*` branch in `serial_callback`.
Unless marked guest, configuration mutations and sensitive reads require owner
or the enabled once-only factory compat policy. The default for unlisted commands is
guest deny. Payloads below describe actual handlers, not command names alone.

| Commands (`CMD_` prefix) | Handler behavior / payload | Guest policy |
|---|---|---|
| DATA | Escaped radio payload; queues transmission | Allowed data plane |
| STOLO | Escaped SCP frame, separately dispatched | Per SCP policy |
| STOLO_DROP | Internal parser sink, no operation | Drop |
| UNKNOWN | Internal command-byte sentinel, no handler effect | No operation |
| FREQUENCY, BANDWIDTH | u32 zero reads; nonzero writes | Echo current value, drop payload |
| TXPOWER, SF, CR | 0xFF reads; otherwise write (legacy clamps) | Echo current value, drop payload |
| ST_ALOCK, LT_ALOCK | u16 writes; **zero disables**, not a read | Echo current value, drop payload |
| RADIO_STATE | 0xFF reads; 0 stops, 1 starts; may mark cable connected | Echo current value, drop payload |
| IMPLICIT | Byte sets implicit packet length | Denied |
| LEAVE | 0xFF clears cable/display telemetry for privileged host, then ends session; guest only ends session | Session teardown allowed |
| STAT_RX, STAT_TX, STAT_RSSI | Telemetry read on payload | Allowed |
| RADIO_LOCK | Recomputes RAM radio-lock state and reports it | Denied |
| BLINK | LED diagnostic side effect, cycles from payload | Explicitly allowed diagnostic |
| RANDOM | Random telemetry read | Allowed |
| DETECT | DETECT_REQ reports identity, marks host cable connected | Allowed discovery |
| PROMISC | 1 enables, 0 disables; other values report state | Denied |
| READY | Reports packet queue availability | Allowed |
| UNLOCK_ROM | ROM_UNLOCK_BYTE mutates provisioning lock/state | Denied |
| RESET | CMD_RESET_BYTE invokes reset; role checked before execution; reset/return ends session | Denied |
| ROM_READ, CFG_READ | EEPROM/config dump, may contain credentials | Denied |
| ROM_WRITE | Two decoded bytes write EEPROM address/value | Denied |
| FW_VERSION, PLATFORM, MCU, BOARD | Public identity reads | Allowed |
| CONF_SAVE, CONF_DELETE | Persist/delete radio configuration | Denied |
| FB_EXT | 0xFF reads; 0/1 changes external framebuffer mode | Denied |
| FB_WRITE | Nine bytes write framebuffer line | Denied |
| FB_READ, DISP_READ | Nonzero requests display contents | Denied sensitive reads |
| DEV_HASH | Nonzero requests public device hash | Allowed |
| DEV_SIG | 64 bytes replace RAM signature, validate and possibly persist | Denied write |
| FW_UPD | 1 enables firmware-update mode; other values disable | Denied |
| HASHES | 1 target / 2 firmware / 3 bootloader / 4 partition hash read | Allowed |
| FW_HASH | 32 bytes replace/persist target firmware hash | Denied |
| WIFI_CHN | 1–13 persist channel (legacy handler); SCP accepts 1–14 | Denied |
| WIFI_MODE | off/STA/AP persists mode and restarts WiFi | Denied |
| WIFI_SSID, WIFI_PSK | NUL-terminated payload writes up to 32 bytes plus padding | Denied |
| WIFI_IP, WIFI_NM | Four bytes persist address/netmask | Denied |
| BT_CTRL | 0 stop/persist disabled, 1 start/persist enabled, 2 start/pair | Denied |
| BT_UNPAIR | 1 requests bond removal | Denied |
| DISP_INT, DISP_ADDR, DISP_BLNK, DISP_ROT | Escaped byte changes/persists display setting | Denied |
| DIS_IA | Escaped byte persists interference avoidance | Denied |
| DISP_RCND | Nonzero sets display reconditioning flag | Denied |
| NP_INT | Escaped byte changes/persists LED intensity | Denied |

## Verification boundary and app handoff

`make test-host` compiles production Stolo headers plus the full automatically
extracted `serial_callback`, frame/FIFO hooks and EEPROM helpers. It exercises
transport transitions, partial/queued frames, mutation role rechecks, owner
RESET and guest DEV_SIG, individual NVS/EEPROM failures, restart snapshots,
record-size/metadata recovery, accepted SET replies, drain/action call order,
entropy lifecycle, compat including a disabled-switch build, carrier admission,
and physical-window rescue/retry. CTRL tests cover separate parser/buffer/sink
routing, interleaving in both directions, guest/owner and shared NUS authority,
SCP LEAVE, carrier rejection/recovery, disconnect/idle expiry, generation
changes, unsolicited routing, and EVENT reply/action ordering. Extracted
BLESerial methods test GATT/CCCD permission declarations, NUS-only advertising,
MTU chunking, subscription gating and RX overflow/reconnect cleanup against a
fake stack. This is **software verified** with hardware,
NVS, entropy/DRBG, Ed25519 and transport fakes. Signature validity, callback
scheduling, reply receipt, entropy quality, USB driver close semantics and power
interruption on real flash are **not device qualified** in this round.
`make firmware-stolo-tbeam_supreme` is a compile check, not device qualification.

App handoff: on error 7 distinguish unreadable identity from a lost owner key.
For unreadable identity explain permanent replacement, have the user hold the
button at power-on, then explicitly send RESCUE after HELLO and confirmation;
refresh HELLO, display/rebind the new public key, then ENROLL. A lost owner key
with readable storage uses the physical window plus ENROLL and preserves node
identity. Decode WiFi/BT SET's accepted mask and pending byte and perform a
follow-up GET. Do not mirror defects into fake-node tests; use these negative
cases alongside the shared host fixtures. OLED recovery display work is pending.
