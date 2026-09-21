// Copyright (C) 2026, Stolo Systems Inc.
// SPDX-License-Identifier: GPL-3.0-only
#ifndef STOLO_BLE_TRACE_H
#define STOLO_BLE_TRACE_H

// Bench-only plaintext on USB; deliberately off in normal KISS/SCP builds.
// No passkeys, peer addresses, key material or application payloads are logged.
#if defined(STOLO_BUILD) && defined(STOLO_BLE_TRACE) && STOLO_BLE_TRACE
#include <stdint.h>
void stolo_ble_trace(const char* event, uint32_t detail);
#define STOLO_BT_TRACE(event, detail) stolo_ble_trace(event, detail)
#else
#define STOLO_BT_TRACE(event, detail) ((void)0)
#endif

#endif
