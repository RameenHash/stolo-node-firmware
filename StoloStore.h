// Copyright (C) 2026, Stolo Systems Inc.
// Part of Stolo Node Firmware, a fork of RNode Firmware
// (Copyright (C) 2024, Mark Qvist). GNU GPL v3 or later; see LICENSE.

// The Stolo config store (plan milestone F2): ONE versioned blob in NVS,
// written to alternating slots with a CRC and an activation record. The
// restart policy is explicit and ambiguous storage enters RECOVERY. The
// upstream EEPROM image (provisioning, radio config, WiFi credentials) is
// not touched — rnodeconf and RNS keep reading what they always did.

#ifndef STOLO_STORE_H
#define STOLO_STORE_H

#if defined(STOLO_BUILD) && MCU_VARIANT == MCU_ESP32

#include <Preferences.h>
#include <Ed25519.h>
#include "esp_system.h"
#include "esp32/rom/crc.h"
#include "bootloader_random.h"
#include <mbedtls/hmac_drbg.h>

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
  uint8_t  ever_enrolled; // formerly reserved[0], preserves schema-1 record size
  uint8_t  reserved[63];
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

bool stolo_ever_enrolled() {
  // Pre-flag schema-1 stores already record enrollment history in epoch.
  return stolo_cfg.ever_enrolled != 0 || stolo_cfg.owner_enrolled || stolo_cfg.owner_epoch != 0;
}

uint32_t stolo_cfg_crc(const StoloConfig* c) {
  return crc32_le(0, (const uint8_t*)c, sizeof(StoloConfig) - sizeof(uint32_t));
}

bool stolo_cfg_valid(const StoloConfig* c) {
  if (c->magic != STOLO_CFG_MAGIC) return false;
  if (c->schema < 1 || c->schema > STOLO_CFG_SCHEMA) return false;
  if (c->length != sizeof(StoloConfig)) return false;
  return c->crc32 == stolo_cfg_crc(c);
}

// Boot entropy is enabled only before RF/ADC startup. A standard HMAC-DRBG
// seeded here supplies runtime nonces, including USB with both RF stacks off.
mbedtls_hmac_drbg_context stolo_drbg;
bool stolo_drbg_ready = false;

void stolo_boot_entropy_fill(uint8_t* out, size_t n) {
  bootloader_random_enable();
  esp_fill_random(out, n);
  bootloader_random_disable();
}

bool stolo_random_fill(uint8_t* out, size_t n) {
  return stolo_drbg_ready && mbedtls_hmac_drbg_random(&stolo_drbg, out, n) == 0;
}

// Called at boot before bt_init/wifi_remote_init, including on an owned
// store so runtime randomness is seeded on EVERY boot. Later rescue calls
// reuse the DRBG; they never enable boot-time entropy with peripherals up.
bool stolo_cfg_defaults(StoloConfig* c) {
  if (!stolo_drbg_ready) {
    uint8_t seed[48];
    stolo_boot_entropy_fill(seed, sizeof(seed));
    mbedtls_hmac_drbg_init(&stolo_drbg);
    int result = mbedtls_hmac_drbg_seed_buf(&stolo_drbg,
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), seed, sizeof(seed));
    memset(seed, 0, sizeof(seed));
    if (result != 0) return false;
    // seed_buf uses no entropy callback and does not perform auto-reseeding.
    stolo_drbg_ready = true;
  }
  memset(c, 0, sizeof(StoloConfig));
  c->magic = STOLO_CFG_MAGIC;
  c->schema = STOLO_CFG_SCHEMA;
  c->length = sizeof(StoloConfig);
  c->bt_window_s = STOLO_BT_PAIRING_TIMEOUT / 1000;
  if (!stolo_random_fill(c->node_priv, sizeof(c->node_priv))) return false;
  Ed25519::derivePublicKey(c->node_pub, c->node_priv);
  return true;
}

// Commit order: slot -> readback -> checked epoch floor -> activation -> RAM.
// An interrupted slot write retains the old activation/floor. Once the
// floor advances, restart can only load the new ownership epoch or RECOVERY.
// Activation failure after the floor moves revokes live configuration access.
bool stolo_store_commit(StoloConfig* next) {
  uint8_t active = stolo_prefs.isKey("act") ? stolo_prefs.getUChar("act", 0xFF) : 0;
  if (active > 1 || next->owner_epoch == UINT32_MAX) return false;
  uint8_t target = 1 - active;
  uint32_t floor = stolo_prefs.isKey(stolo_floor_key) ? stolo_prefs.getUInt(stolo_floor_key, UINT32_MAX) : 0;
  if (next->owner_epoch < floor) return false;
  if (stolo_ever_enrolled() || next->owner_enrolled || next->owner_epoch) next->ever_enrolled = 1;
  next->generation = stolo_cfg.generation + 1;
  next->length = sizeof(StoloConfig);
  next->crc32 = stolo_cfg_crc(next);
  if (stolo_prefs.putBytes(stolo_slot_keys[target], next, sizeof(StoloConfig)) != sizeof(StoloConfig)) return false;
  StoloConfig check;
  if (stolo_prefs.getBytes(stolo_slot_keys[target], &check, sizeof(StoloConfig)) != sizeof(StoloConfig)) return false;
  if (memcmp(&check, next, sizeof(StoloConfig)) != 0) return false;
  if (!stolo_prefs.isKey(stolo_floor_key) || next->owner_epoch > floor) {
    if (stolo_prefs.putUInt(stolo_floor_key, next->owner_epoch) != sizeof(uint32_t)) return false;
  }
  if (stolo_prefs.putUChar("act", target) != 1) {
    stolo_store_ok = false;
    stolo_store_state = STOLO_STORE_RECOVERY;
    return false;
  }
  stolo_cfg = *next;
  return true;
}

bool stolo_store_save() {
  StoloConfig next = stolo_cfg;
  return stolo_store_commit(&next);
}

bool stolo_store_load() {
  stolo_store_state = STOLO_STORE_RECOVERY;
  if (stolo_prefs.isKey("rescue")) return false; // interrupted explicit rescue stays visible/recoverable
  bool has_floor = stolo_prefs.isKey(stolo_floor_key);
  bool has_act = stolo_prefs.isKey("act");
  bool present[2] = {stolo_prefs.isKey("cfgA"), stolo_prefs.isKey("cfgB")};
  if (!present[0] && !present[1] && !has_floor && !has_act) {
    stolo_store_state = STOLO_STORE_FRESH;
    return false;
  }
  StoloConfig slots[2];
  for (int i = 0; i < 2; ++i) {
    if (!present[i]) continue;
    // getBytes() returns zero for oversize blobs AND read failures. Existence
    // must come from the key, never from the number of bytes returned.
    if (stolo_prefs.getBytesLength(stolo_slot_keys[i]) != sizeof(StoloConfig)
        || stolo_prefs.getBytes(stolo_slot_keys[i], &slots[i], sizeof(StoloConfig)) != sizeof(StoloConfig)
        || !stolo_cfg_valid(&slots[i])) return false;
  }
  if (!present[0] && !present[1]) return false;
  uint8_t active = stolo_prefs.getUChar("act", 0xFF);
  if (has_act && (active > 1 || !present[active])) return false;
  int chosen = -1;
  if (!has_floor) {
    // Pre-floor upgrade: choose the highest valid ownership epoch, then
    // generation, even if activation still points at the older slot.
    for (int i = 0; i < 2; ++i) if (present[i] && (chosen < 0
        || slots[i].owner_epoch > slots[chosen].owner_epoch
        || (slots[i].owner_epoch == slots[chosen].owner_epoch && slots[i].generation > slots[chosen].generation))) chosen = i;
    if (slots[chosen].owner_epoch == UINT32_MAX
        || stolo_prefs.putUInt(stolo_floor_key, slots[chosen].owner_epoch) != sizeof(uint32_t)) return false;
  } else {
    // With modern ownership metadata a missing activation is ambiguous.
    if (!has_act) return false;
    uint32_t floor = stolo_prefs.getUInt(stolo_floor_key, UINT32_MAX);
    if (floor == UINT32_MAX) return false;
    if (slots[active].owner_epoch >= floor) chosen = active;
    else {
      int other = 1 - active;
      if (present[other] && slots[other].owner_epoch >= floor) chosen = other;
    }
    if (chosen < 0) return false;
  }
  // Complete an interrupted activation (or an old-store migration).
  if ((!has_act || chosen != active) && stolo_prefs.putUChar("act", chosen) != 1) return false;
  stolo_cfg = slots[chosen];
  if (stolo_ever_enrolled()) stolo_cfg.ever_enrolled = 1;
  stolo_store_state = STOLO_STORE_LOADED;
  return true;
}

// Only the physical-window SCP_RESCUE handler may call this. A durable
// marker prevents an interrupted erase from looking like a factory-new store.
bool stolo_store_rescue() {
  StoloConfig fresh;
  if (!stolo_drbg_ready || !stolo_cfg_defaults(&fresh)) return false;
  fresh.ever_enrolled = 1; // unknown history: rescue stays SCP-enrollment-only
  // Also allow explicit retry after an initial namespace-open failure.
  stolo_prefs.end();
  if (!stolo_prefs.begin("stolo", false)) return false;
  if (stolo_prefs.putUChar("rescue", 1) != 1) return false;
  stolo_store_ok = false;
  stolo_store_state = STOLO_STORE_RECOVERY;
  const char* keys[] = {"cfgA", "cfgB", "act", "epochfl"};
  for (auto key : keys) if (stolo_prefs.isKey(key) && !stolo_prefs.remove(key)) return false;
  memset(&stolo_cfg, 0, sizeof(stolo_cfg));
  if (!stolo_store_commit(&fresh) || !stolo_prefs.remove("rescue")) return false;
  stolo_store_ok = true;
  stolo_store_state = STOLO_STORE_LOADED;
  return true;
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
  stolo_store_ok = false;
  StoloConfig fresh;
  // Seed runtime entropy even if NVS cannot open: HELLO and explicit rescue
  // must remain available while the storage error is being diagnosed.
  if (!stolo_cfg_defaults(&fresh)) {
    stolo_store_state = STOLO_STORE_RECOVERY;
    return;
  }
  if (!stolo_prefs.begin("stolo", false)) {
    memset(&stolo_cfg, 0, sizeof(stolo_cfg));
    stolo_store_state = STOLO_STORE_RECOVERY;
    return;
  }
  stolo_fault_record();
  if (stolo_store_load()) {
    stolo_store_ok = true;
  } else if (stolo_store_state == STOLO_STORE_FRESH) {
    // Factory-new: no slot has ever been written. Mint the identity once.
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
