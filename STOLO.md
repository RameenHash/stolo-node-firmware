# Stolo Node Firmware — working notes

Stolo Node Firmware is a GPL-3.0 fork of RNode Firmware for the Stolo Node,
an ESP32-S3 + SX1262 LoRa node (LilyGO T-Beam Supreme class for v1). The
plan, phases and exit criteria live in the app repository at
`gridx/docs/hardware/stolo-node-plan-2026-09-14.md`; this file is the
firmware-side crib sheet.

## Branches

- `master` — the pinned upstream commit, never edited here. Refresh it from
  `upstream` (`git fetch upstream && git checkout master && git merge --ff-only upstream/master`).
- `main` — Stolo's integration branch. Work lands by pull request from a
  feature branch; nothing is committed to `main` directly.

## Building the unchanged upstream target (F0)

The checkout directory **must be named `RNode_Firmware`**: the Arduino
toolchain requires the sketch folder to match `RNode_Firmware.ino`, and the
upstream Makefile builds the current directory. The GitHub repository is
`RameenHash/stolo-node-firmware`; clone it as
`git clone <url> RNode_Firmware`.

The upstream `Makefile` and `arduino-cli.yaml` are the build system; a second
build system would double what has to be published as Corresponding Source.

    make prep-esp32                 # once: ESP32 core + display/LED libraries
    make firmware-tbeam_supreme     # -> build/esp32.esp32.esp32s3/RNode_Firmware.ino.bin

On macOS `arduino-cli` keeps its data under `~/Library/Arduino15`, not the
`~/.arduino15` the upstream `release-*` recipes assume; the T-Beam Supreme
enumerates as `/dev/cu.usbmodem*` (ESP32-S3 native USB), not `/dev/ttyACM0`.
See `make flash-stolo-tbeam_supreme` (added in F0) for the macOS flash path.

## Flashing the bench unit

Flashing writes the application only. Provisioning (product, model, serial,
signature) lives in EEPROM and survives; the firmware-hash target the
signature check compares against must be updated after every flash, which is
what `rnodeconf --firmware-hash` does in the recipe. Keep the EEPROM backup
from `rnodeconf --eeprom-backup` before the first flash of any unit.

## Licence obligations

- Keep every upstream copyright and licence notice.
- Mark each modified upstream file with `// Modified by Stolo Systems Inc., <date>`.
- Record every change in `CHANGES-from-upstream.md`.
- Every release: source tarball, binary, `SHA256SUMS`, and a release manifest
  (see the CI workflow) — the materials `stolo.io/source/` serves.
