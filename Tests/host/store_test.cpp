// Copyright (C) 2026, Stolo Systems Inc. GPL-3.0-or-later; see LICENSE.
// StoloStore.h on an in-memory NVS: commit-a-copy, the ownership epoch
// floor (review F7), and recovery instead of re-minting. Software verified.
#include "firmware_env.h"

int main() {
  // Fresh boot mints an identity once.
  stolo_prefs = Preferences(); stolo_store_state = STOLO_STORE_FRESH; stolo_store_init();
  CHECK(stolo_store_ok && stolo_store_state == STOLO_STORE_LOADED && stolo_cfg.owner_enrolled == 0, "factory-new boot mints an unowned identity");
  uint8_t node_pub[32]; memcpy(node_pub, stolo_cfg.node_pub, 32);
  // Two owners in sequence; then corrupt the active slot.
  StoloConfig a = stolo_cfg; a.owner_enrolled = 1; a.owner_epoch = 1; a.owner_pub[0] = 10; CHECK(stolo_store_commit(&a), "commit owner 1");
  StoloConfig b = stolo_cfg; b.owner_epoch = 2; b.owner_pub[0] = 20; CHECK(stolo_store_commit(&b), "commit owner 2");
  CHECK(stolo_prefs.getUInt("epochfl") == 2, "the epoch floor follows the highest committed epoch");
  uint8_t active = stolo_prefs.getUChar("act"); stolo_prefs.data[stolo_slot_keys[active]][0] ^= 1;
  stolo_store_state = STOLO_STORE_FRESH; stolo_store_ok = false; stolo_store_init();
  CHECK(!stolo_store_ok && stolo_store_state == STOLO_STORE_RECOVERY, "a corrupt active slot does NOT fall back to the previous owner: recovery");
  CHECK(stolo_cfg.owner_pub[0] != 10 && stolo_cfg.owner_epoch != 1, "owner 1 did not come back");
  // Repair the active slot: loads owner 2 again.
  stolo_prefs.data[stolo_slot_keys[active]][0] ^= 1; stolo_store_state = STOLO_STORE_FRESH; stolo_store_init();
  CHECK(stolo_store_ok && stolo_cfg.owner_pub[0] == 20 && stolo_cfg.owner_epoch == 2, "a valid slot at or above the floor loads");
  CHECK(memcmp(stolo_cfg.node_pub, node_pub, 32) == 0, "node identity preserved throughout");
  // A valid slot below the floor is a rollback and is refused even if the other slot is corrupt.
  StoloConfig old = stolo_cfg; old.owner_epoch = 1; old.owner_pub[0] = 10; old.length = sizeof(StoloConfig); old.crc32 = stolo_cfg_crc(&old);
  for (auto key : stolo_slot_keys) stolo_prefs.putBytes(key, &old, sizeof(old));
  stolo_store_state = STOLO_STORE_FRESH; stolo_store_ok = false; stolo_store_init();
  CHECK(!stolo_store_ok && stolo_store_state == STOLO_STORE_RECOVERY, "slots below the epoch floor are refused: recovery");
  // Future schema → recovery, not fresh init.
  StoloConfig fut = b; fut.schema = 99; fut.length = sizeof(StoloConfig); fut.crc32 = stolo_cfg_crc(&fut);
  for (auto key : stolo_slot_keys) stolo_prefs.putBytes(key, &fut, sizeof(fut));
  stolo_store_state = STOLO_STORE_FRESH; stolo_store_ok = false; stolo_store_init();
  CHECK(!stolo_store_ok && stolo_store_state == STOLO_STORE_RECOVERY && stolo_cfg.owner_enrolled == 0, "an unknown schema enters recovery instead of minting a fresh unowned node");
  CHECK(!stolo_prefs.data.count("cfgZ"), "(sanity) no stray keys");
  // Commit failure leaves the live config untouched.
  stolo_prefs = Preferences(); stolo_store_state = STOLO_STORE_FRESH; stolo_store_init();
  StoloConfig c = stolo_cfg; c.owner_enrolled = 1; c.owner_epoch = 5; stolo_prefs.fail_writes = true;
  CHECK(!stolo_store_commit(&c) && stolo_cfg.owner_enrolled == 0 && stolo_cfg.owner_epoch == 0, "a failed commit changes nothing live");
  printf("%d passed, %d failed\n", passes, failures);
  return failures ? 1 : 0;
}
