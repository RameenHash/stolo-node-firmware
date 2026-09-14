// Copyright (C) 2026, Stolo Systems Inc.
// Part of Stolo Node Firmware, a fork of RNode Firmware
// (Copyright (C) 2024, Mark Qvist). GNU GPL v3 or later; see LICENSE.

// The Stolo config store (plan milestone F2): ONE versioned blob in NVS,
// written to alternating slots with a CRC and an activation record, so a
// power cut mid-write can never lose the node key or the owner. The
// upstream EEPROM image (provisioning, radio config, WiFi credentials) is
// not touched — rnodeconf and RNS keep reading what they always did.

#ifndef STOLO_STORE_H
#define STOLO_STORE_H

#if defined(STOLO_BUILD) && MCU_VARIANT == MCU_ESP32

#include <Preferences.h>
#include <Ed25519.h>
#include "esp_system.h"
#include "esp32/rom/crc.h"

#define STOLO_CFG_MAGIC  0x4F4C5453UL  // "STLO"
#define STOLO_CFG_SCHEMA 1

struct __attribute__((packed)) StoloConfig {
  uint32_t magic;
  uint16_t schema;
  uint16_t length;
  uint32_t generation;
  // The node's own identity: generated on first boot, never leaves the
  // device. The public half is what the app binds an owner to.
  uint8_t  node_priv[32];
  uint8_t  node_pub[32];
  // The Stolo identity that controls this node (not the GPL owner-key of
  // milestone F8, which is a firmware SIGNING key and lives elsewhere).
  uint8_t  owner_pub[32];
  uint8_t  owner_enrolled;
  uint32_t owner_epoch;
  uint16_t bt_window_s;
  uint8_t  reserved[64];
  uint32_t crc32;
};

StoloConfig stolo_cfg;
bool stolo_store_ok = false;
Preferences stolo_prefs;
static const char* stolo_slot_keys[2] = {"cfgA", "cfgB"};

uint32_t stolo_cfg_crc(const StoloConfig* c) {
  return crc32_le(0, (const uint8_t*)c, sizeof(StoloConfig) - sizeof(uint32_t));
}

bool stolo_cfg_valid(const StoloConfig* c) {
  if (c->magic != STOLO_CFG_MAGIC) return false;
  if (c->schema < 1 || c->schema > STOLO_CFG_SCHEMA) return false;
  if (c->length != sizeof(StoloConfig)) return false;
  return c->crc32 == stolo_cfg_crc(c);
}

void stolo_cfg_defaults(StoloConfig* c) {
  memset(c, 0, sizeof(StoloConfig));
  c->magic = STOLO_CFG_MAGIC;
  c->schema = STOLO_CFG_SCHEMA;
  c->length = sizeof(StoloConfig);
  c->bt_window_s = STOLO_BT_PAIRING_TIMEOUT / 1000;
  // An Ed25519 private key is 32 random bytes; the public half derives.
  esp_fill_random(c->node_priv, sizeof(c->node_priv));
  Ed25519::derivePublicKey(c->node_pub, c->node_priv);
}

// Write to the slot that is NOT active, read it back, and only then move
// the activation record. Whichever slot the record names is complete.
bool stolo_store_save() {
  uint8_t active = stolo_prefs.getUChar("act", 0) & 1;
  uint8_t target = 1 - active;
  stolo_cfg.generation++;
  stolo_cfg.length = sizeof(StoloConfig);
  stolo_cfg.crc32 = stolo_cfg_crc(&stolo_cfg);
  if (stolo_prefs.putBytes(stolo_slot_keys[target], &stolo_cfg, sizeof(StoloConfig)) != sizeof(StoloConfig)) return false;
  StoloConfig check;
  if (stolo_prefs.getBytes(stolo_slot_keys[target], &check, sizeof(StoloConfig)) != sizeof(StoloConfig)) return false;
  if (memcmp(&check, &stolo_cfg, sizeof(StoloConfig)) != 0) return false;
  return stolo_prefs.putUChar("act", target) == 1;
}

bool stolo_store_load() {
  uint8_t active = stolo_prefs.getUChar("act", 0) & 1;
  uint8_t order[2] = {active, (uint8_t)(1 - active)};
  for (int i = 0; i < 2; i++) {
    StoloConfig candidate;
    if (stolo_prefs.getBytes(stolo_slot_keys[order[i]], &candidate, sizeof(StoloConfig)) == sizeof(StoloConfig)
        && stolo_cfg_valid(&candidate)) {
      stolo_cfg = candidate;
      return true;
    }
  }
  return false;
}

// The last eight reset reasons, oldest first, for the diagnostics report.
// Cheap, and the difference between "my radio sometimes stops" and a bug.
uint8_t stolo_faults[8];
uint8_t stolo_fault_count = 0;

void stolo_fault_record() {
  // ring[0] = next write index, ring[1] = entries recorded (max 8), then 8 reasons.
  uint8_t ring[10] = {0};
  stolo_prefs.getBytes("faults", ring, sizeof(ring));
  uint8_t idx = ring[0] % 8, count = ring[1] > 8 ? 8 : ring[1];
  ring[2 + idx] = (uint8_t)esp_reset_reason();
  ring[0] = (idx + 1) % 8;
  if (count < 8) count++;
  ring[1] = count;
  stolo_prefs.putBytes("faults", ring, sizeof(ring));
  // Oldest first.
  for (int i = 0; i < count; i++) stolo_faults[i] = ring[2 + ((ring[0] + 8 - count + i) % 8)];
  stolo_fault_count = count;
}

void stolo_store_init() {
  stolo_prefs.begin("stolo", false);
  stolo_fault_record();
  if (stolo_store_load()) {
    stolo_store_ok = true;
  } else {
    stolo_cfg_defaults(&stolo_cfg);
    stolo_store_ok = stolo_store_save();
  }
}

uint32_t stolo_bt_window_ms() {
  if (stolo_store_ok && stolo_cfg.bt_window_s >= 10) return (uint32_t)stolo_cfg.bt_window_s * 1000UL;
  return STOLO_BT_PAIRING_TIMEOUT;
}

#else
uint32_t stolo_bt_window_ms() { return STOLO_BT_PAIRING_TIMEOUT; }
#endif

#endif
