// Copyright (C) 2026, Stolo Systems Inc. GPL-3.0-or-later; see LICENSE.
#ifndef STOLO_CONTROL_H
#define STOLO_CONTROL_H

// Included by StoloProtocol.h after the common dispatcher. Loop-owned state:
// ATT callbacks only enqueue bytes, never run configuration/flash operations.
struct StoloControlParser {
  uint8_t bytes[STOLO_MSG_MAX];
  uint16_t length = 0;
  bool in_frame = false, command_seen = false, escape = false, dropping = false;
  uint32_t link = 0;
  uint32_t faults = 0; // rejected carrier frames/overruns, saturating since boot
} stolo_ctrl_parser;

void stolo_ctrl_abort() {
  stolo_ctrl_parser.length = 0;
  stolo_ctrl_parser.in_frame = false;
  stolo_ctrl_parser.command_seen = false;
  stolo_ctrl_parser.escape = false;
  stolo_ctrl_parser.dropping = false;
}

void stolo_ctrl_fault() {
  if (stolo_ctrl_parser.faults != UINT32_MAX) ++stolo_ctrl_parser.faults;
  stolo_ctrl_parser.dropping = true;
}

bool stolo_ctrl_link_ready() {
  #if HAS_BLE
    return ble_authenticated;
  #else
    return false;
  #endif
}

void stolo_ctrl_feed(uint8_t b, uint32_t link) {
  if (stolo_poll_session() || !stolo_ctrl_link_ready() || link != stolo_ble_link_generation()) {
    stolo_ctrl_abort(); return;
  }
  stolo_note_source(STOLO_SRC_BLE);
  auto& p = stolo_ctrl_parser;
  if (p.in_frame && p.link != link) stolo_ctrl_abort();
  if (b == FEND) {
    if (p.in_frame && p.command_seen && !p.dropping) {
      if (p.escape) stolo_ctrl_fault();
      else {
        // Keep parser and reply routing independent of USB/NUS, including
        // when the dispatcher resets authority as part of this request.
        StoloReplySink saved = stolo_reply_sink; uint32_t saved_link = stolo_reply_link;
        stolo_reply_sink = STOLO_REPLY_EVENT; stolo_reply_link = link;
        stolo_scp_dispatch(p.bytes, p.length, false);
        stolo_reply_sink = saved; stolo_reply_link = saved_link;
      }
    }
    stolo_ctrl_abort(); p.in_frame = true; p.link = link;
    return;
  }
  if (!p.in_frame || p.dropping) return;
  if (!p.command_seen) {
    p.command_seen = true;
    if (b != CMD_STOLO) stolo_ctrl_fault();
    return;
  }
  if (p.escape) {
    p.escape = false;
    if (b == TFEND) b = FEND;
    else if (b == TFESC) b = FESC;
    else { stolo_ctrl_fault(); return; }
  } else if (b == FESC) { p.escape = true; return; }
  if (p.length == sizeof(p.bytes)) { stolo_ctrl_fault(); return; }
  p.bytes[p.length++] = b;
}

// Future unsolicited SCP events use EVENT whenever this is a live BLE session,
// regardless of whether its HELLO came from CTRL or NUS. No event types yet.
void stolo_scp_event(uint8_t type, const uint8_t* body, uint16_t len) {
  if (stolo_poll_session() || !stolo_session.hello_seen) return;
  StoloReplySink saved = stolo_reply_sink; uint32_t saved_link = stolo_reply_link;
  if (stolo_session.source == STOLO_SRC_BLE) {
    if (!stolo_ctrl_link_ready()) return;
    stolo_reply_sink = STOLO_REPLY_EVENT; stolo_reply_link = stolo_ble_link_generation();
  }
  stolo_scp_send(type, 0, body, len);
  stolo_reply_sink = saved; stolo_reply_link = saved_link;
}
#endif
