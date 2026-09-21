// Copyright (C) 2026, Stolo Systems Inc. GPL-3.0-or-later; see LICENSE.
#ifndef STOLO_DISPLAY_H
#define STOLO_DISPLAY_H
#include <stdint.h>

enum StoloBannerKind {
  STOLO_BANNER_RADIO, STOLO_BANNER_PAIRING, STOLO_BANNER_RESCUE,
  STOLO_BANNER_NEW_IDENTITY, STOLO_BANNER_ENROLL, STOLO_BANNER_STORE_ERROR
};
struct StoloBanner { StoloBannerKind kind; uint8_t seconds; };

// Keep faults visible alongside Stolo notices. Plain builds pass false and
// retain the upstream lower-band error precedence, including over pairing.
inline int8_t stolo_fault_y(bool hardware_fault, bool priority_notice) {
  return !hardware_fault ? -1 : priority_notice ? 0 : 37;
}

// Window-open comes from the protocol, so display code never grants presence.
// Unsigned subtraction also handles deadlines/timestamps crossing millis wrap.
inline StoloBanner stolo_select_banner(uint32_t now, bool pairing_pin,
    bool recovery, bool window_open, uint32_t window_until,
    bool identity_notice, uint32_t identity_since) {
  if (pairing_pin) return {STOLO_BANNER_PAIRING, 0};
  const uint32_t remaining = window_until - now;
  // Saturate before narrowing: the renderer has exactly two decimal glyphs.
  const uint8_t seconds = window_open && (int32_t)remaining > 0
      ? (uint8_t)(remaining > 99000 ? 99 : (remaining + 999) / 1000) : 0;
  if (recovery && window_open) return {STOLO_BANNER_RESCUE, seconds};
  if (identity_notice && (uint32_t)(now - identity_since) < 5000)
    return {STOLO_BANNER_NEW_IDENTITY, 0};
  if (window_open) return {STOLO_BANNER_ENROLL, seconds};
  if (recovery) return {STOLO_BANNER_STORE_ERROR, 0};
  return {STOLO_BANNER_RADIO, 0};
}

// HELLO's former attestation byte is advisory build capability only.
// No statement about hardware health, secure boot, or firmware authenticity.
inline uint8_t stolo_display_capabilities() {
  #if defined(STOLO_BUILD) && MCU_VARIANT == MCU_ESP32 && HAS_DISPLAY
    return 0x03;  // bit0: display present in build; bit1: window UI implemented
  #else
    return 0x00;
  #endif
}

#if defined(STOLO_BUILD) && MCU_VARIANT == MCU_ESP32
// Implemented beside protocol state, after Display.h in the firmware includes.
StoloBanner stolo_current_banner(uint32_t now, bool pairing_pin);
#endif
#endif
