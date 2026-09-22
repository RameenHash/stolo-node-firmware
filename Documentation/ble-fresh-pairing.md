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

**Device qualified:** fresh passkey pairing, a connected iPhone link, app ready and
Node settings loaded on 11DE were confirmed by Rameen on 2026-09-21 with candidate `b1d6a56`,
then repeated on the enlarged-digit image (`e4410b8`) on 2026-09-22.
The later session also qualified silent bonded reconnect and the physical
pairing-window/no-bond cold-boot checks. The
[success trace](traces/2026-09-21-11de-success.txt) records one bond and
authenticated connected state. All functional checklist gates below passed; physical photos are still needed. A sanitized
[app journal excerpt](traces/2026-09-21-11de-app-ready.txt) corroborates ready. Rameen confirmed the enlarged digits readable on 2026-09-22.

- [x] Radio reports no bonds; iPhone Settings has no RNode bond (2026-09-22).
- [x] Fresh app connection displays the passkey on the OLED, iOS accepts that
      passkey, creates a bond and the link stays connected.
- [x] App reaches ready and Node settings can be read (Rameen confirmed).
- [x] Reconnect uses the existing bond silently (cold boot, 2026-09-22).
- [x] Forget in iOS plus node debond allows a second clean fresh pairing (2026-09-22).
- [x] Original F3 check 1: no-bond cold boot needs no button press and remains
      pairable beyond the configured window.
- [x] Original F3 check 2: app pairing keeps the link up without a required
      post-pair disconnect/reconnect.
- [x] Original F3 check 3: bonded cold boot does not open pairing, bonded
      reconnect works, and a 5 s button hold opens the configured pairing
      window (120 s default; record any bench override).
- [x] Original F3 check 4: record a BLE throughput baseline with the existing
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

**Device qualified:** the later successful 11DE pairing supersedes these
pre-fix failures; see the success record below. Reconnect, forget/debond recovery, remaining F3 checks and photos
are pending; Rameen subsequently confirmed ready and Node settings loaded.

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
**Device qualified:** the physical code was subsequently entered and pairing
succeeded; the remaining checklist is still pending.

## Successful fresh pairing and larger digits

**Device qualified:** Rameen reported “connected and paired” on 2026-09-21,
then requested a larger OLED code. The captured fresh attempt starts with
zero bonds; `onAuthenticationComplete` reports success, mode `0x0d`, one bond,
and `state=3 auth=1` with no post-authentication disconnect. EVENT subscription
(handle `0x3c`) follows authentication. Rameen also confirmed that the app reached ready and Node settings loaded.
This qualifies fresh pairing, the retained link and Node settings access;
silent reconnect, recovery and throughput remain pending.

**Software verified:** the follow-up enlarges each positive 3×5 glyph to
6×10 pixels. Six digits span 56 pixels with four-pixel margins and clear
four-pixel gaps; they remain below the PAIRING label within the 64×64 panel.
The production-renderer tests check all ten digits at doubled size, gaps,
margins, clipping and leading zeros. Rameen subsequently confirmed all enlarged digits readable (device qualified); SMP, bond storage and retry behavior are unchanged by this follow-up.

**Software verified:** enlarged-digit `make test-host` passes; T-Beam Supreme
trace, normal Stolo and plain builds pass at 1,466,197, 1,463,549 and
1,442,429 bytes respectively. The ROM-loader upload completed with flash hash
verification after the RAM-stub uploader failed before writing. USB records
from the new image show `state=3 auth=1 bonds=1` after reboot. The phone-side
silent reconnect observation remains pending. The firmware-hash target update
was completed and read back over USB on 2026-09-22, after the phone was removed.

**Software verified:** the reported “corrupt firmware” warning after the
enlarged-digit upload was consistent with the unfinished firmware-hash target
update. With BLE disconnected, `rnodeconf --firmware-hash` completed; subsequent
USB reads returned the same target and actual hash, matching the built image:
`f08349a5df4c4b4874f2378d32d3aef563ee686d1062b89fd4dda9ddfd651f08`.
No reflashing or bond erasure was needed for this repair. After an explicit
USB reset, the display canvas reads “DEVICE CHECKS PASSED”. The boot trace
still reports one radio bond,
so removal from the phone alone has not established a bond-free radio.
[Recovery evidence](traces/2026-09-22-11de-hash-recovery.txt).

**Device qualified:** Rameen confirmed on 2026-09-22 that the physical OLED
returned to its normal screen after the hash repair and reset. The remaining BLE qualification gates are still pending.

## Continued qualification, 2026-09-22

**Device qualified:** Rameen powered 11DE off and on without holding a button
or connecting the app and confirmed a normal screen with no pairing code.
The corresponding USB cold-boot trace reports `bonds=1 state=1 allow=0`.
This satisfies the bonded cold-boot portion of F3 check 3; reconnect and the
physical pairing window are tracked separately.

**Software verified:** GET_BT read back an existing 600-second bench window.
It was temporarily set to the standard 120 seconds for timeout tests, then
restored to 600 seconds with GET_BT readback at 06:46:23 EDT. During the first gesture attempt, USB output
recorded “Starting Access Point...” and “SPIFFS Ready”. The button handler
selects console AP mode above ten seconds and pairing above five seconds, on
release; `update_bt()` is skipped in console mode. That excursion is not a
valid normal-mode pairing timeout result. A USB reset exited console mode;
GET_WIFI showed the persisted Wi-Fi setting was off, and Bluetooth was
re-enabled after intervening short presses toggled it off. The retry uses one
six-second hold. No firmware behavior was changed for this observation.

**Device qualified:** on the retry, Rameen confirmed that one six-second hold
opened the pairing screen without Wi-Fi AP mode and that all six enlarged
digits were readable. The matching trace records pairing open with one bond
at `t=84592` (06:39:10 EDT); the timeout check follows separately.

**Software verified:** the normal-mode bonded window expired at `t=204597`,
120.005 seconds after the six-second gesture opened it. `bt_disable_pairing`
cleared permission and the passkey, returned to `state=1`, and retained one
bond. Subsequent GET_BT confirmed pairing closed. SET_BT debond was then
applied, and a later GET_BT confirmed zero bonds, Bluetooth enabled, and the
120-second test window. No owner or radio configuration was erased.

**Software verified:** [cold-boot, timed-window and debond USB evidence](traces/2026-09-22-11de-window-and-debond.txt).

**Device qualified:** Rameen confirmed the no-bond cold boot automatically
showed a pairing code without a button press and that no RNode entry remained
in iPhone Bluetooth Settings. **Software verified:** the trace reports zero
bonds and automatic pairing at `t=780`; at the 120-second threshold it logs
`pairing.window_elapsed` without closing. GET_BT at 06:46:00 EDT, about
138 seconds after boot, still reports `pairing_open=true`, `bonds=0`,
`window_s=120`. The original 600-second window was then restored without
closing pairing. Rameen then successfully paired from the phone, as recorded below.

**Device qualified:** after the no-bond cold boot and wait beyond the test
window, Rameen entered the enlarged code and confirmed pairing, retained
connection, app ready and Node settings loaded. This completes forget-in-iOS
plus node-debond fresh-pair recovery, and F3 check 1. **Software verified:**
USB authentication succeeds at `t=202416`, with `auth_mode=0x0d`, one bond,
`state=3 auth=1 allow=0`. The iPhone app journal independently records ready
at 06:47:03.792 with a 512-byte maximum write payload.

**Software verified:** [no-bond boot, restoration and fresh-pair recovery evidence](traces/2026-09-22-11de-fresh-recovery.txt).

**Device qualified:** after the successful fresh-pair recovery, Rameen power
cycled 11DE without holding a button, kept the iPhone bond, and confirmed
ready/settings loaded without a code request. This completes silent reconnect
and F3 check 3. **Software verified:** the iPhone journal records connected at
06:50:27.925 and ready at 06:50:28.150 (512-byte write payload). USB output
paused during connect, then drained queued records later: authentication
succeeded at `t=1463` with one bond and no passkey notification in the
continuous reconnect sequence. The later load interval dropped 1,014 trace
records, so it is not a lossless throughput trace. Subsequent protected CTRL
reads also succeeded. [Reconnect evidence](traces/2026-09-22-11de-silent-reconnect.txt).

**Software verified — throughput preparation:** a read-only benchmark reached
both iPhone loopback bridge ports via USB forwarding. After HELLO, the
three-second simultaneous GET_RADIO smoke run returned 200/200 valid replies
on each channel. A 60-second attempt completed 6,848/6,848 CTRL replies but
the NUS socket closed after 328 replies and about five seconds. No corrupted
SCP payload was observed before that close, but the simultaneous sustained
run is not qualified. The bridge accepts one socket client per channel and
replaces the previous one on a new accept; pausing the normal RNS connection
for an isolated benchmark is the next check. This is not yet evidence of a
firmware or iOS bridge defect.

**Device qualified:** the isolated 60-second simultaneous NUS/CTRL baseline
completed 3,936/3,936 valid GET_RADIO replies per channel, with no SCP integrity
errors. The normal saved app connection was disabled for measurement while
iPhone Bluetooth and the native BLE link remained on. This completes F3 check
4 for this workload. [Method, limitations and counters](ble-throughput-baseline.md).
Rameen re-enabled the normal app connection: the app briefly displayed a
restart-required banner, reconnected without restarting, and then cleared the
banner. This is recorded as an app observation; no app/fork source was changed.
Physical photo evidence is still outstanding.

**Software verified:** `make test-host` passed again after qualification. The
firmware source is unchanged from the passing trace, normal Stolo and plain
T-Beam Supreme builds. The benchmark harness passed a local framing, sequence
wrap and bad-sequence rejection check. Temporary USB TCP forwarding was stopped;
the original 600-second pairing window and enabled app connection were restored.

**Device qualified — remaining evidence:** physical bench photos have not
been supplied. Keep the PR draft for this evidence; the functional iPhone
checklist is complete.
