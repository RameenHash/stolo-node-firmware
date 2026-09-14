// Copyright (C) 2026, Stolo Systems Inc.
// Part of Stolo Node Firmware, a fork of RNode Firmware
// (Copyright (C) 2024, Mark Qvist). GNU GPL v3 or later; see LICENSE.

// SCP, the Stolo Control Protocol (plan milestone F4), USB half.
//
// One message set, carried here inside a single KISS vendor command byte
// (CMD_STOLO 0x7A). A stock host never sees it: nothing is sent on 0x7A
// until the host has said HELLO, and a stock RNS discards an unknown
// command byte to the next FEND. The same messages will ride a separate
// BLE GATT service and the WiFi control port later; the dispatcher below
// is the one they share.
//
// Wire format inside the frame:   [ver=1][type][seq][body...]
// Replies use type | 0x80. Multi-byte integers are big-endian, like KISS.
// Bodies that set things are TLV: [tag][len][value].
//
// What a session may do depends on who it is. HELLO hands out a nonce;
// AUTH proves the owner's key over it; only an authenticated session may
// change anything (SET_*), enroll a new owner, or — through the legacy
// KISS gate at the bottom — change the radio with plain RNode commands
// when it arrived over WiFi. USB and a bonded BLE phone are physical
// presence and keep upstream's behaviour for legacy KISS.

#ifndef STOLO_PROTOCOL_H
#define STOLO_PROTOCOL_H

#if defined(STOLO_BUILD) && MCU_VARIANT == MCU_ESP32

// Defined in RNode_Firmware.ino; Arduino's generated prototypes come after
// this header is pulled in, so the ones this file calls are declared here.
void setFrequency(); void setBandwidth(); void setSpreadingFactor();
void setCodingRate(); void setTXPower(); bool startRadio(); void stopRadio();

#define SCP_VERSION       0x01
#define STOLO_MSG_MAX     255
#define CMD_STOLO_DROP    0x7B   // internal parser sink, never on the wire

#define SCP_HELLO         0x01
#define SCP_AUTH          0x02
#define SCP_ENROLL        0x03
#define SCP_FORGET_OWNER  0x04
#define SCP_GET_RADIO     0x10
#define SCP_SET_RADIO     0x11
#define SCP_GET_WIFI      0x20
#define SCP_SET_WIFI      0x21
#define SCP_GET_BT        0x30
#define SCP_SET_BT        0x31
#define SCP_GET_FAULTS    0x40
#define SCP_ERROR         0x7F
#define SCP_REPLY         0x80

#define SCP_ERR_BAD_REQUEST   0x01
#define SCP_ERR_UNAUTHORIZED  0x02
#define SCP_ERR_OUT_OF_BAND   0x03
#define SCP_ERR_NOT_SUPPORTED 0x04
#define SCP_ERR_STORE_FAILED  0x05
#define SCP_ERR_ENROLL_CLOSED 0x06

// Radio TLV tags
#define SCP_R_FREQ   0x01
#define SCP_R_BW     0x02
#define SCP_R_SF     0x03
#define SCP_R_CR     0x04
#define SCP_R_TXP    0x05
#define SCP_R_STAL   0x06
#define SCP_R_LTAL   0x07
#define SCP_R_STATE  0x08
// WiFi TLV tags
#define SCP_W_MODE   0x01
#define SCP_W_SSID   0x02
#define SCP_W_PSK    0x03
#define SCP_W_CHN    0x04
// BT TLV tags
#define SCP_B_PAIRING 0x01
#define SCP_B_DEBOND  0x02
#define SCP_B_WINDOW  0x03
#define SCP_B_ENABLED 0x04

// ── the band the provisioning ROM says this radio is built for ──────────
// rnodeconf's own per-model table, transcribed for the models this fork
// builds. A write outside it is refused BEFORE it reaches the modem, and
// the legacy echo carries the unchanged value, so a stock client fails its
// validation cleanly instead of seeing an error code it cannot parse.
struct StoloBand { uint8_t model; uint32_t lo; uint32_t hi; uint8_t max_txp; };
static const StoloBand stolo_bands[] = {
  {0xDB, 420000000, 520000000, 22}, {0xDC, 850000000, 950000000, 22},   // T-Beam Supreme
  {0xE4, 420000000, 520000000, 17}, {0xE9, 850000000, 950000000, 17},   // T-Beam
  {0xA1, 410000000, 525000000, 22}, {0xA6, 820000000, 1020000000, 22},  // T3S3 SX126x
  {0xA5, 410000000, 525000000, 17}, {0xAA, 820000000, 1020000000, 17},  // T3S3 SX127x
  {0xA2, 410000000, 525000000, 17}, {0xA7, 820000000, 1020000000, 17},  // RNode NG21
};

const StoloBand* stolo_band_for_model(uint8_t m) {
  for (unsigned i = 0; i < sizeof(stolo_bands)/sizeof(stolo_bands[0]); i++) {
    if (stolo_bands[i].model == m) return &stolo_bands[i];
  }
  return NULL;
}

// ── session and enrollment state ────────────────────────────────────────
uint8_t  stolo_rx[STOLO_MSG_MAX];
uint16_t stolo_rx_len = 0;
bool     stolo_rx_overflow = false;
bool     stolo_hello_seen = false;
bool     stolo_session_authorized = false;
uint8_t  stolo_session_nonce[16];
uint32_t stolo_enroll_window_until = 0;

bool stolo_enroll_window_open() {
  return stolo_enroll_window_until != 0 && (int32_t)(stolo_enroll_window_until - millis()) > 0;
}

// ── writing replies ─────────────────────────────────────────────────────
void stolo_scp_send(uint8_t type, uint8_t seq, const uint8_t* body, uint16_t len) {
  serial_write(FEND);
  serial_write(CMD_STOLO);
  escaped_serial_write(SCP_VERSION);
  escaped_serial_write(type);
  escaped_serial_write(seq);
  for (uint16_t i = 0; i < len; i++) escaped_serial_write(body[i]);
  serial_write(FEND);
}

void stolo_scp_error(uint8_t seq, uint8_t code, const char* msg) {
  uint8_t body[1 + 64]; uint16_t n = 0;
  body[n++] = code;
  for (const char* p = msg; *p && n < sizeof(body); p++) body[n++] = (uint8_t)*p;
  stolo_scp_send(SCP_ERROR, seq, body, n);
}

static void put_u32(uint8_t* b, uint16_t* n, uint32_t v) { b[(*n)++] = v >> 24; b[(*n)++] = v >> 16; b[(*n)++] = v >> 8; b[(*n)++] = v; }
static void put_u16(uint8_t* b, uint16_t* n, uint16_t v) { b[(*n)++] = v >> 8; b[(*n)++] = v; }
static uint32_t get_u32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint16_t get_u16(const uint8_t* p) { return ((uint16_t)p[0] << 8) | p[1]; }

// ── HELLO / AUTH / ENROLL ───────────────────────────────────────────────
void stolo_reply_hello(uint8_t seq) {
  uint8_t body[128]; uint16_t n = 0;
  const char* fw = STOLO_FW_VERSION;
  body[n++] = strlen(fw); for (const char* p = fw; *p; p++) body[n++] = *p;
  body[n++] = MAJ_VERS; body[n++] = MIN_VERS;
  memcpy(body + n, stolo_cfg.node_pub, 32); n += 32;
  uint8_t flags = 0;
  if (stolo_cfg.owner_enrolled) flags |= 0x01;
  if (stolo_session_authorized) flags |= 0x02;
  if (stolo_enroll_window_open()) flags |= 0x04;
  if (stolo_store_ok) flags |= 0x08;
  body[n++] = flags;
  body[n++] = 0x00;  // attestation: advisory/unknown until F8
  put_u32(body, &n, stolo_cfg.owner_epoch);
  memcpy(body + n, stolo_session_nonce, 16); n += 16;
  body[n++] = stolo_input_source;
  stolo_scp_send(SCP_HELLO | SCP_REPLY, seq, body, n);
}

void stolo_handle_hello(uint8_t seq) {
  esp_fill_random(stolo_session_nonce, sizeof(stolo_session_nonce));
  stolo_session_authorized = false;
  stolo_hello_seen = true;
  stolo_reply_hello(seq);
}

// AUTH body: sig[64] over "stolo-auth-v1" || nonce[16] || epoch(u32be)
void stolo_handle_auth(uint8_t seq, const uint8_t* body, uint16_t len) {
  if (len != 64) { stolo_scp_error(seq, SCP_ERR_BAD_REQUEST, "auth: need 64-byte signature"); return; }
  if (!stolo_cfg.owner_enrolled) { stolo_scp_error(seq, SCP_ERR_UNAUTHORIZED, "no owner enrolled"); return; }
  uint8_t msg[13 + 16 + 4]; uint16_t n = 0;
  memcpy(msg, "stolo-auth-v1", 13); n = 13;
  memcpy(msg + n, stolo_session_nonce, 16); n += 16;
  put_u32(msg, &n, stolo_cfg.owner_epoch);
  bool ok = Ed25519::verify(body, stolo_cfg.owner_pub, msg, n);
  stolo_session_authorized = ok;
  // A nonce is single-use, whatever the answer was.
  esp_fill_random(stolo_session_nonce, sizeof(stolo_session_nonce));
  uint8_t reply[2] = { (uint8_t)(ok ? 1 : 0), (uint8_t)(ok ? 1 : 0) };
  stolo_scp_send(SCP_AUTH | SCP_REPLY, seq, reply, 2);
}

// ENROLL body: owner_pub[32] || sig[64] over "stolo-enroll-v1" || node_pub || owner_pub
// Allowed when the node has no owner yet (factory-unowned), when the
// boot-time enroll window is open (button held at power-on), or from a
// session the current owner has authenticated (a hand-over).
void stolo_handle_enroll(uint8_t seq, const uint8_t* body, uint16_t len) {
  if (len != 96) { stolo_scp_error(seq, SCP_ERR_BAD_REQUEST, "enroll: need pub[32]+sig[64]"); return; }
  if (stolo_cfg.owner_enrolled && !stolo_enroll_window_open() && !stolo_session_authorized) {
    stolo_scp_error(seq, SCP_ERR_ENROLL_CLOSED, "owned: hold the button at power-on to open enrollment");
    return;
  }
  uint8_t msg[15 + 32 + 32];
  memcpy(msg, "stolo-enroll-v1", 15);
  memcpy(msg + 15, stolo_cfg.node_pub, 32);
  memcpy(msg + 47, body, 32);
  if (!Ed25519::verify(body + 32, body, msg, sizeof(msg))) { stolo_scp_error(seq, SCP_ERR_UNAUTHORIZED, "enroll: bad proof of key"); return; }
  memcpy(stolo_cfg.owner_pub, body, 32);
  stolo_cfg.owner_enrolled = 1;
  stolo_cfg.owner_epoch++;
  if (!stolo_store_save()) { stolo_scp_error(seq, SCP_ERR_STORE_FAILED, "enroll: store write failed"); return; }
  stolo_enroll_window_until = 0;
  stolo_session_authorized = false;   // the new owner authenticates like anyone else
  uint8_t reply[5]; uint16_t n = 0; reply[n++] = 1; put_u32(reply, &n, stolo_cfg.owner_epoch);
  stolo_scp_send(SCP_ENROLL | SCP_REPLY, seq, reply, n);
}

void stolo_handle_forget_owner(uint8_t seq) {
  if (!stolo_session_authorized) { stolo_scp_error(seq, SCP_ERR_UNAUTHORIZED, "authenticate first"); return; }
  memset(stolo_cfg.owner_pub, 0, 32);
  stolo_cfg.owner_enrolled = 0;
  stolo_cfg.owner_epoch++;
  stolo_session_authorized = false;
  if (!stolo_store_save()) { stolo_scp_error(seq, SCP_ERR_STORE_FAILED, "store write failed"); return; }
  uint8_t reply[1] = {1};
  stolo_scp_send(SCP_FORGET_OWNER | SCP_REPLY, seq, reply, 1);
}

// ── radio ───────────────────────────────────────────────────────────────
void stolo_reply_radio(uint8_t seq, uint8_t type = SCP_GET_RADIO) {
  uint8_t body[40]; uint16_t n = 0;
  put_u32(body, &n, lora_freq);
  put_u32(body, &n, lora_bw);
  body[n++] = (uint8_t)lora_sf;
  body[n++] = (uint8_t)lora_cr;
  body[n++] = (uint8_t)lora_txp;
  // Hundredths of a percent, rounded: 1 % is 100, not the 99 a truncated
  // 0.01 * 10000 gives.
  put_u16(body, &n, (uint16_t)(st_airtime_limit * 10000.0 + 0.5));
  put_u16(body, &n, (uint16_t)(lt_airtime_limit * 10000.0 + 0.5));
  body[n++] = radio_online ? 1 : 0;
  body[n++] = model;
  const StoloBand* b = stolo_band_for_model(model);
  put_u32(body, &n, b ? b->lo : 0);
  put_u32(body, &n, b ? b->hi : 0);
  body[n++] = b ? b->max_txp : 0;
  stolo_scp_send(type | SCP_REPLY, seq, body, n);
}

// Validate the WHOLE request against the band and the hardware ranges
// first; only then apply, through the same paths the legacy commands use,
// so every attached client sees the same echoes it always did.
void stolo_handle_set_radio(uint8_t seq, const uint8_t* body, uint16_t len) {
  if (!stolo_session_authorized) { stolo_scp_error(seq, SCP_ERR_UNAUTHORIZED, "authenticate first"); return; }
  const StoloBand* band = stolo_band_for_model(model);
  uint32_t nfreq = lora_freq, nbw = lora_bw; int nsf = lora_sf, ncr = lora_cr, ntxp = lora_txp;
  bool has_freq = false, has_bw = false, has_sf = false, has_cr = false, has_txp = false, has_st = false, has_lt = false, has_state = false;
  uint16_t st = 0, lt = 0; uint8_t state = 0;
  uint16_t i = 0;
  while (i + 2 <= len) {
    uint8_t tag = body[i], l = body[i + 1]; const uint8_t* v = body + i + 2;
    if (i + 2 + l > len) { stolo_scp_error(seq, SCP_ERR_BAD_REQUEST, "set_radio: truncated TLV"); return; }
    switch (tag) {
      case SCP_R_FREQ:  if (l != 4) goto bad; nfreq = get_u32(v); has_freq = true; break;
      case SCP_R_BW:    if (l != 4) goto bad; nbw = get_u32(v); has_bw = true; break;
      case SCP_R_SF:    if (l != 1) goto bad; nsf = v[0]; has_sf = true; break;
      case SCP_R_CR:    if (l != 1) goto bad; ncr = v[0]; has_cr = true; break;
      case SCP_R_TXP:   if (l != 1) goto bad; ntxp = v[0]; has_txp = true; break;
      case SCP_R_STAL:  if (l != 2) goto bad; st = get_u16(v); has_st = true; break;
      case SCP_R_LTAL:  if (l != 2) goto bad; lt = get_u16(v); has_lt = true; break;
      case SCP_R_STATE: if (l != 1) goto bad; state = v[0]; has_state = true; break;
      default: break;  // unknown tags are ignored, so an older node tolerates a newer host
    }
    i += 2 + l;
  }
  // Only what the request names is judged: a radio no host has configured
  // yet holds 0 / 0xFF in the fields it was never given, and a partial
  // write must not be refused for them. The app judges the merged stanza
  // on its side, where it knows the whole configuration.
  if ((has_sf && (nsf < 5 || nsf > 12)) || (has_cr && (ncr < 5 || ncr > 8)) || (has_txp && (ntxp < 0 || ntxp > 37))
      || (has_bw && (nbw < 7800 || nbw > 1625000)) || (has_freq && (nfreq < 137000000UL || nfreq > 3000000000UL))) {
    bad: stolo_scp_error(seq, SCP_ERR_BAD_REQUEST, "set_radio: value out of hardware range"); return;
  }
  if (band) {
    if (has_freq && (nfreq < band->lo || nfreq > band->hi)) { stolo_scp_error(seq, SCP_ERR_OUT_OF_BAND, "frequency outside this radio's band"); return; }
    if (has_txp && ntxp > band->max_txp)                    { stolo_scp_error(seq, SCP_ERR_OUT_OF_BAND, "TX power above this radio's limit"); return; }
  }
  if (has_freq) { lora_freq = nfreq; if (op_mode == MODE_HOST) setFrequency(); kiss_indicate_frequency(); }
  if (has_bw)   { lora_bw = nbw; if (op_mode == MODE_HOST) setBandwidth(); kiss_indicate_bandwidth(); }
  if (has_sf)   { lora_sf = nsf; if (op_mode == MODE_HOST) setSpreadingFactor(); kiss_indicate_spreadingfactor(); }
  if (has_cr)   { lora_cr = ncr; if (op_mode == MODE_HOST) setCodingRate(); kiss_indicate_codingrate(); }
  if (has_txp)  { lora_txp = ntxp; if (op_mode == MODE_HOST) setTXPower(); kiss_indicate_txpower(); }
  if (has_st)   { st_airtime_limit = st == 0 ? 0.0 : (float)st / (100.0 * 100.0); if (st_airtime_limit >= 1.0) st_airtime_limit = 0.0; kiss_indicate_st_alock(); }
  if (has_lt)   { lt_airtime_limit = lt == 0 ? 0.0 : (float)lt / (100.0 * 100.0); if (lt_airtime_limit >= 1.0) lt_airtime_limit = 0.0; kiss_indicate_lt_alock(); }
  if (has_state) { if (state) startRadio(); else stopRadio(); kiss_indicate_radiostate(); }
  // A reply carries the request's type: a SET is answered as a SET.
  stolo_reply_radio(seq, SCP_SET_RADIO);
}

// ── WiFi ────────────────────────────────────────────────────────────────
void stolo_reply_wifi(uint8_t seq, uint8_t type = SCP_GET_WIFI) {
  uint8_t body[64]; uint16_t n = 0;
  #if HAS_WIFI
    body[n++] = 1;  // supported
    // An unset EEPROM byte reads 0xFF; that is "off", not a mode.
    body[n++] = (wifi_mode == WR_WIFI_STA || wifi_mode == WR_WIFI_AP) ? wifi_mode : WR_WIFI_OFF;
    body[n++] = wr_channel;
    uint8_t sl = strnlen(wr_ssid, 32); body[n++] = sl; memcpy(body + n, wr_ssid, sl); n += sl;
    body[n++] = wr_psk[0] != 0 ? 1 : 0;
    body[n++] = wr_state;
    put_u32(body, &n, (uint32_t)wr_device_ip);
  #else
    body[n++] = 0;  // not supported
  #endif
  stolo_scp_send(type | SCP_REPLY, seq, body, n);
}

void stolo_handle_set_wifi(uint8_t seq, const uint8_t* body, uint16_t len) {
  if (!stolo_session_authorized) { stolo_scp_error(seq, SCP_ERR_UNAUTHORIZED, "authenticate first"); return; }
  #if HAS_WIFI
    uint16_t i = 0; bool has_mode = false; uint8_t mode = wifi_mode;
    while (i + 2 <= len) {
      uint8_t tag = body[i], l = body[i + 1]; const uint8_t* v = body + i + 2;
      if (i + 2 + l > len) { stolo_scp_error(seq, SCP_ERR_BAD_REQUEST, "set_wifi: truncated TLV"); return; }
      switch (tag) {
        case SCP_W_MODE:
          if (l != 1 || (v[0] != WR_WIFI_OFF && v[0] != WR_WIFI_STA && v[0] != WR_WIFI_AP)) { stolo_scp_error(seq, SCP_ERR_BAD_REQUEST, "set_wifi: bad mode"); return; }
          mode = v[0]; has_mode = true; break;
        case SCP_W_SSID:
          if (l > 32) { stolo_scp_error(seq, SCP_ERR_BAD_REQUEST, "set_wifi: SSID over 32 bytes"); return; }
          for (uint8_t k = 0; k < 33; k++) eeprom_update(config_addr(ADDR_CONF_SSID + k), k < l ? v[k] : 0x00);
          break;
        case SCP_W_PSK:
          if (l > 32) { stolo_scp_error(seq, SCP_ERR_BAD_REQUEST, "set_wifi: passphrase over 32 bytes"); return; }
          for (uint8_t k = 0; k < 33; k++) eeprom_update(config_addr(ADDR_CONF_PSK + k), k < l ? v[k] : 0x00);
          break;
        case SCP_W_CHN:
          if (l != 1 || v[0] < 1 || v[0] > 14) { stolo_scp_error(seq, SCP_ERR_BAD_REQUEST, "set_wifi: channel 1-14"); return; }
          eeprom_update(eeprom_addr(ADDR_CONF_WCHN), v[0]); break;
        default: break;
      }
      i += 2 + l;
    }
    // Same sequence as CMD_WIFI_MODE: persist, then (re)start, which
    // re-reads SSID, PSK and channel from the EEPROM config block.
    wr_conf_save(mode);
    wifi_mode = mode;
    wifi_remote_init();
    (void)has_mode;
    stolo_reply_wifi(seq, SCP_SET_WIFI);
  #else
    stolo_scp_error(seq, SCP_ERR_NOT_SUPPORTED, "no WiFi on this board");
  #endif
}

// ── Bluetooth ───────────────────────────────────────────────────────────
void stolo_reply_bt(uint8_t seq, uint8_t type = SCP_GET_BT) {
  uint8_t body[8]; uint16_t n = 0;
  #if HAS_BLE
    body[n++] = 1;  // supported
    body[n++] = bt_state;
    body[n++] = (uint8_t)esp_ble_get_bond_device_num();
    body[n++] = bt_allow_pairing ? 1 : 0;
    put_u16(body, &n, stolo_cfg.bt_window_s);
    body[n++] = bt_enabled ? 1 : 0;
  #else
    body[n++] = 0;  // not supported
  #endif
  stolo_scp_send(type | SCP_REPLY, seq, body, n);
}

void stolo_handle_set_bt(uint8_t seq, const uint8_t* body, uint16_t len) {
  if (!stolo_session_authorized) { stolo_scp_error(seq, SCP_ERR_UNAUTHORIZED, "authenticate first"); return; }
  #if HAS_BLE
    uint16_t i = 0; bool store_dirty = false;
    while (i + 2 <= len) {
      uint8_t tag = body[i], l = body[i + 1]; const uint8_t* v = body + i + 2;
      if (i + 2 + l > len) { stolo_scp_error(seq, SCP_ERR_BAD_REQUEST, "set_bt: truncated TLV"); return; }
      switch (tag) {
        case SCP_B_PAIRING: if (l != 1) goto bad; if (v[0]) { if (bt_state != BT_STATE_CONNECTED) bt_enable_pairing(); } else { bt_disable_pairing(); } break;
        case SCP_B_DEBOND:  if (l != 1) goto bad; if (v[0]) bt_debond_all(); break;
        case SCP_B_WINDOW: {
          if (l != 2) goto bad; uint16_t s = get_u16(v);
          if (s < 10 || s > 600) { stolo_scp_error(seq, SCP_ERR_BAD_REQUEST, "set_bt: window 10-600 s"); return; }
          stolo_cfg.bt_window_s = s; store_dirty = true; break; }
        case SCP_B_ENABLED: if (l != 1) goto bad; if (v[0]) { bt_start(); bt_conf_save(true); } else { bt_stop(); bt_conf_save(false); } break;
        default: break;
      }
      i += 2 + l;
    }
    if (store_dirty && !stolo_store_save()) { stolo_scp_error(seq, SCP_ERR_STORE_FAILED, "store write failed"); return; }
    stolo_reply_bt(seq, SCP_SET_BT);
    return;
    bad: stolo_scp_error(seq, SCP_ERR_BAD_REQUEST, "set_bt: bad TLV length");
  #else
    stolo_scp_error(seq, SCP_ERR_NOT_SUPPORTED, "no BLE on this board");
  #endif
}

void stolo_reply_faults(uint8_t seq) {
  uint8_t body[9]; uint16_t n = 0;
  body[n++] = stolo_fault_count;
  for (int i = 0; i < stolo_fault_count && i < 8; i++) body[n++] = stolo_faults[i];
  stolo_scp_send(SCP_GET_FAULTS | SCP_REPLY, seq, body, n);
}

// ── the parser's three hooks ────────────────────────────────────────────
void stolo_scp_frame_begin() { stolo_rx_len = 0; stolo_rx_overflow = false; }

void stolo_scp_rx_byte(uint8_t b) {
  if (stolo_rx_len < STOLO_MSG_MAX) stolo_rx[stolo_rx_len++] = b; else stolo_rx_overflow = true;
}

void stolo_scp_frame_end() {
  if (stolo_rx_overflow || stolo_rx_len < 3) { stolo_scp_error(0, SCP_ERR_BAD_REQUEST, "malformed SCP frame"); return; }
  uint8_t ver = stolo_rx[0], type = stolo_rx[1], seq = stolo_rx[2];
  const uint8_t* body = stolo_rx + 3; uint16_t len = stolo_rx_len - 3;
  if (ver != SCP_VERSION) { stolo_scp_error(seq, SCP_ERR_NOT_SUPPORTED, "SCP version"); return; }
  if (type != SCP_HELLO && !stolo_hello_seen) { stolo_scp_error(seq, SCP_ERR_BAD_REQUEST, "say hello first"); return; }
  switch (type) {
    case SCP_HELLO:        stolo_handle_hello(seq); break;
    case SCP_AUTH:         stolo_handle_auth(seq, body, len); break;
    case SCP_ENROLL:       stolo_handle_enroll(seq, body, len); break;
    case SCP_FORGET_OWNER: stolo_handle_forget_owner(seq); break;
    case SCP_GET_RADIO:    stolo_reply_radio(seq); break;
    case SCP_SET_RADIO:    stolo_handle_set_radio(seq, body, len); break;
    case SCP_GET_WIFI:     stolo_reply_wifi(seq); break;
    case SCP_SET_WIFI:     stolo_handle_set_wifi(seq, body, len); break;
    case SCP_GET_BT:       stolo_reply_bt(seq); break;
    case SCP_SET_BT:       stolo_handle_set_bt(seq, body, len); break;
    case SCP_GET_FAULTS:   stolo_reply_faults(seq); break;
    default:               stolo_scp_error(seq, SCP_ERR_NOT_SUPPORTED, "unknown SCP type"); break;
  }
}

// ── the legacy KISS mutation gate ───────────────────────────────────────
// A guest (a WiFi session that has not authenticated) may read the radio
// but not change it. For the radio parameters the refusal is the CURRENT
// value echoed back, so a stock RNS on the guest side fails its own
// validation cleanly ("frequency mismatch") rather than seeing silence,
// which its validator would pass. Everything else state-changing is
// dropped. USB and a bonded BLE phone are physical presence and keep
// upstream's behaviour.
bool stolo_kiss_gate(uint8_t cmd) {
  if (stolo_input_source != STOLO_SRC_WIFI || stolo_session_authorized) return true;
  switch (cmd) {
    case CMD_FREQUENCY:  kiss_indicate_frequency(); return false;
    case CMD_BANDWIDTH:  kiss_indicate_bandwidth(); return false;
    case CMD_TXPOWER:    kiss_indicate_txpower(); return false;
    case CMD_SF:         kiss_indicate_spreadingfactor(); return false;
    case CMD_CR:         kiss_indicate_codingrate(); return false;
    case CMD_ST_ALOCK:   kiss_indicate_st_alock(); return false;
    case CMD_LT_ALOCK:   kiss_indicate_lt_alock(); return false;
    case CMD_RADIO_STATE: kiss_indicate_radiostate(); return false;
    case CMD_LEAVE: case CMD_RESET: case CMD_BT_CTRL: case CMD_BT_UNPAIR:
    case CMD_WIFI_MODE: case CMD_WIFI_SSID: case CMD_WIFI_PSK: case CMD_WIFI_CHN: case CMD_WIFI_IP: case CMD_WIFI_NM:
    case CMD_CONF_SAVE: case CMD_CONF_DELETE: case CMD_ROM_WRITE: case CMD_UNLOCK_ROM: case CMD_FW_UPD: case CMD_FW_HASH:
    case CMD_DISP_INT: case CMD_DISP_ADDR: case CMD_DISP_BLNK: case CMD_DISP_ROT: case CMD_DISP_RCND: case CMD_NP_INT: case CMD_DIS_IA:
      return false;
    default: return true;
  }
}

// ── boot and housekeeping ───────────────────────────────────────────────
// Holding the button while power is applied opens a 60 s enrollment
// window: the physical-presence gesture an already-owned node requires
// before it accepts a new owner. Read here, before the input handler
// exists, straight from the pin.
void stolo_setup() {
  stolo_store_init();
  #if HAS_INPUT
    pinMode(pin_btn_usr1, INPUT_PULLUP);
    uint32_t held_since = millis();
    while (digitalRead(pin_btn_usr1) == LOW) {
      if (millis() - held_since > 3000) { stolo_enroll_window_until = millis() + 60000; break; }
      delay(10);
    }
  #endif
}

void stolo_update() {
  if (stolo_enroll_window_until != 0 && !stolo_enroll_window_open()) stolo_enroll_window_until = 0;
}

#endif
#endif
