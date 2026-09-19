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
#ifndef STOLO_HOST_TEST
#include "bootloader_random.h"
#endif

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
// Why the store is not OK. A node in RECOVERY holds whatever identity it
// could read (or none) and refuses every configuration change until a
// visible physical recovery; it never silently mints a fresh, unowned
// identity over a store it could not understand (review 2026-09-19, F7).
#define STOLO_STORE_FRESH     0   // no slot ever written: factory-new
#define STOLO_STORE_LOADED    1
#define STOLO_STORE_RECOVERY  2   // slots present but none acceptable
uint8_t stolo_store_state = STOLO_STORE_FRESH;
Preferences stolo_prefs;
static const char* stolo_slot_keys[2] = {"cfgA", "cfgB"};
// The ownership generation floor: the highest owner_epoch ever committed,
// kept outside the A/B slots. A slot fallback after a corrupted write may
// only load a config whose epoch is at least this, so a previous owner can
// never return through storage recovery (review F7).
static const char* stolo_floor_key = "epochfl";

uint32_t stolo_cfg_crc(const StoloConfig* c) {
  return crc32_le(0, (const uint8_t*)c, sizeof(StoloConfig) - sizeof(uint32_t));
}

bool stolo_cfg_valid(const StoloConfig* c) {
  if (c->magic != STOLO_CFG_MAGIC) return false;
  if (c->schema < 1 || c->schema > STOLO_CFG_SCHEMA) return false;
  if (c->length != sizeof(StoloConfig)) return false;
  return c->crc32 == stolo_cfg_crc(c);
}

// Fill with hardware randomness at a point where the RF subsystem is not
// yet running (stolo_setup runs before bt_init and wifi_remote_init).
// ESP-IDF's RNG is only guaranteed to be truly random while an RF
// subsystem or the bootloader entropy source is enabled, and the
// bootloader disables its source before the app starts (review F10).
// bootloader_random_enable() conflicts with ADC/I2S use, so it is
// bracketed tightly around the fill. The host test stubs it.
void stolo_random_fill(uint8_t* out, size_t n) {
  #ifndef STOLO_HOST_TEST
    bootloader_random_enable();
  #endif
  esp_fill_random(out, n);
  #ifndef STOLO_HOST_TEST
    bootloader_random_disable();
  #endif
}

void stolo_cfg_defaults(StoloConfig* c) {
  memset(c, 0, sizeof(StoloConfig));
  c->magic = STOLO_CFG_MAGIC;
  c->schema = STOLO_CFG_SCHEMA;
  c->length = sizeof(StoloConfig);
  c->bt_window_s = STOLO_BT_PAIRING_TIMEOUT / 1000;
  // An Ed25519 private key is 32 random bytes; the public half derives.
  stolo_random_fill(c->node_priv, sizeof(c->node_priv));
  Ed25519::derivePublicKey(c->node_pub, c->node_priv);
}

// Commit `next` — a fully prepared copy of the configuration — durably,
// and only then make it the live configuration. Callers build the next
// state on a copy, validate it, call this, and touch RAM only on success,
// so a failed write leaves the node exactly as it was (review F8).
//
// Write to the slot that is NOT active, read it back, move the activation
// record, then raise the epoch floor. Whichever slot the record names is
// complete; the floor only ever goes up.
bool stolo_store_commit(StoloConfig* next) {
  uint8_t active = stolo_prefs.getUChar("act", 0) & 1;
  uint8_t target = 1 - active;
  next->generation = stolo_cfg.generation + 1;
  next->length = sizeof(StoloConfig);
  next->crc32 = stolo_cfg_crc(next);
  if (stolo_prefs.putBytes(stolo_slot_keys[target], next, sizeof(StoloConfig)) != sizeof(StoloConfig)) return false;
  StoloConfig check;
  if (stolo_prefs.getBytes(stolo_slot_keys[target], &check, sizeof(StoloConfig)) != sizeof(StoloConfig)) return false;
  if (memcmp(&check, next, sizeof(StoloConfig)) != 0) return false;
  if (stolo_prefs.putUChar("act", target) != 1) return false;
  uint32_t floor = stolo_prefs.getUInt(stolo_floor_key, 0);
  if (next->owner_epoch > floor) stolo_prefs.putUInt(stolo_floor_key, next->owner_epoch);
  stolo_cfg = *next;
  return true;
}

// Legacy entry point: commit the live configuration as it stands.
bool stolo_store_save() {
  StoloConfig next = stolo_cfg;
  return stolo_store_commit(&next);
}

bool stolo_store_load() {
  uint8_t active = stolo_prefs.getUChar("act", 0) & 1;
  uint8_t order[2] = {active, (uint8_t)(1 - active)};
  uint32_t floor = stolo_prefs.getUInt(stolo_floor_key, 0);
  bool any_slot = false;
  for (int i = 0; i < 2; i++) {
    StoloConfig candidate;
    size_t got = stolo_prefs.getBytes(stolo_slot_keys[order[i]], &candidate, sizeof(StoloConfig));
    if (got != 0) any_slot = true;
    if (got != sizeof(StoloConfig) || !stolo_cfg_valid(&candidate)) continue;
    // A valid but OLDER ownership generation is a rollback, not a recovery:
    // refuse it rather than let a previous owner regain authority.
    if (candidate.owner_epoch < floor) continue;
    stolo_cfg = candidate;
    stolo_store_state = STOLO_STORE_LOADED;
    return true;
  }
  stolo_store_state = any_slot ? STOLO_STORE_RECOVERY : STOLO_STORE_FRESH;
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
  } else if (stolo_store_state == STOLO_STORE_FRESH) {
    // Factory-new: no slot has ever been written. Mint the identity once.
    StoloConfig fresh; stolo_cfg_defaults(&fresh);
    stolo_cfg = fresh; stolo_cfg.generation = 0;
    stolo_store_ok = stolo_store_commit(&fresh);
    stolo_store_state = stolo_store_ok ? STOLO_STORE_LOADED : STOLO_STORE_RECOVERY;
  } else {
    // Slots exist but none is acceptable (corrupt, a schema this firmware
    // does not know, or an ownership rollback). Do NOT create a fresh
    // unowned identity over it: hold a zeroed config, report store_ok = 0
    // in HELLO, refuse configuration, and wait for physical recovery.
    memset(&stolo_cfg, 0, sizeof(StoloConfig));
    stolo_store_ok = false;
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
