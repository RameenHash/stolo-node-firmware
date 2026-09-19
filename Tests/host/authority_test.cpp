// Copyright (C) 2026, Stolo Systems Inc. GPL-3.0-or-later; see LICENSE.
// The authority contract (decision 2026-09-19), tested against the REAL
// dispatcher in StoloProtocol.h with hardware/crypto/storage stubs.
// Covers review findings F1 (session binding), F2 (default-deny across
// KISS and SCP, sensitive reads), F3 (one radio policy), F8 (transactions),
// F9 (transcript binding, enrollment freshness). Software verified only.
#include "firmware_env.h"

static void test_guest_kiss_default_deny() {
  boot_owned(); as_source(STOLO_SRC_BLE); ble_authenticated = true;   // a bonded phone, no owner auth
  CHECK(stolo_role() == STOLO_ROLE_GUEST, "a bonded BLE client on an owned node is a guest");
  CHECK(stolo_kiss_gate(CMD_DATA), "guest: data frames pass");
  CHECK(stolo_kiss_gate(CMD_STAT_RX) && stolo_kiss_gate(CMD_STAT_BAT) && stolo_kiss_gate(CMD_FW_VERSION) && stolo_kiss_gate(CMD_DETECT), "guest: telemetry and identity reads pass");
  reset_out(); CHECK(!stolo_kiss_gate(CMD_FREQUENCY) && called("kiss_indicate_frequency"), "guest: frequency write refused with the current value echoed");
  reset_out(); CHECK(!stolo_kiss_gate(CMD_RADIO_STATE) && called("kiss_indicate_radiostate"), "guest: radio state write refused with echo");
  CHECK(!stolo_kiss_gate(CMD_CFG_READ), "guest: config-area dump (contains the PSK) is refused");
  CHECK(!stolo_kiss_gate(CMD_ROM_READ), "guest: ROM dump is refused");
  CHECK(!stolo_kiss_gate(CMD_WIFI_PSK) && !stolo_kiss_gate(CMD_BT_UNPAIR) && !stolo_kiss_gate(CMD_CONF_SAVE), "guest: credential, debond and persistence writes refused");
  CHECK(!stolo_kiss_gate(CMD_IMPLICIT) && !stolo_kiss_gate(CMD_PROMISC), "guest: mutations that the old blacklist missed are refused (default-deny)");
  as_source(STOLO_SRC_USB);
  CHECK(stolo_role() == STOLO_ROLE_GUEST && !stolo_kiss_gate(CMD_FREQUENCY), "USB attachment alone authorizes nothing on an owned node");
  as_source(STOLO_SRC_WIFI);
  CHECK(!stolo_kiss_gate(CMD_CFG_READ) && !stolo_kiss_gate(CMD_IMPLICIT), "WiFi guest: same policy");
}

static void test_unowned_compat() {
  boot_unowned(); as_source(STOLO_SRC_USB);
  CHECK(stolo_role() == STOLO_ROLE_COMPAT, "factory-unowned + USB = compatibility mode");
  CHECK(stolo_kiss_gate(CMD_FREQUENCY) && stolo_kiss_gate(CMD_CONF_SAVE), "compat: a stock client can configure a brand-new radio");
  as_source(STOLO_SRC_BLE); ble_authenticated = false;
  CHECK(stolo_role() == STOLO_ROLE_GUEST, "unowned + BLE link that is not encrypted/bonded = guest");
  ble_authenticated = true;
  CHECK(stolo_role() == STOLO_ROLE_COMPAT, "unowned + bonded BLE = compat");
  as_source(STOLO_SRC_WIFI);
  CHECK(stolo_role() == STOLO_ROLE_GUEST, "unowned + WiFi = guest (WiFi is never presence)");
  // The first ENROLL ends compat.
  as_source(STOLO_SRC_USB); scp(SCP_HELLO);
  std::vector<uint8_t> enroll(96, 0x11); scp(SCP_ENROLL, enroll, 2);
  CHECK(replied(SCP_ENROLL | SCP_REPLY) && stolo_cfg.owner_enrolled == 1, "compat host can enroll the first owner");
  CHECK(stolo_role() == STOLO_ROLE_GUEST && !stolo_kiss_gate(CMD_FREQUENCY), "after the first ENROLL the same cable is a guest");
}

static void test_owner_session_binding() {
  boot_owned(); as_source(STOLO_SRC_USB); become_owner();
  CHECK(replied(SCP_AUTH | SCP_REPLY) && last_body()[0] == 1 && stolo_is_owner(), "AUTH on USB makes this connection the owner");
  CHECK(stolo_kiss_gate(CMD_FREQUENCY) && stolo_kiss_gate(CMD_CFG_READ), "owner: legacy writes and sensitive reads pass");
  // F1: a source change is a new host.
  as_source(STOLO_SRC_WIFI);
  CHECK(!stolo_is_owner() && stolo_role() == STOLO_ROLE_GUEST, "authorization does NOT survive a source change");
  CHECK(!stolo_kiss_gate(CMD_FREQUENCY), "the next source cannot retune over KISS");
  std::vector<uint8_t> sf = {SCP_R_SF, 1, 9}; scp(SCP_SET_RADIO, sf, 3);
  CHECK(errored(SCP_ERR_NOT_SUPPORTED), "the next source cannot SET_RADIO without its own HELLO+AUTH");
  CHECK(lora_sf == 8, "nothing changed");
  // Disconnect ends the session even with the same source label.
  as_source(STOLO_SRC_USB); become_owner(); CHECK(stolo_is_owner(), "re-authenticated on USB");
  stolo_host_disconnected();
  CHECK(!stolo_is_owner(), "host disconnect ends authorization");
  become_owner();
  CHECK(stolo_kiss_gate(CMD_LEAVE) && stolo_is_owner(), "LEAVE gate checks authority before execution");
  CHECK(stolo_kiss_gate(CMD_RESET) && stolo_is_owner(), "RESET gate permits the owner before execution");
  // Idle timeout.
  become_owner(); fake_millis += STOLO_SESSION_IDLE_MS + 1; stolo_update();
  CHECK(!stolo_is_owner(), "an idle owner session expires");
  // A second HELLO from the same connection drops authority (a fresh session, fresh nonce).
  become_owner(); scp(SCP_HELLO);
  CHECK(!stolo_is_owner(), "HELLO restarts the session without authority");
  // Ownership generation binding.
  become_owner(); StoloConfig next = stolo_cfg; next.owner_epoch++; CHECK(stolo_store_commit(&next), "epoch bump committed");
  CHECK(!stolo_is_owner(), "an ownership change voids authorization even on the same connection");
}

static void test_auth_transcript_and_freshness() {
  boot_owned(9); as_source(STOLO_SRC_BLE); ble_authenticated = true;
  scp(SCP_HELLO); auto hb = last_body(); std::vector<uint8_t> nonce(hb.end() - 17, hb.end() - 1);
  std::vector<uint8_t> sig(64, 1); scp(SCP_AUTH, sig, 2);
  // v2 transcript: "stolo-auth-v2" || node_pub || nonce || epoch || source
  CHECK(ed_last_msg.size() == 13 + 32 + 16 + 4 + 1, "AUTH signs the v2 transcript length");
  CHECK(memcmp(ed_last_msg.data(), "stolo-auth-v2", 13) == 0, "AUTH domain string");
  CHECK(memcmp(ed_last_msg.data() + 13, stolo_cfg.node_pub, 32) == 0, "AUTH binds node_pub (no cross-node relay)");
  CHECK(std::equal(nonce.begin(), nonce.end(), ed_last_msg.begin() + 45), "AUTH binds the HELLO nonce");
  CHECK(ed_last_msg[61] == 0 && ed_last_msg[62] == 0 && ed_last_msg[63] == 0 && ed_last_msg[64] == 9, "AUTH binds the owner epoch");
  CHECK(ed_last_msg[65] == STOLO_SRC_BLE, "AUTH binds the carrier");
  CHECK(stolo_is_owner(), "and succeeds");
  // Nonce is single use: the transcript a second AUTH must sign carries a DIFFERENT nonce,
  // so a captured signature cannot be replayed (the stub accepts any signature; the
  // transcript is what a real verifier would check).
  std::vector<uint8_t> first_nonce(ed_last_msg.begin() + 45, ed_last_msg.begin() + 61);
  scp(SCP_AUTH, sig, 3);
  std::vector<uint8_t> second_nonce(ed_last_msg.begin() + 45, ed_last_msg.begin() + 61);
  CHECK(first_nonce != second_nonce, "every AUTH signs a fresh nonce: a captured signature is useless");
  scp(SCP_HELLO); ed_verify_result = false; scp(SCP_AUTH, sig, 30); ed_verify_result = true;
  scp(SCP_AUTH, sig, 31);
  CHECK(errored(SCP_ERR_UNAUTHORIZED), "after a rejected AUTH there is no live nonce: AUTH is refused until HELLO");
  // A failed AUTH clears authority.
  scp(SCP_HELLO); ed_verify_result = false; scp(SCP_AUTH, sig, 4);
  CHECK(!stolo_is_owner() && last_body()[0] == 0, "a rejected signature leaves the session unauthorized");
  ed_verify_result = true;
  // ENROLL freshness: transcript carries the nonce; replay without HELLO fails.
  boot_owned(9); as_source(STOLO_SRC_USB); stolo_enroll_window_until = millis() + 60000;   // physical window open
  scp(SCP_HELLO); std::vector<uint8_t> enroll(96, 0x22); scp(SCP_ENROLL, enroll, 2);
  CHECK(replied(SCP_ENROLL | SCP_REPLY), "ENROLL inside the physical window succeeds");
  CHECK(ed_last_msg.size() == 15 + 32 + 32 + 16 + 4 && memcmp(ed_last_msg.data(), "stolo-enroll-v2", 15) == 0, "ENROLL signs the v2 transcript with a nonce and epoch");
  CHECK(stolo_cfg.owner_epoch == 10 && called("bt_debond_all"), "ENROLL bumps the epoch and wipes the previous owner's bonds");
  stolo_enroll_window_until = millis() + 60000;
  scp(SCP_ENROLL, enroll, 3);
  CHECK(errored(SCP_ERR_BAD_REQUEST) || errored(SCP_ERR_UNAUTHORIZED), "a replayed ENROLL proof after the session reset is refused");
  CHECK(stolo_cfg.owner_epoch == 10, "and changes nothing");
}

static void test_enroll_rules() {
  boot_owned(); as_source(STOLO_SRC_USB); scp(SCP_HELLO);
  std::vector<uint8_t> enroll(96, 0x33); scp(SCP_ENROLL, enroll, 2);
  CHECK(errored(SCP_ERR_ENROLL_CLOSED), "owned node, no window, not owner: enrollment closed");
  CHECK(stolo_cfg.owner_pub[0] == 0xAB, "owner unchanged");
  // Hand-over by the current owner, no physical window: HELLO → AUTH → ENROLL.
  become_owner();
  { auto b = last_body(); CHECK(b.size() == 18 && b[0] == 1, "a successful AUTH reply carries a fresh nonce for a following ENROLL"); }
  CHECK(stolo_session.nonce_live, "the nonce issued by AUTH is live");
  scp(SCP_ENROLL, enroll, 3);
  CHECK(replied(SCP_ENROLL | SCP_REPLY) && stolo_cfg.owner_pub[0] == 0x33 && stolo_cfg.owner_epoch == 8, "the owner hands the node to a new key without a HELLO in between");
  CHECK(!stolo_is_owner() && called("bt_debond_all"), "after hand-over the old session has no authority and old bonds are gone");
  // A failed AUTH does not issue a nonce.
  ed_verify_result = false; scp(SCP_HELLO); std::vector<uint8_t> sig(64, 1); scp(SCP_AUTH, sig, 4);
  { auto b = last_body(); CHECK(b.size() == 2 && b[0] == 0 && !stolo_session.nonce_live, "a rejected AUTH consumes the nonce and issues none"); }
  ed_verify_result = true;
}

static void test_set_transactions() {
  boot_owned(); as_source(STOLO_SRC_USB); become_owner();
  // F8: SET_WIFI with a later bad field applies nothing.
  reset_out(); std::vector<uint8_t> bad_wifi = {SCP_W_SSID, 3, 'n', 'e', 'w', SCP_W_CHN, 1, 0};
  scp(SCP_SET_WIFI, bad_wifi, 3);
  CHECK(errored(SCP_ERR_BAD_REQUEST) && !called("eeprom_update") && EEPROM.bytes[0] != 'n', "SET_WIFI: a later invalid channel leaves the SSID untouched");
  std::vector<uint8_t> good_wifi = {SCP_W_MODE, 1, WR_WIFI_AP, SCP_W_SSID, 4, 'R', 'o', 'o', 'f', SCP_W_CHN, 1, 6};
  scp(SCP_SET_WIFI, good_wifi, 4);
  CHECK(replied(SCP_SET_WIFI | SCP_REPLY) && EEPROM.bytes[0] == 'R' && wifi_mode == WR_WIFI_AP, "SET_WIFI applies a valid request");
  { auto b = last_body(); CHECK(b[b.size()-2] == 0b1011 && b.back()==SCP_PENDING_RUNTIME, "SET_WIFI reply reports the applied tags (mode, ssid, chn)"); }
  { size_t reply_at = 0, init_at = 0; for (size_t i = 0; i < calls.size(); i++) if (calls[i] == "wifi_remote_init") init_at = i; (void)reply_at; CHECK(init_at == calls.size() - 1, "the WiFi restart is the LAST thing SET_WIFI does"); }
  // SET_BT with a later bad field disables nothing.
  bt_enabled = true; std::vector<uint8_t> bad_bt = {SCP_B_ENABLED, 1, 0, SCP_B_WINDOW, 1, 1};
  scp(SCP_SET_BT, bad_bt, 5);
  CHECK(errored(SCP_ERR_BAD_REQUEST) && bt_enabled, "SET_BT: a later malformed field leaves Bluetooth enabled");
  std::vector<uint8_t> bt_ok = {SCP_B_WINDOW, 2, 0, 60, SCP_B_DEBOND, 1, 1};
  scp(SCP_SET_BT, bt_ok, 6);
  CHECK(replied(SCP_SET_BT | SCP_REPLY) && stolo_cfg.bt_window_s == 60 && called("bt_debond_all"), "SET_BT applies window then debonds");
  { size_t debond_at = 0; for (size_t i = 0; i < calls.size(); i++) if (calls[i] == "bt_debond_all") debond_at = i; CHECK(debond_at == calls.size() - 1, "debond happens after the reply, last"); }
  // SET_RADIO: trailing bytes rejected; unknown tag reported as not applied.
  std::vector<uint8_t> dangling = {SCP_R_SF, 1, 10, 0xEE}; scp(SCP_SET_RADIO, dangling, 7);
  CHECK(errored(SCP_ERR_BAD_REQUEST) && lora_sf == 8, "SET_RADIO rejects a dangling trailing byte and applies nothing");
  std::vector<uint8_t> with_unknown = {SCP_R_SF, 1, 10, 0x40, 1, 0}; scp(SCP_SET_RADIO, with_unknown, 8);
  CHECK(replied(SCP_SET_RADIO | SCP_REPLY) && lora_sf == 10 && last_body().back() == 0b100, "SET_RADIO applies known tags and reports only those as applied");
  // Store failure leaves RAM and session untouched.
  stolo_prefs.fail_writes = true; scp(SCP_HELLO); std::vector<uint8_t> sig(64, 1); scp(SCP_AUTH, sig, 9);
  stolo_enroll_window_until = millis() + 60000; scp(SCP_HELLO); std::vector<uint8_t> enroll(96, 0x44); scp(SCP_ENROLL, enroll, 10);
  CHECK(errored(SCP_ERR_STORE_FAILED) && stolo_cfg.owner_pub[0] == 0xAB && stolo_cfg.owner_epoch == 7, "ENROLL: a failed store write changes neither owner nor epoch in RAM");
  scp(SCP_HELLO); scp(SCP_AUTH, sig, 11); scp(SCP_FORGET_OWNER, {}, 12);
  CHECK(errored(SCP_ERR_STORE_FAILED) && stolo_cfg.owner_enrolled == 1, "FORGET_OWNER: a failed store write leaves the node owned");
  stolo_prefs.fail_writes = false;
}

static void test_one_radio_policy() {
  boot_owned(); as_source(STOLO_SRC_USB); become_owner();
  CHECK(!stolo_kiss_freq_write(433000000) && called("kiss_indicate_frequency"), "legacy frequency write outside the band is refused with an echo");
  reset_out(); CHECK(stolo_kiss_freq_write(868100000 + 60000000), "legacy frequency inside the band passes");
  CHECK(!stolo_kiss_txp_write(23) && stolo_kiss_txp_write(17), "legacy power above the model limit is refused");
  model = 0x42;
  CHECK(!stolo_radio_freq_allowed(915000000) && !stolo_radio_txp_allowed(10), "unknown model: no frequency or power writes at all");
  std::vector<uint8_t> f = {SCP_R_FREQ, 4, 0x36, 0x89, 0xCA, 0xC0}; scp(SCP_SET_RADIO, f, 3);
  CHECK(errored(SCP_ERR_OUT_OF_BAND), "SCP on an unknown model is refused too (same policy)");
  model = 0xDC;
}

static void test_sensitive_reads_over_scp() {
  boot_owned(); as_source(STOLO_SRC_BLE); ble_authenticated = true; scp(SCP_HELLO);
  scp(SCP_GET_WIFI, {}, 2); CHECK(errored(SCP_ERR_UNAUTHORIZED), "guest: GET_WIFI (SSID, state) refused");
  scp(SCP_GET_BT, {}, 3);   CHECK(errored(SCP_ERR_UNAUTHORIZED), "guest: GET_BT (bond state) refused");
  scp(SCP_GET_RADIO, {}, 4); CHECK(replied(SCP_GET_RADIO | SCP_REPLY), "guest: GET_RADIO allowed");
  scp(SCP_GET_FAULTS, {}, 5); CHECK(replied(SCP_GET_FAULTS | SCP_REPLY), "guest: GET_FAULTS allowed");
  std::vector<uint8_t> v1 = {0x01, SCP_GET_RADIO, 6}; reset_out(); stolo_scp_frame_begin(); for (auto b : v1) stolo_scp_rx_byte(b); stolo_scp_frame_end();
  CHECK(errored(SCP_ERR_NOT_SUPPORTED), "a v1 client is told to upgrade");
}

static void test_recovery_state_refuses_configuration() {
  boot_owned(); as_source(STOLO_SRC_USB);
  // Corrupt both slots → recovery, not a fresh unowned node.
  for (auto key : stolo_slot_keys) if (stolo_prefs.data.count(key)) stolo_prefs.data[key][0] ^= 0xFF;
  stolo_store_state = STOLO_STORE_FRESH; stolo_store_init();
  CHECK(!stolo_store_ok && stolo_store_state == STOLO_STORE_RECOVERY, "unreadable slots put the node in RECOVERY");
  CHECK(stolo_cfg.owner_enrolled == 0 && stolo_role() == STOLO_ROLE_GUEST, "recovery is not compat: nobody may configure");
  scp(SCP_HELLO); CHECK((last_body()[1 + 9 + 2 + 32] & 0x08) == 0, "HELLO reports store_ok = 0");
  std::vector<uint8_t> enroll(96, 0x55); scp(SCP_ENROLL, enroll, 2);
  CHECK(errored(SCP_ERR_RECOVERY), "ENROLL in recovery is refused (no silent re-mint of identity)");
}

int main() {
  test_guest_kiss_default_deny();
  test_unowned_compat();
  test_owner_session_binding();
  test_auth_transcript_and_freshness();
  test_enroll_rules();
  test_set_transactions();
  test_one_radio_policy();
  test_sensitive_reads_over_scp();
  test_recovery_state_refuses_configuration();
  printf("%d passed, %d failed\n", passes, failures);
  return failures ? 1 : 0;
}
