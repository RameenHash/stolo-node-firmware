# iPhone BLE read-throughput baseline, 2026-09-22

**Device qualified:** T-Beam Supreme 11DE and the iPhone 16 Pro completed a
60-second simultaneous NUS/CTRL read workload with all requested SCP responses
intact. Both paths use the existing write-with-response characteristics.

| Channel | Requests / valid replies | Elapsed | Response payload | Replies/s |
| --- | ---: | ---: | ---: | ---: |
| NUS | 3,936 / 3,936 | 60.094 s | 1,702.94 B/s | 65.50 |
| CTRL | 3,936 / 3,936 | 60.065 s | 1,703.76 B/s | 65.53 |

**Software verified:** [raw counters](traces/2026-09-22-11de-throughput.json)
record no errors, missing replies, duplicate/reordered sequence numbers, invalid
KISS escapes, or changed GET_RADIO response bodies. NUS also delivered 24
non-SCP frames, which were counted but excluded from payload validation/rates.
Only the 26-byte GET_RADIO bodies count toward the response-payload rate.
This is an end-to-end request/reply baseline through USB forwarding, the iPhone
bridge and BLE; it is not a bulk-transfer ceiling or an RF throughput test.

**Software verified:** firmware was the enlarged-digit trace build (`e4410b8`,
image hash `f08349a5df4c4b4874f2378d32d3aef563ee686d1062b89fd4dda9ddfd651f08`),
Arduino ESP32 2.0.17; installed Stolo app was 1.1.0 (23), and its journal reported
a 512-byte maximum write payload. Diagnostic logging remained enabled. USB
trace output stalled and its bounded queue dropped records under the earlier
load attempt; BLE integrity was measured from actual matched socket replies,
not inferred from absence of USB errors.

**Software verified — reproduce:** disable only the saved radio connection's
card switch in Stolo Connections, leaving iPhone Bluetooth on and the native
BLE link ready. Keep the app open. Forward its existing bridge ports to local
loopback, then run the read-only tool (replace `IPHONE_UDID` with the connected
phone's identifier). HELLO establishes each session; batches of eight public
GET_RADIO requests exercise each channel concurrently with distinct sequence
parities, including escaped sequence bytes and wraparound.

```sh
iproxy -s 127.0.0.1 -u IPHONE_UDID 17633:7633 17634:7634
# In another terminal:
python3 Tools/ble_read_benchmark.py --seconds 60 --batch 8
```

**Software verified:** the tool makes no configuration or RF-data writes.
It checks exact response type/version/length, request order and payload equality.
A local fake peer verified normal concurrent framing and sequence wrap, plus
rejection of an intentionally incorrect response sequence. The tool returns
nonzero when either channel fails. Re-enable the saved app connection and stop
the USB forwarder after the run.

**Software verified — excluded attempts:** the first harness smoke run omitted
HELLO and was corrected before measurement. With the normal app backend active,
a later 60-second attempt retained CTRL but lost the NUS socket after roughly
five seconds (328 replies). The bridge has one client per socket and a new
accept replaces the old client; contention is the inferred explanation, not
proven packet loss. With the normal connection disabled, the complete
simultaneous run above passed. An intervening phone-Bluetooth-off observation
was confirmed by the app journal and corrected before the successful run.
No app/fork source was changed; these observations do not establish an iOS
bridge defect requiring a behavior fix.
