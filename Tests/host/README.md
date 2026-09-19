# Authority correction regressions

Run `make test-host` from the firmware root. Python tests need only the standard
library; C++ tests need a C++17 compiler. This is **software verified** evidence.
Nothing in this suite is **device qualified**.

`extract_dispatcher.py` extracts complete production functions at each run,
including `serial_callback`, parser/FIFO hooks, USB event callbacks, transport
drain and EEPROM helpers. It does not keep a manually maintained parser copy.
The extracted files are generated under ignored `build/host/`.

| Suite | Coverage |
|---|---|
| `dispatcher_test.cpp` | C1: every pair of source transitions, queued bytes, SCP/escape state, mid-frame disconnect, mutation role check, authorized positive case |
| `store_test.cpp` | C2/C3: slot/floor/activation write faults and restart snapshots, pre-floor upgrade, oversized/truncated slots, missing activation, namespace/read/length errors |
| `corrections_test.cpp` | C4–C11 via the production KISS byte dispatcher: USB expiry/keepalive, RESET/DEV_SIG/LEAVE effects, accepted/pending SET fields, persistence errors, drain/action order, ever-enrolled lock, WiFi/unencrypted BLE refusal, rescue and each rescue write/remove fault |
| `persistence_test.cpp` | C5: real EEPROM helper failure propagation, dirty cached byte retry, enabled preference update |
| `transport_test.cpp` | C4/C6: real USB event hook under both USB modes; real BLE flush/100 ms grace period ordering; USB/WiFi flush paths |
| `compat_disabled_test.cpp` | C10: separate build with factory compat disabled still supports SCP enrollment |
| `tool_test.py` | C4/C5/C8: LEAVE/flush/close, lost-device close, pending SET decoding, rescue token/new-key result |
| `authority_test.cpp` | Existing authority, transcript, nonce, radio policy and validation regressions |

Hardware actions, FIFO/storage objects, Ed25519, RNG/DRBG and transports are
fakes. EEPROM helpers and the source dispatcher are real, but the underlying
flash and peripherals are not. Tests establish source behavior and call order;
they do not establish signature validity, physical entropy, callback scheduling,
USB driver close detection, BLE notification delivery or flash power-loss behavior.
The specification lists those bench qualification gates separately.
