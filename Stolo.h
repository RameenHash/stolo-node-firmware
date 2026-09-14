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

// Stolo's knobs, in one place. Everything Stolo changes about upstream
// behaviour is compiled in only with -DSTOLO_BUILD=1 (the
// firmware-stolo-* Makefile targets); without it this file is inert and
// the build is upstream's.

#ifndef STOLO_H
#define STOLO_H

#define STOLO_FW_VERSION "0.1.0"

// How long a presence-gated pairing window stays open once the button
// opens it. Upstream's 35 s self-expiring window (BT_PAIRING_TIMEOUT)
// closed before a user had found the phone's Bluetooth sheet, and a radio
// with no bonds then had no way to be paired without a cable. A radio
// with NO bonds does not close the window at all — see update_bt().
#define STOLO_BT_PAIRING_TIMEOUT 120000

#endif
