# SCP over BLE — brief validation and firmware handoff

Reviewed against `2026-09-19-scp-over-ble.md` and firmware `next` at
`99da2589c7b5ace62a648ccddbe844e9caca2e5d` on 2026-09-19.

The second-service design is sound for this single-host firmware. Implemented
Part A with the brief's recommended D1/D2/D3 assumptions: shared BLE authority,
continued SCP support on NUS, and reserved/uncreated BULK. No second radio
client, advertising UUID, OTA service or new framing was added.

## Findings and implemented corrections

1. **Prerequisite branch:** this workspace started on `main`, before SCP v2.
   Its existing feature branch was fast-forwarded to the brief's pinned `next`
   commit. The firmware change is intended for `next`, not the bench-qualified
   `main`. The Arduino compile runs from a local copy named `RNode_Firmware`;
   the Conductor workspace and branch are not renamed.
2. **BLE callbacks are not the firmware loop.** Running the dispatcher from
   `onWrite` would allow concurrent changes to shared session/parser state,
   EEPROM and radio configuration. CTRL now has a bounded, critical-section
   protected queue. Only the firmware loop parses/dispatches it. BLE connection
   callbacks post an atomic boundary flag; owner checks reject pending
   boundaries immediately and the loop performs session/parser teardown.
3. **Two parsers alone were insufficient.** The existing HELLO handler clears
   the shared serial parser and unfinished data frame. CTRL HELLO now resets
   authority/challenge while preserving an in-flight NUS frame. Disconnect,
   source change, timeout and ownership changes retain the complete parser
   teardown. Legacy handlers continue checking authority at mutation time.
4. **The CCCD needs write protection as well as read protection.** Existing NUS
   TX adds a default BLE2902 descriptor without explicitly securing it. EVENT
   gets explicit encrypted/MITM **read and write** permissions on its CCCD;
   copying NUS literally would not implement the brief's stated security rule.
   The pinned core accepts separate characteristic/descriptor permissions:
   [BLECharacteristic.cpp](https://github.com/espressif/arduino-esp32/blob/2.0.17/libraries/BLE/src/BLECharacteristic.cpp),
   [BLEDescriptor.cpp](https://github.com/espressif/arduino-esp32/blob/2.0.17/libraries/BLE/src/BLEDescriptor.cpp).
5. **Notifications need explicit MTU chunking.** EVENT frames are escaped into
   their own buffer and submitted in pieces no larger than negotiated MTU−3.
   Link-generation and authentication/subscription checks stop stale output;
   connection boundaries also clear the last EVENT attribute value.
   CTRL queue overflow drops the buffered stream instead of overwriting
   unread bytes; the next FEND resynchronizes it. Clients must await the SCP
   response, since the ATT acknowledgement only confirms the GATT write.
6. **The app's proposed LEAVE cannot use the old wire command.** Existing
   cleanup emits legacy KISS CMD_LEAVE, which a `0x7A`-only CTRL parser must
   reject. Added SCP LEAVE (`type=0x06`, empty body, reply `0x86 [1]`) so the
   control client can end shared authority without detaching RNS or truncating
   its frame. USB/NUS CMD_LEAVE continues working. This is an additive SCP-v2
   command; the future BLE app must send it explicitly and await its reply.
7. **Some qualification expectations conflict with existing policy.** Pairing
   open while BLE is connected is rejected by the existing authority contract.
   This remains error 1 over CTRL: changing the window duration is allowed,
   but opening pairing needs the disconnected physical flow or USB while BLE
   is disconnected. A future “reply, disconnect, then pair” feature needs its
   own lifecycle design. Also, unauthenticated bonded phones are guests on
   **owned/ever-enrolled** nodes; never-enrolled factory nodes intentionally
   retain once-only compat. The protocol documentation now states that policy
   as decided, with `STOLO_ENABLE_FACTORY_COMPAT` enabled by default.

## Software verification

- `make test-host`: all suites pass (420 C++ checks and four Python tests,
  including 158 new CTRL/GATT checks). New CTRL tests cover real production
  parsing/dispatch, independent NUS state, escaping, authorization, recovery,
  shared-session LEAVE and reply/action ordering. Extracted BLESerial methods
  cover GATT declarations, NUS-only advertising, the real queue, negotiated MTU
  sizes (23/50/247/517), overflow, subscription and link-generation cleanup.
- `make firmware-stolo-tbeam_supreme`: successful ESP32-S3 build on pinned
  Arduino ESP32 2.0.17, no compiler warnings. Flash 1,461,701 / 2,097,152 bytes;
  global RAM 98,524 / 327,680 bytes. No flash/upload was performed.
- The ordinary non-Stolo `make firmware-tbeam_supreme` build also passes
  without compiler warnings.
- The CTRL suite passes AddressSanitizer and UndefinedBehaviorSanitizer;
  `git diff --check` is clean.

These results are **software verified**, not **device qualified**. Tests use
hardware, stack, storage and crypto fakes. They verify configured permissions
and call ordering, not real stack rejection, task scheduling, signature
validity, pairing behavior or notification delivery.

EVENT submission precedes disable/debond, followed by the same bounded 100 ms
grace period used in the corrections round. Notifications provide no peer
acknowledgement: this is attempted drain, **not delivery proof**. Independent
review and Rameen's iPhone/T-Beam qualification remain required before merge
or promotion toward `main`.

## App sequencing and remaining qualification

Part B is gated on the SCP-v2 migration:
[gridx-stolo-node PR #9](https://github.com/RameenHash/gridx-stolo-node/pull/9)
was still **open/unmerged** when checked on 2026-09-19. No app checkout is
modified by this firmware change. After it merges, base the app work on the
fork's `main`, retain the configuration slot/managed-radio restrictions, and
implement the 7634 bridge/transport/gating work from the brief. Apply the
SCP LEAVE and pairing-flow corrections above. Never push the fork's work to
`RameenHash/gridx`.

The iPhone round still needs to establish: service discovery/subscription;
unauthenticated-link permission rejection; settings reads and writes while
NUS remains live; owned/guest/factory policy; enrollment/forget; actual replies
before disable/debond; owner Connections rebuilds while AUTH is held; guest
mismatch behavior; disconnect/reconnect guest state; and sustained simultaneous
NUS/CTRL traffic without corruption. Test pairing-window duration over CTRL
and expect active-link pairing-open rejection; qualify actual pairing opening
via the disconnected physical or USB path. No hardware result is claimed here.
