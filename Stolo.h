// Copyright (C) 2026, Stolo Systems Inc.
// Part of Stolo Node Firmware, a fork of RNode Firmware
// (Copyright (C) 2024, Mark Qvist).
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

// Stolo's knobs and the few globals every build sees. Everything Stolo
// changes about upstream behaviour is compiled in only with
// -DSTOLO_BUILD=1 (the firmware-stolo-* Makefile targets); without it
// this header is inert and the build is upstream's. MCU-agnostic on
// purpose: it is included from Utilities.h for every board.

#ifndef STOLO_H
#define STOLO_H

#include <stdint.h>
#include "StoloBleTrace.h"

#define STOLO_FW_VERSION "0.2.1"

// Decided policy: once-only factory legacy compat, on by default.
// Set to 0 in product builds that require SCP enrollment from first boot.
#ifndef STOLO_ENABLE_FACTORY_COMPAT
#define STOLO_ENABLE_FACTORY_COMPAT 1
#endif

// The default presence-gated pairing window. Upstream's 35 s self-expiring
// window (BT_PAIRING_TIMEOUT) closed before a user had found the phone's
// Bluetooth sheet. Persisted and adjustable through the config store
// (StoloStore.h); this is only the value a fresh store starts with.
#define STOLO_BT_PAIRING_TIMEOUT 120000
uint32_t stolo_bt_window_ms();

// Which host the bytes now in the serial FIFO came from. Upstream funnels
// USB, BLE and WiFi into one FIFO and reads from exactly one of them per
// pass (buffer_serial), so tagging at push time is exact for the
// single-host design this fork inherits. The legacy-KISS mutation gate
// (StoloProtocol.h) reads it: a WiFi joiner that has not authenticated is
// a guest, and a guest may read the radio but not change it.
#define STOLO_SRC_USB  0
#define STOLO_SRC_BLE  1
#define STOLO_SRC_WIFI 2

#if defined(STOLO_BUILD)
// Session hooks (StoloProtocol.h), declared here because Bluetooth.h,
// Remote.h and the .ino call them before that header is included.
void stolo_note_source(uint8_t source);   // buffer_serial: where these bytes came from
void stolo_ble_connection_boundary();     // BLE task: defer parser teardown to loop
void stolo_host_disconnected();           // loop-side disconnect, WiFi close, LEAVE, RESET
bool stolo_radio_freq_allowed(uint32_t f);
bool stolo_radio_txp_allowed(int p);
bool stolo_kiss_freq_write(uint32_t f);
bool stolo_kiss_txp_write(int p);
#endif

#endif
