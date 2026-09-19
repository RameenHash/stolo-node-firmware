// Copyright (C) 2026, Stolo Systems Inc.
// Part of Stolo Node Firmware, a fork of RNode Firmware
// (Copyright (C) 2024, Mark Qvist). GNU GPL v3 or later; see LICENSE.

// SCP, the Stolo Control Protocol (plan milestone F4), and the authority
// contract every command on this node is judged by (decision 2026-09-19).
//
// One message set, carried here inside a single KISS vendor command byte
// (CMD_STOLO 0x7A). A stock host never sees it: nothing is sent on 0x7A
// until the host has said HELLO, and a stock RNS discards an unknown
// command byte to the next FEND. The same messages also ride the separate
// BLE CTRL/EVENT service, and a future WiFi control port; the dispatcher below
// is the one they share.
//
// Wire format inside the frame:   [ver=2][type][seq][body...]
// Replies use type | 0x80. Multi-byte integers are big-endian, like KISS.
// Bodies that set things are TLV: [tag][len][value].
//
// ── The authority contract ──────────────────────────────────────────────
// A Bluetooth bond (or a USB cable) grants ACCESS. The owner key grants
// CONFIGURATION AUTHORITY. Both protocols — legacy KISS and SCP — enforce
// the same policy, default-deny, on sensitive reads as well as mutations.
//
//   OWNER    An authenticated session. Authentication is bound to ONE
//            connection (a generation number that changes on every
//            disconnect, source hand-off, LEAVE/RESET and idle timeout)
//            and to the ownership generation (owner_epoch) at the time of
//            AUTH. Any of those changing ends the authorization.
//   COMPAT   A factory-unowned node with a physically present host (USB,
//            or an encrypted+bonded BLE link). Upstream behaviour, so a
//            stock client can configure a brand-new radio. Ends at the
//            first ENROLL. WiFi never qualifies.
//   GUEST    Everyone else: data frames and an explicit read allow-list.
//            Radio-parameter writes are answered with the CURRENT value
//            (a stock RNS then fails its own validation cleanly);
//            everything else is dropped.
//
// Physical recovery (replacing the owner) is the presence-gated boot-time
// window, which must be VISIBLE on the display and wipes the previous
// owner's bonds (F8). USB attachment alone authorizes nothing on an
// owned node.

#ifndef STOLO_PROTOCOL_H
#define STOLO_PROTOCOL_H

#if defined(STOLO_BUILD) && MCU_VARIANT == MCU_ESP32

// Defined in RNode_Firmware.ino; Arduino's generated prototypes come after
// this header is pulled in, so the ones this file calls are declared here.
void stolo_transport_drain();
void stolo_rescue_announce(uint8_t flashes);
void setFrequency(); void setBandwidth(); void setSpreadingFactor();
void setCodingRate(); void setTXPower(); bool startRadio(); void stopRadio();

#define SCP_VERSION       0x02
#define STOLO_MSG_MAX     255
#define CMD_STOLO_DROP    0x7B   // internal parser sink, never on the wire

#define SCP_HELLO         0x01
#define SCP_AUTH          0x02
#define SCP_ENROLL        0x03
#define SCP_FORGET_OWNER  0x04
#define SCP_RESCUE        0x05
#define SCP_LEAVE         0x06   // end authority without closing the BLE data link
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
#define SCP_ERR_RECOVERY      0x07   // store unreadable: node refuses configuration

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

// Roles
#define STOLO_ROLE_GUEST  0
#define STOLO_ROLE_COMPAT 1
#define STOLO_ROLE_OWNER  2

// An authenticated session that says nothing for this long is over.
#define STOLO_SESSION_IDLE_MS 600000UL
#define STOLO_USB_SESSION_IDLE_MS 30000UL

// ── the band the provisioning ROM says this radio is built for ──────────
// rnodeconf's own per-model table, transcribed for the models this fork
// builds. This is the ONE radio policy: SCP SET_RADIO, the legacy KISS
// frequency/power handlers and the boot-time config load all ask it
// (review F3). A model that is not in the table gets no frequency or
// power writes at all — conservative by design.
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

bool stolo_radio_freq_allowed(uint32_t f) {
  const StoloBand* b = stolo_band_for_model(model);
  return b != NULL && f >= b->lo && f <= b->hi;
}

bool stolo_radio_txp_allowed(int p) {
  const StoloBand* b = stolo_band_for_model(model);
  return b != NULL && p >= 0 && p <= b->max_txp;
}

// ── the session ─────────────────────────────────────────────────────────
struct StoloSession {
  uint32_t generation;    // which connection this state belongs to
  uint8_t  source;        // STOLO_SRC_*
  bool     hello_seen;
  bool     authorized;
  uint32_t auth_epoch;    // owner_epoch when AUTH succeeded
  uint8_t  nonce[16];
  bool     nonce_live;    // a nonce is single-use, whatever it was used for
  uint32_t last_activity;
};

StoloSession stolo_session;
uint32_t stolo_conn_generation = 1;   // bumped on every connection boundary
uint8_t  stolo_rx[STOLO_MSG_MAX];
uint16_t stolo_rx_len = 0;
bool     stolo_rx_overflow = false;
uint32_t stolo_enroll_window_until = 0;
volatile bool stolo_usb_boundary_pending = false;
volatile bool stolo_ble_boundary_pending = false;

// Only the firmware loop dispatches requests. A scoped sink survives HELLO,
// ownership changes and LEAVE, which reset the session while replying.
enum StoloReplySink { STOLO_REPLY_SERIAL, STOLO_REPLY_EVENT };
StoloReplySink stolo_reply_sink = STOLO_REPLY_SERIAL;
uint32_t stolo_reply_link = 0;
void stolo_ctrl_abort();
uint32_t stolo_ble_link_generation();
void stolo_ble_event_send(const uint8_t* bytes, size_t len, uint32_t link);

bool stolo_enroll_window_open() {
  return stolo_enroll_window_until != 0 && (int32_t)(stolo_enroll_window_until - millis()) > 0;
}

// A connection boundary: everything the previous host earned is gone.
void stolo_parser_abort();  // .ino: parser and queued bytes share the session boundary
void stolo_session_reset(uint8_t source, bool abort_parsers = true) {
  if (abort_parsers) {
    stolo_parser_abort();
    stolo_ctrl_abort();
    stolo_rx_len = 0;
    stolo_rx_overflow = false;
  }
  stolo_conn_generation++;
  memset(&stolo_session, 0, sizeof(stolo_session));
  stolo_session.generation = stolo_conn_generation;
  stolo_session.source = source;
  stolo_session.last_activity = millis();
}

// Called by buffer_serial for every byte's origin. A change of source is
// a change of host: two successive WiFi clients share a label, so the
// WiFi calls stolo_host_disconnected() on close; BLE posts an atomic
// boundary that the loop turns into the same teardown.
void stolo_note_source(uint8_t source) {
  if (source != stolo_session.source) stolo_session_reset(source);
}

// Loop-side teardown on BLE boundary, WiFi close/timeout, LEAVE and RESET.
void stolo_host_disconnected() {
  stolo_session_reset(stolo_session.source);
}

// USB event task only posts a flag; parser/FIFO state belongs to the loop.
void stolo_usb_connection_boundary() { __atomic_store_n(&stolo_usb_boundary_pending, true, __ATOMIC_RELEASE); }

// BLE callbacks revoke authority immediately, but never mutate loop-owned
// parser/session memory. Even a disconnect/reconnect between loop ticks is seen.
void stolo_ble_connection_boundary() { __atomic_store_n(&stolo_ble_boundary_pending, true, __ATOMIC_RELEASE); }

bool stolo_poll_session() {
  bool boundary = __atomic_exchange_n(&stolo_usb_boundary_pending, false, __ATOMIC_ACQ_REL);
  bool ble_boundary = __atomic_exchange_n(&stolo_ble_boundary_pending, false, __ATOMIC_ACQ_REL);
  uint32_t timeout = stolo_session.source == STOLO_SRC_USB ? STOLO_USB_SESSION_IDLE_MS : STOLO_SESSION_IDLE_MS;
  if ((boundary && stolo_session.source == STOLO_SRC_USB)
      || (ble_boundary && stolo_session.source == STOLO_SRC_BLE)
      || (stolo_session.authorized && (uint32_t)(millis() - stolo_session.last_activity) > timeout)) {
    stolo_host_disconnected();
    return true;
  }
  return false;
}

bool stolo_host_is_present();
bool stolo_is_owner() {
  return stolo_host_is_present() && !__atomic_load_n(&stolo_usb_boundary_pending, __ATOMIC_ACQUIRE)
      && !__atomic_load_n(&stolo_ble_boundary_pending, __ATOMIC_ACQUIRE) && stolo_store_ok && stolo_session.authorized
      && stolo_session.generation == stolo_conn_generation
      && stolo_cfg.owner_enrolled
      && stolo_session.auth_epoch == stolo_cfg.owner_epoch;
}

// Physical presence for the unowned-compat role: a cable, or a BLE link
// that is encrypted and bonded (ble_authenticated is set by the security
// callback on the ESP32 BLE path).
bool stolo_host_is_present() {
  if (stolo_session.source == STOLO_SRC_USB) return true;
  #if HAS_BLE
    if (stolo_session.source == STOLO_SRC_BLE) return ble_authenticated;
  #endif
  return false;
}

uint8_t stolo_role() {
  if (stolo_is_owner()) return STOLO_ROLE_OWNER;
  if (STOLO_ENABLE_FACTORY_COMPAT && stolo_store_ok && !stolo_cfg.owner_enrolled
      && !stolo_ever_enrolled() && stolo_host_is_present()) return STOLO_ROLE_COMPAT;
  return STOLO_ROLE_GUEST;
}

// Anything that changes configuration needs the store to be trustworthy.
bool stolo_may_configure() { return stolo_store_ok && stolo_role() != STOLO_ROLE_GUEST; }

// ── writing replies ─────────────────────────────────────────────────────
void stolo_scp_send(uint8_t type, uint8_t seq, const uint8_t* body, uint16_t len) {
  if (stolo_reply_sink == STOLO_REPLY_EVENT) {
    // Worst case: every payload byte is escaped. No shared NUS TX buffer.
    if (len > STOLO_MSG_MAX - 3) return;
    uint8_t frame[2 * STOLO_MSG_MAX + 3]; size_t n = 0;
    frame[n++] = FEND; frame[n++] = CMD_STOLO;
    auto escaped = [&](uint8_t b) {
      if (b == FEND || b == FESC) { frame[n++] = FESC; frame[n++] = b == FEND ? TFEND : TFESC; }
      else frame[n++] = b;
    };
    escaped(SCP_VERSION); escaped(type); escaped(seq);
    for (uint16_t i = 0; i < len; ++i) escaped(body[i]);
    frame[n++] = FEND;
    stolo_ble_event_send(frame, n, stolo_reply_link);
    return;
  }
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

// Every SET_* body must be a whole number of complete TLVs. Returns false
// (and answers the caller) on a truncated entry or trailing bytes, so a
// malformed request applies nothing (review F8).
bool stolo_tlv_wellformed(uint8_t seq, const uint8_t* body, uint16_t len, const char* what) {
  uint16_t i = 0;
  while (i + 2 <= len) {
    uint8_t l = body[i + 1];
    if (i + 2 + l > len) { stolo_scp_error(seq, SCP_ERR_BAD_REQUEST, what); return false; }
    i += 2 + l;
  }
  if (i != len) { stolo_scp_error(seq, SCP_ERR_BAD_REQUEST, what); return false; }
  return true;
}

// ── HELLO / AUTH / ENROLL ───────────────────────────────────────────────
void stolo_reply_hello(uint8_t seq) {
  uint8_t body[128]; uint16_t n = 0;
  const char* fw = STOLO_FW_VERSION;
  body[n++] = strlen(fw); for (const char* p = fw; *p; p++) body[n++] = *p;
  body[n++] = MAJ_VERS; body[n++] = MIN_VERS;
  memcpy(body + n, stolo_cfg.node_pub, 32); n += 32;
  uint8_t flags = 0;
  if (stolo_cfg.owner_enrolled) flags |= 0x01;
  if (stolo_is_owner()) flags |= 0x02;
  if (stolo_enroll_window_open()) flags |= 0x04;
  if (stolo_store_ok) flags |= 0x08;
  if (stolo_role() == STOLO_ROLE_COMPAT) flags |= 0x10;
  body[n++] = flags;
  body[n++] = 0x00;  // attestation: advisory/unknown until F8
  put_u32(body, &n, stolo_cfg.owner_epoch);
  memcpy(body + n, stolo_session.nonce, 16); n += 16;
  body[n++] = stolo_session.source;
  stolo_scp_send(SCP_HELLO | SCP_REPLY, seq, body, n);
}

// HELLO opens a session on THIS connection: fresh nonce, no authority.
void stolo_handle_hello(uint8_t seq) {
  // HELLO on CTRL resets authority, not an in-flight NUS frame. Both
  // characteristics still share the new nonce and connection authority.
  stolo_session_reset(stolo_session.source, stolo_reply_sink != STOLO_REPLY_EVENT);
  if (!stolo_random_fill(stolo_session.nonce, sizeof(stolo_session.nonce))) {
    stolo_session_reset(stolo_session.source);
    stolo_scp_error(seq, SCP_ERR_RECOVERY, "random generator unavailable"); return;
  }
  stolo_session.nonce_live = true;
  stolo_session.authorized = false;
  stolo_session.hello_seen = true;
  stolo_reply_hello(seq);
}

bool stolo_consume_nonce() {
  stolo_session.nonce_live = false;
  return stolo_random_fill(stolo_session.nonce, sizeof(stolo_session.nonce));
}

// AUTH body: sig[64] over
//   "stolo-auth-v2" || node_pub[32] || nonce[16] || owner_epoch(u32be) || source(u8)
// Reply: [ok][role] and, on success, a fresh nonce[16] for a following ENROLL.
// node_pub stops a challenge from node A being answered for node B under
// the same owner key; source binds the answer to the carrier it was
// issued on (review F9). Same-node relay over an unauthenticated carrier
// still needs a protected channel; that is the BLE/WiFi transport's job.
void stolo_handle_auth(uint8_t seq, const uint8_t* body, uint16_t len) {
  if (len != 64) { stolo_scp_error(seq, SCP_ERR_BAD_REQUEST, "auth: need 64-byte signature"); return; }
  if (!stolo_store_ok) { stolo_scp_error(seq, SCP_ERR_RECOVERY, "store unreadable: physical recovery required"); return; }
  if (!stolo_cfg.owner_enrolled) { stolo_scp_error(seq, SCP_ERR_UNAUTHORIZED, "no owner enrolled"); return; }
  if (!stolo_session.nonce_live) { stolo_scp_error(seq, SCP_ERR_UNAUTHORIZED, "auth: say hello first"); return; }
  uint8_t msg[13 + 32 + 16 + 4 + 1]; uint16_t n = 0;
  memcpy(msg, "stolo-auth-v2", 13); n = 13;
  memcpy(msg + n, stolo_cfg.node_pub, 32); n += 32;
  memcpy(msg + n, stolo_session.nonce, 16); n += 16;
  put_u32(msg, &n, stolo_cfg.owner_epoch);
  msg[n++] = stolo_session.source;
  bool ok = Ed25519::verify(body, stolo_cfg.owner_pub, msg, n);
  // The nonce that was signed is spent either way. A SUCCESSFUL auth issues
  // a fresh one in its reply, so the owner can go on to ENROLL (hand-over)
  // without a HELLO — which would drop the authority just earned.
  ok = stolo_consume_nonce() && ok;
  stolo_session.authorized = ok;
  stolo_session.auth_epoch = stolo_cfg.owner_epoch;
  uint8_t reply[2 + 16]; uint16_t rn = 0;
  reply[rn++] = ok ? 1 : 0;
  reply[rn++] = ok ? STOLO_ROLE_OWNER : 0;
  if (ok) { stolo_session.nonce_live = true; memcpy(reply + rn, stolo_session.nonce, 16); rn += 16; }
  stolo_scp_send(SCP_AUTH | SCP_REPLY, seq, reply, rn);
}

// ENROLL body: owner_pub[32] || sig[64] over
//   "stolo-enroll-v2" || node_pub[32] || owner_pub[32] || nonce[16] || owner_epoch(u32be)
// The nonce makes a captured proof useless later (review F9). Allowed on
// a factory-unowned node from a present host, inside the visible boot-time
// enrollment window, or from the current owner's session (a hand-over).
// The new state is committed to the store BEFORE it becomes live.
void stolo_handle_enroll(uint8_t seq, const uint8_t* body, uint16_t len) {
  if (len != 96) { stolo_scp_error(seq, SCP_ERR_BAD_REQUEST, "enroll: need pub[32]+sig[64]"); return; }
  if (!stolo_store_ok) { stolo_scp_error(seq, SCP_ERR_RECOVERY, "store unreadable: physical recovery required"); return; }
  if (!stolo_session.nonce_live) { stolo_scp_error(seq, SCP_ERR_UNAUTHORIZED, "enroll: say hello first"); return; }
  bool allowed = (!stolo_cfg.owner_enrolled && stolo_host_is_present())
              || stolo_enroll_window_open()
              || stolo_is_owner();
  if (!allowed) {
    stolo_consume_nonce();
    stolo_scp_error(seq, SCP_ERR_ENROLL_CLOSED, "owned: hold the button at power-on to open enrollment");
    return;
  }
  uint8_t msg[15 + 32 + 32 + 16 + 4]; uint16_t n = 0;
  memcpy(msg, "stolo-enroll-v2", 15); n = 15;
  memcpy(msg + n, stolo_cfg.node_pub, 32); n += 32;
  memcpy(msg + n, body, 32); n += 32;
  memcpy(msg + n, stolo_session.nonce, 16); n += 16;
  put_u32(msg, &n, stolo_cfg.owner_epoch);
  bool ok = Ed25519::verify(body + 32, body, msg, n);
  stolo_consume_nonce();
  if (!ok) { stolo_scp_error(seq, SCP_ERR_UNAUTHORIZED, "enroll: bad proof of key"); return; }
  StoloConfig next = stolo_cfg;
  memcpy(next.owner_pub, body, 32);
  next.owner_enrolled = 1;
  next.owner_epoch++;
  if (!stolo_store_commit(&next)) { stolo_scp_error(seq, SCP_ERR_STORE_FAILED, "enroll: store write failed"); return; }
  // Ownership changed: every session's authority is void, the window is
  // spent, and the previous owner's bonds are no longer presence.
  stolo_enroll_window_until = 0;
  stolo_session_reset(stolo_session.source);
  uint8_t reply[5]; n = 0; reply[n++] = 1; put_u32(reply, &n, stolo_cfg.owner_epoch);
  stolo_scp_send(SCP_ENROLL | SCP_REPLY, seq, reply, n);
  stolo_transport_drain();
  #if HAS_BLE
    bt_debond_all();
  #endif
}

void stolo_handle_forget_owner(uint8_t seq) {
  if (!stolo_is_owner()) { stolo_scp_error(seq, SCP_ERR_UNAUTHORIZED, "authenticate first"); return; }
  StoloConfig next = stolo_cfg;
  memset(next.owner_pub, 0, 32);
  next.owner_enrolled = 0;
  next.owner_epoch++;
  if (!stolo_store_commit(&next)) { stolo_scp_error(seq, SCP_ERR_STORE_FAILED, "store write failed"); return; }
  stolo_session_reset(stolo_session.source);
  uint8_t reply[1] = {1};
  stolo_scp_send(SCP_FORGET_OWNER | SCP_REPLY, seq, reply, 1);
  stolo_transport_drain();
  #if HAS_BLE
    bt_debond_all();
  #endif
}

// Explicit destructive rescue: physical boot window AND unreadable store.
// Confirmation token is deliberate UI intent, not a secret or authority key.
void stolo_handle_rescue(uint8_t seq, const uint8_t* body, uint16_t len) {
  if (stolo_store_ok || stolo_store_state != STOLO_STORE_RECOVERY || !stolo_enroll_window_open()) {
    stolo_scp_error(seq, SCP_ERR_UNAUTHORIZED, "rescue needs RECOVERY and physical window"); return;
  }
  if (len != 6 || memcmp(body, "RESCUE", 6) != 0) {
    stolo_scp_error(seq, SCP_ERR_BAD_REQUEST, "rescue replaces identity: send RESCUE"); return;
  }
  stolo_rescue_announce(6);
  if (!stolo_store_rescue()) { stolo_scp_error(seq, SCP_ERR_STORE_FAILED, "rescue incomplete: retry physical rescue"); return; }
  stolo_enroll_window_until = 0;
  stolo_session_reset(stolo_session.source);
  uint8_t reply[33]; reply[0] = 1; memcpy(reply + 1, stolo_cfg.node_pub, 32);
  stolo_scp_send(SCP_RESCUE | SCP_REPLY, seq, reply, sizeof(reply));
  stolo_transport_drain();
  #if HAS_BLE
    bt_debond_all();
  #endif
}

// ── radio ───────────────────────────────────────────────────────────────
void stolo_reply_radio(uint8_t seq, uint8_t type = SCP_GET_RADIO, uint8_t applied = 0) {
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
  // A SET reply says which tags it applied (bit = tag-1), so a host
  // learns exactly what an older or newer node understood.
  if (type != SCP_GET_RADIO) body[n++] = applied;
  stolo_scp_send(type | SCP_REPLY, seq, body, n);
}

// Validate the WHOLE request against the band and the hardware ranges
// first; only then apply, through the same paths the legacy commands use,
// so every attached client sees the same echoes it always did.
void stolo_handle_set_radio(uint8_t seq, const uint8_t* body, uint16_t len) {
  if (!stolo_may_configure()) { stolo_scp_error(seq, stolo_store_ok ? SCP_ERR_UNAUTHORIZED : SCP_ERR_RECOVERY, "authenticate first"); return; }
  if (!stolo_tlv_wellformed(seq, body, len, "set_radio: malformed TLV")) return;
  uint32_t nfreq = lora_freq, nbw = lora_bw; int nsf = lora_sf, ncr = lora_cr, ntxp = lora_txp;
  bool has_freq = false, has_bw = false, has_sf = false, has_cr = false, has_txp = false, has_st = false, has_lt = false, has_state = false;
  uint16_t st = 0, lt = 0; uint8_t state = 0; uint8_t applied = 0;
  for (uint16_t i = 0; i + 2 <= len; i += 2 + body[i + 1]) {
    uint8_t tag = body[i], l = body[i + 1]; const uint8_t* v = body + i + 2;
    switch (tag) {
      case SCP_R_FREQ:  if (l != 4) goto bad; nfreq = get_u32(v); has_freq = true; break;
      case SCP_R_BW:    if (l != 4) goto bad; nbw = get_u32(v); has_bw = true; break;
      case SCP_R_SF:    if (l != 1) goto bad; nsf = v[0]; has_sf = true; break;
      case SCP_R_CR:    if (l != 1) goto bad; ncr = v[0]; has_cr = true; break;
      case SCP_R_TXP:   if (l != 1) goto bad; ntxp = v[0]; has_txp = true; break;
      case SCP_R_STAL:  if (l != 2) goto bad; st = get_u16(v); has_st = true; break;
      case SCP_R_LTAL:  if (l != 2) goto bad; lt = get_u16(v); has_lt = true; break;
      case SCP_R_STATE: if (l != 1) goto bad; state = v[0]; has_state = true; break;
      default: break;  // unknown tags are ignored and reported as not applied
    }
    if (tag >= 1 && tag <= 8) applied |= (1 << (tag - 1));
  }
  // Only what the request names is judged: a radio no host has configured
  // yet holds 0 / 0xFF in the fields it was never given, and a partial
  // write must not be refused for them. The app judges the merged stanza
  // on its side, where it knows the whole configuration.
  if ((has_sf && (nsf < 5 || nsf > 12)) || (has_cr && (ncr < 5 || ncr > 8)) || (has_txp && (ntxp < 0 || ntxp > 37))
      || (has_bw && (nbw < 7800 || nbw > 1625000)) || (has_freq && (nfreq < 137000000UL || nfreq > 3000000000UL))) {
    bad: stolo_scp_error(seq, SCP_ERR_BAD_REQUEST, "set_radio: value out of hardware range"); return;
  }
  if (has_freq && !stolo_radio_freq_allowed(nfreq)) { stolo_scp_error(seq, SCP_ERR_OUT_OF_BAND, "frequency outside this radio's band"); return; }
  if (has_txp && !stolo_radio_txp_allowed(ntxp))    { stolo_scp_error(seq, SCP_ERR_OUT_OF_BAND, "TX power above this radio's limit"); return; }
  if (has_freq) { lora_freq = nfreq; if (op_mode == MODE_HOST) setFrequency(); kiss_indicate_frequency(); }
  if (has_bw)   { lora_bw = nbw; if (op_mode == MODE_HOST) setBandwidth(); kiss_indicate_bandwidth(); }
  if (has_sf)   { lora_sf = nsf; if (op_mode == MODE_HOST) setSpreadingFactor(); kiss_indicate_spreadingfactor(); }
  if (has_cr)   { lora_cr = ncr; if (op_mode == MODE_HOST) setCodingRate(); kiss_indicate_codingrate(); }
  if (has_txp)  { lora_txp = ntxp; if (op_mode == MODE_HOST) setTXPower(); kiss_indicate_txpower(); }
  if (has_st)   { st_airtime_limit = st == 0 ? 0.0 : (float)st / (100.0 * 100.0); if (st_airtime_limit >= 1.0) st_airtime_limit = 0.0; kiss_indicate_st_alock(); }
  if (has_lt)   { lt_airtime_limit = lt == 0 ? 0.0 : (float)lt / (100.0 * 100.0); if (lt_airtime_limit >= 1.0) lt_airtime_limit = 0.0; kiss_indicate_lt_alock(); }
  if (has_state) { if (state) startRadio(); else stopRadio(); kiss_indicate_radiostate(); }
  stolo_reply_radio(seq, SCP_SET_RADIO, applied);
}

// ── WiFi ────────────────────────────────────────────────────────────────
// GET_WIFI reveals the SSID and connection state: owner and compat only.
// SET replies carry accepted configuration + pre-application runtime
// observations + accepted mask + pending flags. GET returns effective state.
#define SCP_PENDING_RUNTIME 0x01
struct StoloWifiReply {
  uint8_t mode, channel;
  char ssid[33];
  bool psk_set;
};
void stolo_reply_wifi(uint8_t seq, uint8_t type = SCP_GET_WIFI, uint8_t applied = 0,
                      const StoloWifiReply* accepted = NULL, uint8_t pending = 0) {
  uint8_t body[64]; uint16_t n = 0;
  #if HAS_WIFI
    body[n++] = 1;
    body[n++] = accepted ? accepted->mode : ((wifi_mode == WR_WIFI_STA || wifi_mode == WR_WIFI_AP) ? wifi_mode : WR_WIFI_OFF);
    body[n++] = accepted ? accepted->channel : wr_channel;
    const char* ssid = accepted ? accepted->ssid : wr_ssid;
    uint8_t sl = strnlen(ssid, 32); body[n++] = sl; memcpy(body + n, ssid, sl); n += sl;
    body[n++] = accepted ? accepted->psk_set : wr_psk[0] != 0;
    body[n++] = wr_state;
    put_u32(body, &n, (uint32_t)wr_device_ip);
  #else
    body[n++] = 0;
  #endif
  if (type != SCP_GET_WIFI) { body[n++] = applied; body[n++] = pending; }
  stolo_scp_send(type | SCP_REPLY, seq, body, n);
}

// Parse and validate EVERYTHING, then write, then restart the WiFi remote
// (the action that can drop the very connection carrying this request).
void stolo_handle_set_wifi(uint8_t seq, const uint8_t* body, uint16_t len) {
  if (!stolo_may_configure()) { stolo_scp_error(seq, stolo_store_ok ? SCP_ERR_UNAUTHORIZED : SCP_ERR_RECOVERY, "authenticate first"); return; }
  #if HAS_WIFI
    if (!stolo_tlv_wellformed(seq, body, len, "set_wifi: malformed TLV")) return;
    uint8_t mode = (wifi_mode == WR_WIFI_STA || wifi_mode == WR_WIFI_AP) ? wifi_mode : WR_WIFI_OFF; bool has_mode = false;
    const uint8_t* ssid = NULL; uint8_t ssid_len = 0; bool has_ssid = false;
    const uint8_t* psk = NULL;  uint8_t psk_len = 0;  bool has_psk = false;
    uint8_t chn = 0; bool has_chn = false; uint8_t applied = 0;
    for (uint16_t i = 0; i + 2 <= len; i += 2 + body[i + 1]) {
      uint8_t tag = body[i], l = body[i + 1]; const uint8_t* v = body + i + 2;
      switch (tag) {
        case SCP_W_MODE:
          if (l != 1 || (v[0] != WR_WIFI_OFF && v[0] != WR_WIFI_STA && v[0] != WR_WIFI_AP)) { stolo_scp_error(seq, SCP_ERR_BAD_REQUEST, "set_wifi: bad mode"); return; }
          mode = v[0]; has_mode = true; break;
        case SCP_W_SSID:
          if (l > 32 || memchr(v, 0, l) || memchr(v, 0xFF, l)) { stolo_scp_error(seq, SCP_ERR_BAD_REQUEST, "set_wifi: invalid SSID bytes/length"); return; }
          ssid = v; ssid_len = l; has_ssid = true; break;
        case SCP_W_PSK:
          if (l > 32 || memchr(v, 0, l) || memchr(v, 0xFF, l)) { stolo_scp_error(seq, SCP_ERR_BAD_REQUEST, "set_wifi: invalid passphrase bytes/length"); return; }
          psk = v; psk_len = l; has_psk = true; break;
        case SCP_W_CHN:
          if (l != 1 || v[0] < 1 || v[0] > 14) { stolo_scp_error(seq, SCP_ERR_BAD_REQUEST, "set_wifi: channel 1-14"); return; }
          chn = v[0]; has_chn = true; break;
        default: break;
      }
      if (tag >= 1 && tag <= 4) applied |= (1 << (tag - 1));
    }
    StoloWifiReply accepted = {};
    accepted.mode = mode;
    accepted.channel = has_chn ? chn : EEPROM.read(eeprom_addr(ADDR_CONF_WCHN));
    if (accepted.channel < 1 || accepted.channel > 14) accepted.channel = WR_CHANNEL_DEFAULT;
    // Unspecified fields also come from the next EEPROM image, not stale
    // runtime caches (WiFi may never have started, or a prior write failed).
    for (uint8_t k = 0; k < 32; ++k) {
      uint8_t b = EEPROM.read(config_addr(ADDR_CONF_SSID + k));
      accepted.ssid[k] = b == 0xFF ? 0 : b;
    }
    uint8_t saved_psk = EEPROM.read(config_addr(ADDR_CONF_PSK));
    accepted.psk_set = has_psk ? psk_len != 0 : (saved_psk != 0 && saved_psk != 0xFF);
    if (has_ssid) { memset(accepted.ssid, 0, sizeof(accepted.ssid)); memcpy(accepted.ssid, ssid, ssid_len); }
    // EEPROM is per-byte checked persistence, not a multi-field transaction.
    // Stop on the first failed commit; earlier bytes can already be durable.
    if (has_ssid) for (uint8_t k = 0; k < 33; k++) if (!eeprom_update(config_addr(ADDR_CONF_SSID + k), k < ssid_len ? ssid[k] : 0x00)) { stolo_scp_error(seq, SCP_ERR_STORE_FAILED, "wifi: partial persistence possible"); return; }
    if (has_psk) for (uint8_t k = 0; k < 33; k++) if (!eeprom_update(config_addr(ADDR_CONF_PSK + k), k < psk_len ? psk[k] : 0x00)) { stolo_scp_error(seq, SCP_ERR_STORE_FAILED, "wifi: partial persistence possible"); return; }
    if (has_chn && !eeprom_update(eeprom_addr(ADDR_CONF_WCHN), chn)) { stolo_scp_error(seq, SCP_ERR_STORE_FAILED, "wifi: partial persistence possible"); return; }
    if (!wr_conf_save(mode)) { stolo_scp_error(seq, SCP_ERR_STORE_FAILED, "wifi: partial persistence possible"); return; }
    (void)has_mode;
    stolo_reply_wifi(seq, SCP_SET_WIFI, applied, &accepted, SCP_PENDING_RUNTIME);
    stolo_transport_drain();
    wifi_mode = mode;
    wifi_remote_init();
  #else
    stolo_scp_error(seq, SCP_ERR_NOT_SUPPORTED, "no WiFi on this board");
  #endif
}

// ── Bluetooth ───────────────────────────────────────────────────────────
// GET_BT reveals bond and pairing state: owner and compat only.
struct StoloBtReply { bool enabled, pairing; uint8_t bonds; };
void stolo_reply_bt(uint8_t seq, uint8_t type = SCP_GET_BT, uint8_t applied = 0,
                    const StoloBtReply* accepted = NULL, uint8_t pending = 0) {
  uint8_t body[9]; uint16_t n = 0;
  #if HAS_BLE
    body[n++] = 1;
    body[n++] = bt_state; // runtime observation before any pending actions
    body[n++] = accepted ? accepted->bonds : (uint8_t)esp_ble_get_bond_device_num();
    body[n++] = accepted ? accepted->pairing : bt_allow_pairing;
    put_u16(body, &n, stolo_cfg.bt_window_s);
    body[n++] = accepted ? accepted->enabled : bt_state != BT_STATE_OFF;
  #else
    body[n++] = 0;
  #endif
  if (type != SCP_GET_BT) { body[n++] = applied; body[n++] = pending; }
  stolo_scp_send(type | SCP_REPLY, seq, body, n);
}

// Parse and validate everything; persist the window; reply; and only
// then perform the actions that can end this very link (disable, debond).
void stolo_handle_set_bt(uint8_t seq, const uint8_t* body, uint16_t len) {
  if (!stolo_may_configure()) { stolo_scp_error(seq, stolo_store_ok ? SCP_ERR_UNAUTHORIZED : SCP_ERR_RECOVERY, "authenticate first"); return; }
  #if HAS_BLE
    if (!stolo_tlv_wellformed(seq, body, len, "set_bt: malformed TLV")) return;
    bool has_pairing = false, pairing = false, debond = false, has_enabled = false, enabled = false, has_window = false;
    uint16_t window = 0; uint8_t applied = 0;
    for (uint16_t i = 0; i + 2 <= len; i += 2 + body[i + 1]) {
      uint8_t tag = body[i], l = body[i + 1]; const uint8_t* v = body + i + 2;
      switch (tag) {
        case SCP_B_PAIRING: if (l != 1) { stolo_scp_error(seq, SCP_ERR_BAD_REQUEST, "set_bt: bad TLV length"); return; } has_pairing = true; pairing = v[0] != 0; break;
        case SCP_B_DEBOND:  if (l != 1) { stolo_scp_error(seq, SCP_ERR_BAD_REQUEST, "set_bt: bad TLV length"); return; } debond = v[0] != 0; break;
        case SCP_B_WINDOW: {
          if (l != 2) { stolo_scp_error(seq, SCP_ERR_BAD_REQUEST, "set_bt: bad TLV length"); return; } window = get_u16(v);
          if (window < 10 || window > 600) { stolo_scp_error(seq, SCP_ERR_BAD_REQUEST, "set_bt: window 10-600 s"); return; }
          has_window = true; break; }
        case SCP_B_ENABLED: if (l != 1) { stolo_scp_error(seq, SCP_ERR_BAD_REQUEST, "set_bt: bad TLV length"); return; } has_enabled = true; enabled = v[0] != 0; break;
        default: break;
      }
      if (tag >= 1 && tag <= 4) applied |= (1 << (tag - 1));
    }
    if (has_pairing && pairing && ((has_enabled && !enabled) || bt_state == BT_STATE_CONNECTED)) {
      stolo_scp_error(seq, SCP_ERR_BAD_REQUEST, "pairing requires enabled, disconnected BLE"); return;
    }
    if (has_window) {
      StoloConfig next = stolo_cfg; next.bt_window_s = window;
      if (!stolo_store_commit(&next)) { stolo_scp_error(seq, SCP_ERR_STORE_FAILED, "store write failed"); return; }
    }
    if (has_enabled && !bt_conf_save(enabled)) { stolo_scp_error(seq, SCP_ERR_STORE_FAILED, "bt: partial persistence possible"); return; }
    StoloBtReply accepted = {has_enabled ? enabled : bt_state != BT_STATE_OFF,
                            has_pairing ? pairing : bt_allow_pairing,
                            debond ? (uint8_t)0 : (uint8_t)esp_ble_get_bond_device_num()};
    if (has_enabled && !enabled) accepted.pairing = false;
    uint8_t pending = (has_enabled || has_pairing || debond) ? SCP_PENDING_RUNTIME : 0;
    stolo_reply_bt(seq, SCP_SET_BT, applied, &accepted, pending);
    if (pending) stolo_transport_drain();
    if (has_enabled) { if (enabled) bt_start(); else bt_stop(); }
    if (has_pairing) { if (pairing) bt_enable_pairing(); else bt_disable_pairing(); }
    if (debond) bt_debond_all();
    return;
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

void stolo_scp_dispatch(const uint8_t* rx, uint16_t rx_len, bool overflow) {
  if (stolo_poll_session()) return;
  stolo_session.last_activity = millis();
  if (overflow || rx_len < 3) { stolo_scp_error(0, SCP_ERR_BAD_REQUEST, "malformed SCP frame"); return; }
  uint8_t ver = rx[0], type = rx[1], seq = rx[2];
  const uint8_t* body = rx + 3; uint16_t len = rx_len - 3;
  if (ver != SCP_VERSION) { stolo_scp_error(seq, SCP_ERR_NOT_SUPPORTED, "SCP version 2 required"); return; }
  // Raw WiFi KISS is not a protected control carrier. Only public reads
  // may use it. BLE administration requires the encrypted/bonded link.
  if (type != SCP_HELLO && type != SCP_GET_RADIO && type != SCP_GET_FAULTS && !stolo_host_is_present()) {
    stolo_scp_error(seq, SCP_ERR_NOT_SUPPORTED, "control requires USB or encrypted BLE"); return;
  }
  if (type != SCP_HELLO && !stolo_session.hello_seen) { stolo_scp_error(seq, SCP_ERR_BAD_REQUEST, "say hello first"); return; }
  bool privileged = stolo_role() != STOLO_ROLE_GUEST;
  switch (type) {
    case SCP_HELLO:        stolo_handle_hello(seq); break;
    case SCP_LEAVE: {
      if (len != 0) { stolo_scp_error(seq, SCP_ERR_BAD_REQUEST, "leave: empty body required"); break; }
      uint8_t ok = 1; stolo_scp_send(SCP_LEAVE | SCP_REPLY, seq, &ok, 1);
      stolo_session_reset(stolo_session.source, stolo_reply_sink != STOLO_REPLY_EVENT);
      break;
    }
    case SCP_AUTH:         stolo_handle_auth(seq, body, len); break;
    case SCP_ENROLL:       stolo_handle_enroll(seq, body, len); break;
    case SCP_FORGET_OWNER: stolo_handle_forget_owner(seq); break;
    case SCP_RESCUE:       stolo_handle_rescue(seq, body, len); break;
    case SCP_GET_RADIO:    stolo_reply_radio(seq); break;
    case SCP_SET_RADIO:    stolo_handle_set_radio(seq, body, len); break;
    case SCP_GET_WIFI:     if (privileged) stolo_reply_wifi(seq); else stolo_scp_error(seq, SCP_ERR_UNAUTHORIZED, "authenticate first"); break;
    case SCP_SET_WIFI:     stolo_handle_set_wifi(seq, body, len); break;
    case SCP_GET_BT:       if (privileged) stolo_reply_bt(seq); else stolo_scp_error(seq, SCP_ERR_UNAUTHORIZED, "authenticate first"); break;
    case SCP_SET_BT:       stolo_handle_set_bt(seq, body, len); break;
    case SCP_GET_FAULTS:   stolo_reply_faults(seq); break;
    default:               stolo_scp_error(seq, SCP_ERR_NOT_SUPPORTED, "unknown SCP type"); break;
  }
}

void stolo_scp_frame_end() {
  stolo_scp_dispatch(stolo_rx, stolo_rx_len, stolo_rx_overflow);
}

#include "StoloControl.h"

// ── the legacy KISS command policy ──────────────────────────────────────
// Default-deny. Owner and compat keep upstream's behaviour. A guest gets
// data frames and the read allow-list below; a radio-parameter write is
// answered with the CURRENT value so a stock RNS fails its own validation
// cleanly ("frequency mismatch") rather than seeing silence, which its
// validator would pass; everything else — including the config-area and
// ROM dumps that carry the WiFi credential — is dropped (review F2).
bool stolo_kiss_gate(uint8_t cmd) {
  if (stolo_poll_session()) return false;
  stolo_session.last_activity = millis();
  if (stolo_role() != STOLO_ROLE_GUEST) return true;
  switch (cmd) {
    // Data and session plumbing
    case CMD_DATA: case CMD_STOLO: case CMD_READY: case CMD_LEAVE: case CMD_DETECT:
    // Telemetry reads
    case CMD_STAT_RX: case CMD_STAT_TX: case CMD_STAT_RSSI: case CMD_STAT_SNR: case CMD_STAT_CHTM:
    case CMD_STAT_PHYPRM: case CMD_STAT_BAT: case CMD_STAT_CSMA: case CMD_STAT_TEMP:
    // Identity reads (public provenance, no secrets)
    case CMD_FW_VERSION: case CMD_PLATFORM: case CMD_MCU: case CMD_BOARD: case CMD_HASHES: case CMD_DEV_HASH:
    case CMD_RANDOM: case CMD_BLINK:
      return true;
    // Radio parameters: a guest's read (value 0 / 0xFF) and a guest's write
    // both yield the current value — which is exactly what a read returns.
    case CMD_FREQUENCY:   kiss_indicate_frequency(); return false;
    case CMD_BANDWIDTH:   kiss_indicate_bandwidth(); return false;
    case CMD_TXPOWER:     kiss_indicate_txpower(); return false;
    case CMD_SF:          kiss_indicate_spreadingfactor(); return false;
    case CMD_CR:          kiss_indicate_codingrate(); return false;
    case CMD_ST_ALOCK:    kiss_indicate_st_alock(); return false;
    case CMD_LT_ALOCK:    kiss_indicate_lt_alock(); return false;
    case CMD_RADIO_STATE: kiss_indicate_radiostate(); return false;
    default:              return false;
  }
}

// The legacy handlers for frequency and power ask these before assigning,
// so KISS, SCP and the boot load share one policy (review F3). A refusal
// echoes the unchanged value.
bool stolo_kiss_freq_write(uint32_t f) {
  if (stolo_radio_freq_allowed(f)) return true;
  kiss_indicate_frequency(); return false;
}
bool stolo_kiss_txp_write(int p) {
  if (stolo_radio_txp_allowed(p)) return true;
  kiss_indicate_txpower(); return false;
}

// ── boot and housekeeping ───────────────────────────────────────────────
// Holding the button while power is applied opens a 60 s enrollment
// window: the physical-presence gesture an already-owned node requires
// before it accepts a new owner. Read here, before the input handler
// exists, straight from the pin. F8 makes it visible on the display.
void stolo_setup() {
  stolo_store_init();
  stolo_session_reset(STOLO_SRC_USB);
  #if HAS_INPUT
    pinMode(pin_btn_usr1, INPUT_PULLUP);
    uint32_t held_since = millis();
    while (digitalRead(pin_btn_usr1) == LOW) {
      if (millis() - held_since > 3000) { stolo_enroll_window_until = millis() + 60000; stolo_rescue_announce(3); break; }
      delay(10);
    }
  #endif
}

void stolo_update() {
  if (stolo_enroll_window_until != 0 && !stolo_enroll_window_open()) stolo_enroll_window_until = 0;
  stolo_poll_session();
}

#endif
#endif
