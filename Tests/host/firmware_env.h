// Copyright (C) 2026, Stolo Systems Inc. GPL-3.0-or-later; see LICENSE.
// The firmware globals StoloStore.h / StoloProtocol.h reach into, stubbed
// for a host build. Hardware, persistence and Ed25519 are fakes: these
// tests establish the dispatcher's STATE TRANSITIONS and POLICY — "software
// verified" — not ESP32 timing, signature correctness or flash behaviour,
// which stay "device qualified" on the bench.
#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#define STOLO_BUILD 1
#define STOLO_HOST_TEST 1
#define MCU_VARIANT 1
#define MCU_ESP32 1
#define HAS_WIFI 1
#define HAS_BLE 1
#define HAS_INPUT 0
#define HAS_EEPROM 1
#define MAJ_VERS 1
#define MIN_VERS 86
#define MODE_HOST 1
#define WR_CHANNEL_DEFAULT 1
#define WR_WIFI_OFF 0
#define WR_WIFI_STA 1
#define WR_WIFI_AP 2
#define BT_STATE_OFF 0
#define BT_STATE_ON 1
#define BT_STATE_CONNECTED 3
#define ADDR_CONF_SSID 0x00
#define ADDR_CONF_PSK  0x21
#define ADDR_CONF_WCHN 0x96
#define CONFIG_SIZE 256
#include "Framing.h"
#include "Stolo.h"
inline uint32_t fake_millis = 1000;
inline uint32_t millis() { return fake_millis; }
inline std::vector<std::string> calls;
inline void delay(uint32_t n) { calls.push_back("delay:"+std::to_string(n)); }
inline uint32_t lora_freq = 915000000, lora_bw = 125000;
inline int lora_sf = 8, lora_cr = 5, lora_txp = 17, op_mode = 1;
inline float st_airtime_limit = 0, lt_airtime_limit = 0;
inline bool radio_online = true;
inline uint8_t model = 0xDC;
inline uint8_t wifi_mode = 0, wr_channel = 1, wr_state = 0;
inline uint32_t wr_device_ip = 0;
inline char wr_ssid[33] = {}, wr_psk[33] = {};
inline uint8_t bt_state = BT_STATE_ON; inline bool bt_allow_pairing = false, bt_enabled = true, ble_authenticated = false;
inline int fake_bonds = 1;
inline std::vector<uint8_t> out;
inline void serial_write(uint8_t b) { out.push_back(b); calls.push_back("serial_write"); }
inline void escaped_serial_write(uint8_t b) { out.push_back(b); }
#define NOOP(name) inline void name() { calls.push_back(#name); }
inline void stolo_rescue_announce(uint8_t) { calls.push_back("rescue_announce"); }
#ifndef STOLO_REAL_TRANSPORT_TEST
NOOP(stolo_transport_drain)
#endif
NOOP(setFrequency) NOOP(setBandwidth) NOOP(setSpreadingFactor) NOOP(setCodingRate) NOOP(setTXPower)
inline bool startRadio() { calls.push_back("startRadio"); return radio_online = true; }
inline void stopRadio() { calls.push_back("stopRadio"); radio_online = false; }
NOOP(kiss_indicate_frequency) NOOP(kiss_indicate_bandwidth) NOOP(kiss_indicate_spreadingfactor) NOOP(kiss_indicate_codingrate)
NOOP(kiss_indicate_txpower) NOOP(kiss_indicate_st_alock) NOOP(kiss_indicate_lt_alock) NOOP(kiss_indicate_radiostate)
struct EE { uint8_t bytes[1024] = {}; uint8_t read(int p) { return bytes[p]; } } inline EEPROM;
inline int config_addr(int p) { return p; }
inline int eeprom_addr(int p) { return 512 + p; }
inline int ee_write_count=0, ee_fail_call=-1;
inline bool eeprom_update(int p, uint8_t b) { calls.push_back("eeprom_update"); if (++ee_write_count == ee_fail_call) return false; EEPROM.bytes[p] = b; return true; }
inline bool wr_conf_save(uint8_t m) { calls.push_back("wr_conf_save"); return eeprom_update(700,m); }
inline void wifi_remote_init() {
  calls.push_back("wifi_remote_init"); memcpy(wr_ssid,EEPROM.bytes,33); memcpy(wr_psk,EEPROM.bytes+33,33);
  wr_channel=EEPROM.bytes[eeprom_addr(ADDR_CONF_WCHN)]; wr_state=wifi_mode?1:0;
}
inline int esp_ble_get_bond_device_num() { return fake_bonds; }
inline void bt_debond_all() { calls.push_back("bt_debond_all"); fake_bonds = 0; }
inline void bt_enable_pairing() { bt_allow_pairing = true; calls.push_back("bt_enable_pairing"); }
inline void bt_disable_pairing() { bt_allow_pairing = false; }
inline void bt_start() { bt_state = BT_STATE_ON; calls.push_back("bt_start"); }
inline void bt_stop() { bt_state = BT_STATE_OFF; calls.push_back("bt_stop"); }
inline bool bt_conf_save(bool enabled) { if (!eeprom_update(701,enabled)) return false; bt_enabled=enabled; return true; }
#ifndef STOLO_DISPATCH_TEST
inline void stolo_parser_abort() {}
#endif
#include "StoloStore.h"
#include "StoloProtocol.h"

// ── tiny test kit ─────────────────────────────────────────────────────
inline int failures = 0, passes = 0;
#define CHECK(cond, label) do { if (cond) { passes++; } else { failures++; fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, label); } } while (0)
inline void reset_out() { out.clear(); calls.clear(); }
inline bool called(const char* name) { for (auto& c : calls) if (c == name) return true; return false; }
// Replies are [FEND][CMD_STOLO][ver][type][seq][body...][FEND] (no escaping in this stub).
inline int last_reply_type() { for (int i = (int)out.size() - 1; i >= 0; i--) if (out[i] == FEND && i >= 4) { /* find frame start */ } return out.size() >= 5 ? out[3] : -1; }
inline std::vector<uint8_t> last_body() { std::vector<uint8_t> b; if (out.size() < 6) return b; b.assign(out.begin() + 5, out.end() - 1); return b; }
inline bool replied(uint8_t type) { return out.size() >= 5 && out[3] == type; }
inline bool errored(uint8_t code) { return replied(SCP_ERROR) && out.size() >= 6 && out[5] == code; }
// Dispatcher suites drive the actual KISS byte stream; header-only suites
// call the same SCP hooks directly. Neither replaces production handlers.
#ifdef STOLO_DISPATCH_TEST
void serial_callback(uint8_t);
#endif
inline void scp(uint8_t type, const std::vector<uint8_t>& body = {}, uint8_t seq = 1) {
  reset_out();
  #ifdef STOLO_DISPATCH_TEST
    serial_callback(FEND); serial_callback(CMD_STOLO);
    std::vector<uint8_t> payload={SCP_VERSION,type,seq}; payload.insert(payload.end(),body.begin(),body.end());
    for (auto b:payload) {
      if (b==FEND) { serial_callback(FESC); serial_callback(TFEND); }
      else if (b==FESC) { serial_callback(FESC); serial_callback(TFESC); }
      else serial_callback(b);
    }
    serial_callback(FEND);
  #else
    stolo_scp_frame_begin();
    stolo_scp_rx_byte(SCP_VERSION); stolo_scp_rx_byte(type); stolo_scp_rx_byte(seq);
    for (auto b : body) stolo_scp_rx_byte(b);
    stolo_scp_frame_end();
  #endif
}
inline void as_source(uint8_t src) { stolo_note_source(src); }
inline void boot_owned(uint32_t epoch = 7) {
  stolo_prefs = Preferences(); stolo_store_state = STOLO_STORE_FRESH; stolo_store_ok = false;
  stolo_store_init();
  StoloConfig next = stolo_cfg; memset(next.owner_pub, 0xAB, 32); next.owner_enrolled = 1; next.owner_epoch = epoch;
  if (!stolo_store_commit(&next)) { fprintf(stderr, "setup commit failed\n"); exit(2); }
  stolo_session_reset(STOLO_SRC_USB); ble_authenticated = false; ed_verify_result = true; fake_bonds = 1; stolo_enroll_window_until = 0;
  lora_freq = 915000000; lora_txp = 17; lora_sf = 8; wifi_mode = 0; bt_enabled = true; radio_online = true;
}
inline void boot_unowned() {
  stolo_prefs = Preferences(); stolo_store_state = STOLO_STORE_FRESH; stolo_store_ok = false;
  stolo_store_init(); stolo_session_reset(STOLO_SRC_USB); ble_authenticated = false; ed_verify_result = true; fake_bonds = 0; stolo_enroll_window_until = 0;
}
// HELLO then AUTH on the current source; the stub accepts the signature.
inline void become_owner() { scp(SCP_HELLO); std::vector<uint8_t> sig(64, 1); scp(SCP_AUTH, sig, 2); }
