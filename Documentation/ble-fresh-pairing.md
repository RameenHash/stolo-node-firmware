# Fresh iPhone pairing investigation

**Software verified:** this investigation starts at `next` commit `7aab493`.
Commit `b9c1008` adds instrumentation only. Pairing, GATT permissions, control
reset and protocol boundary behavior are unchanged in that commit.

**Software verified:** build the diagnostic image with
`make firmware-stolo-tbeam_supreme-ble-trace` in a directory named
`RNode_Firmware`. The normal `make firmware-stolo-tbeam_supreme` target does
not emit diagnostic text. Diagnostic output is plaintext on USB, so use an
exclusive serial recorder at 115200 baud during a pairing attempt; do not
attach RNS or a configuration client concurrently.

**Software verified:** each `[BLETRACE]` record includes milliseconds since
boot, sequence, radio-name suffix, cumulative dropped-record count, event, hexadecimal detail, Bluetooth state (`0` off, `1` on, `2` pairing,
`3` connected), pairing permission, application authentication, whether a
passkey is present, pending BLE boundary and radio bond count. No passkey,
peer address, key or application payload is printed by the diagnostic logger.
The existing KISS stream can contain the passkey; retain raw USB capture locally
and publish only diagnostic records.

**Software verified:** callbacks enqueue bounded records; the firmware loop
drains complete lines only when USB has buffer space. Sequence gaps and the
dropped-record count identify an incomplete capture. `display.path` records
render inputs, not physical OLED visibility: bit 0 initialized, bit 1 update
mode, bit 2 external framebuffer, bit 3 radio diagnostics, bits 4–5 Bluetooth
state, bit 8 valid firmware hash.

**Software verified:** callback entry/exit records bracket `resetControl`,
`onConnect`, `onDisconnect`, and `stolo_host_disconnected`. The boundary-poll
detail is a bitmask (`1` USB, `2` BLE). `onSecurityRequest.accept` records the
returned decision; authentication records success, raw failure reason and
authentication mode. `onPassKeyNotify` detail records whether the stack's code
matches the previously displayed value, without disclosing it.

**Software verified:** GAP event IDs and GATT disconnect reasons use the
pinned Arduino ESP32 2.0.17 / ESP-IDF headers. The custom GAP/GATTS handlers
run after Arduino's handlers, so the preceding callback records are necessary
to interpret state changes. `local.disconnect.request` and `local.deinit`
identify application-requested link termination; a raw disconnect reason
alone does not establish which application action caused it. GATT read/write
handles identify requests delivered to the application, not a packet-level
capture of every ATT request rejected inside the stack.

## Qualification record

**Device qualified: none.** The following gates require the physical iPhone,
OLED observations/photos and app logs. A host-test pass or firmware build
cannot satisfy them.

- [ ] Radio reports no bonds; iPhone Settings has no RNode bond.
- [ ] Fresh app connection displays the passkey on the OLED, iOS accepts that
      passkey, creates a bond and the link stays connected.
- [ ] App reaches ready and Node settings can be read.
- [ ] Reconnect uses the existing bond silently.
- [ ] Forget in iOS plus node debond allows a second clean fresh pairing.
- [ ] Original F3 check 1: no-bond cold boot needs no button press and remains
      pairable beyond the configured window.
- [ ] Original F3 check 2: app pairing keeps the link up without a required
      post-pair disconnect/reconnect.
- [ ] Original F3 check 3: bonded cold boot does not open pairing, bonded
      reconnect works, and a 5 s button hold opens the configured pairing
      window (120 s default; record any bench override).
- [ ] Original F3 check 4: record a BLE throughput baseline with the existing
      write-with-response path before any WRITE_NR change.

**Software verified:** any iOS bridge change belongs in a separate fork PR;
this firmware PR does not modify either app repository.

## Captured before behavior changes, 2026-09-21

**Software verified (USB capture interpretation; device qualified: none):**
The attached `/dev/cu.usbmodem1101` radio identifies itself as `11DE`. Rameen
reported a rapidly failing code-entry prompt for `01BA`, but a stable prompt
for `11DE`, no visible code, and disappearance of the radio's pairing screen
after pressing Pair without entering a code. The captured radio is `11DE`;
these records do not establish what happened on `01BA`.

**Software verified:** the [first capture](traces/2026-09-21-11de-first.txt)
has truncated lines, so absence of an event there is not conclusive. The
[buffered repeat](traces/2026-09-21-11de-buffered.txt) records sequences 20–49
continuously with `drop=0`: connect/control reset/deferred boundary keep
`state=2 allow=1`; passkey notification arrives 885 ms after connect and
matches the stored display PIN (`detail=1`); authentication fails later with
`0x51`, and its callback changes the state to `1`, permission to `0`, PIN
presence to `0`. No security-request rejection appears in that interval.
Both captures record eventual peer termination (`0x13`): about 78 s
after connect in the first, 85 s in the buffered repeat. This is not the reported ~0.7 s connect-time teardown.

**Software verified:** in pinned ESP-IDF v4.4.7, auth reason `0x51` is
`SMP_CONFIRM_VALUE_ERR (0x04)` plus the BTA offset `0x4d` (`0x43 + 10`),
not HCI error `0x51`. Sources:
[BTA mapping](https://github.com/espressif/esp-idf/blob/v4.4.7/components/bt/host/bluedroid/bta/include/bta/bta_api.h#L624-L629),
[HCI maximum](https://github.com/espressif/esp-idf/blob/v4.4.7/components/bt/host/bluedroid/stack/include/stack/hcidefs.h#L815),
[SMP reason](https://github.com/espressif/esp-idf/blob/v4.4.7/components/bt/host/bluedroid/stack/include/stack/smp_api.h#L67).
A wrong/empty entered code is consistent with this failure; the reason does
not explain why the OLED's code was not visible.

**Software verified:** the buffered trace's display-path flags `0x121` mean
initialized, valid firmware hash, pairing, without update mode, an external
framebuffer or radio diagnostics. They establish software render inputs,
not physical OLED visibility. The pre-fix failure callback then changes them to
`0x111` (Bluetooth on, no displayed pairing PIN).

**Software verified:** the trace does not implicate an early EVENT CCCD
write. Its protected descriptor is handle `0x3c`; the observed write delivered
to the application is `0x2e`, and the stack rejects writes to `0x2a` for missing
MITM authentication. Do not relax EVENT/CTRL permissions on this evidence.
The iOS bridge remains outside this PR; further bridge evidence requires a
separate fork PR.

**Device qualified: none.** Missing evidence: physical OLED photo and correct
code entry, successful iPhone bond, ready/settings, reconnect/recovery,
original F3 checks and app log. The candidate fixes below remain a draft pending the iPhone gates.

## Candidate fixes after the captured failure

**Software verified:** reading the radio's actual `disp_area` with USB
`CMD_DISP_READ` exposed the PIN's inverse digit artwork over F8's dark banner.
`bm_n_uh` contains black numeral strokes with white padding, intended for the
old white pairing panel. On the dark panel those strokes merge into the
background, leaving fragments. The PIN renderer now uses the existing
positive `bm_stolo_digits` glyphs that match the current banner. It renders
from one PIN snapshot with a seven-byte stack buffer; the SMP code itself is
unchanged and the stack notification remains authoritative.

**Software verified:** after flashing, a second USB canvas read contains six
complete positive digit glyphs. This is framebuffer evidence, not a photo
of the physical OLED and not iPhone qualification. Raw canvases contain a
live passkey and remain under local `.context/`, outside the PR.

**Software verified:** the failed-auth callback now retains pairing and its
new code when the existing window is still open. Peer cancellation likewise
retains that window. A no-bond radio keeps the already-open window indefinitely;
a bonded radio retains only the existing deadline, including across millis
wrap. Neither path reopens an explicitly closed window, extends its deadline,
authenticates the failed link, or requests a disconnect. Successful
pairing still clears the code/window and keeps the authenticated connection.

**Software verified:** `make test-host` passes with the actual extracted
ESP32 callbacks (15 checks), the production PIN renderer and artwork
(11 checks), and a real deferred-boundary preservation regression. Against
the pre-fix callbacks, four retry checks fail; against the old digit artwork,
all 11 pixel checks fail. Those regressions pass with the candidate.

**Software verified:** the candidate trace image builds at 1,466,153 bytes;
normal Stolo at 1,463,513 bytes. It was flashed with its firmware hash updated.
**Device qualified: none** until the physical code is entered successfully
and the iPhone qualification checklist is completed.
