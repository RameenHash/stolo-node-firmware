# Changes from upstream RNode Firmware

This repository is **Stolo Node Firmware**, a fork of
[markqvist/RNode_Firmware](https://github.com/markqvist/RNode_Firmware),
licensed under the GNU General Public License v3.0 like its upstream. This
file is the running record of every deliberate difference from the pinned
upstream commit, kept so that a recipient of a Stolo Node can see exactly
what was changed and why (GPL-3.0 §5(a)), and so a rebase onto a newer
upstream is a merge of hooks rather than a merge of logic.

**Upstream pin:** `markqvist/RNode_Firmware` master
`d39339f8ecd5145b248c18bac7b6ea0f82faf85a` (reports firmware version 1.86;
identical to the `1.86` tag except for one README line). The `master` branch
of this repository is that commit, untouched. Stolo work lands on `main`.

Every modified upstream file carries a line of the form
`// Modified by Stolo Systems Inc., <date>` near its licence header; new
files carry a Stolo copyright header under the same licence.

## Changes

| Date | Area | Change | Why |
|---|---|---|---|
| 2026-09-14 | Repo | Added `CHANGES-from-upstream.md`, `STOLO.md`, and a CI workflow that builds the T-Beam Supreme target from the pinned source and publishes the binary with its SHA-256. | GPL kit §4 (reproducible build + release manifest) and the F0 gate: the fork must build the unchanged target before any Stolo change lands. No firmware source is modified by this entry. |
| 2026-09-14 | `Makefile` | Added `flash-stolo-tbeam_supreme` (PORT/STOLO_PY/STOLO_RNODECONF variables): the upstream `upload-tbeam_supreme` sequence for a macOS bench (`/dev/cu.usbmodem*`, an explicit Python, no console-partition rewrite). Marked `Modified by Stolo Systems Inc., 2026-09-14`. | The upstream recipes assume Linux paths. The console partition is left alone until F9 regenerates the console image from this fork's source. |
| 2026-09-14 | `Bluetooth.h` (ESP32 BLE path), `Stolo.h`, `Makefile` | With `-DSTOLO_BUILD=1` (`make firmware-stolo-tbeam_supreme`): the pairing window is 120 s (`STOLO_BT_PAIRING_TIMEOUT`) instead of 35 s; a radio with **no bonds** boots straight into pairing and its window never closes; the bonded link is kept after a successful pairing instead of being dropped 2 s later. Without the flag the build is upstream's. `Bluetooth.h` marked `Modified by Stolo Systems Inc., 2026-09-14`. | F3 of the Stolo Node plan. Upstream's self-expiring window closed before users found the phone's Bluetooth sheet, and a unit with no bonds had no way to be paired without a cable; the post-bond disconnect made pairing and connecting two user steps. Passkey (MITM) pairing is unchanged. |
| 2026-09-14 | `StoloStore.h` (new), `StoloProtocol.h` (new), `Stolo.h`, `Framing.h`, `Utilities.h`, `RNode_Firmware.ino`, `Bluetooth.h`, `Tools/stolo_node_tool.py` (new), `Documentation/stolo-control-protocol.md` (new) | With `-DSTOLO_BUILD=1`: a versioned NVS config store (node key pair, owner, pairing window; A/B slots + CRC; upstream EEPROM untouched); the Stolo Control Protocol over KISS `0x7A` — HELLO/AUTH/ENROLL/FORGET_OWNER, GET/SET radio with a per-model band clamp, GET/SET WiFi, GET/SET Bluetooth, faults; input-source tagging in `buffer_serial`; a session gate on legacy KISS mutations from unauthenticated WiFi guests; boot-time (button held at power-on) owner-enrollment window. Marked files carry `Modified by Stolo Systems Inc., 2026-09-14`. | Plan milestones F2 and the USB half of F4. Without the flag the build is upstream's. |
| 2026-09-19 | `StoloProtocol.h`, `StoloStore.h`, `Stolo.h`, `RNode_Firmware.ino`, `Bluetooth.h`, `Remote.h`, `Tools/stolo_node_tool.py`, `Documentation/stolo-control-protocol.md`, `Tests/host/` (new), `Makefile` (`test-host`) | **The authority contract** (decision 2026-09-19, after the 14-finding review): a Bluetooth bond or USB cable grants access, the owner key grants configuration authority. A real session object bound to one connection (ends on BLE disconnect, WiFi client close, source change, `CMD_LEAVE`/`CMD_RESET`, idle timeout, ownership change). Three roles — owner / compat (unowned + present host) / guest — enforced **default-deny on legacy KISS and SCP alike**, sensitive reads included (`CFG_READ`/`ROM_READ`, which carry the WiFi PSK, are refused to guests). One radio policy consulted by SCP, the legacy frequency/power handlers and `startRadio()`. SCP v2: AUTH signs node key, nonce, epoch and carrier; ENROLL proofs are fresh; SET_* parse-then-commit-then-apply with link-ending actions last and an applied-tags byte in replies. Store: commit-a-copy, an ownership epoch floor (a slot below it is a rollback, not a recovery), recovery state instead of re-minting an unowned identity on unreadable slots, bootloader entropy enabled around first-boot key generation. Host tests against the real dispatcher: 91 checks. `Remote.h` newly marked `Modified by Stolo Systems Inc., 2026-09-19`. | Review findings F1 (global authorization), F2 (bonded clients bypass, PSK dump), F3 (band clamp SCP-only), F7 (store rollback), F8 (mutate-before-validate), F9 (transcript binding), F10 (entropy). Software verified; device qualification pending (testing queue). |
