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
