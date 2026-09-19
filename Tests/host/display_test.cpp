// Copyright (C) 2026, Stolo Systems Inc. GPL-3.0-or-later; see LICENSE.
// Software verified: pure banner policy + real SCP handlers, fake hardware.
#include "firmware_env.h"

static void priorities() {
  // Every combination, including overlapping/stale notice inputs.
  for (bool pairing : {false, true})
    for (bool recovery : {false, true})
      for (bool window : {false, true})
        for (bool notice : {false, true}) {
          auto b = stolo_select_banner(1000, pairing, recovery, window, 61000, notice, 500);
          auto expected = pairing ? STOLO_BANNER_PAIRING
              : recovery && window ? STOLO_BANNER_RESCUE
              : notice ? STOLO_BANNER_NEW_IDENTITY
              : window ? STOLO_BANNER_ENROLL
              : recovery ? STOLO_BANNER_STORE_ERROR : STOLO_BANNER_RADIO;
          CHECK(b.kind == expected, "complete banner priority matrix");
          CHECK(b.seconds == ((expected == STOLO_BANNER_ENROLL || expected == STOLO_BANNER_RESCUE) ? 60 : 0), "only timed banners carry countdown");
        }
}

static void timing() {
  const uint32_t start = UINT32_MAX - 30000;
  const uint32_t end = start + 60000;
  for (uint32_t elapsed : {0u, 1u, 999u, 1000u, 30000u, 59999u, 60000u, 60001u}) {
    fake_millis = start + elapsed;
    stolo_enroll_window_until = end;
    const bool open = stolo_enroll_window_open();
    auto b = stolo_select_banner(fake_millis, false, false, open, end, false, 0);
    CHECK(open == (elapsed < 60000), "protocol window closes across wrap");
    CHECK(b.kind == (open ? STOLO_BANNER_ENROLL : STOLO_BANNER_RADIO), "banner expires across wrap");
    CHECK(b.seconds == (open ? (60000-elapsed+999)/1000 : 0), "countdown rounds up each second across wrap");
  }
  CHECK(stolo_select_banner(UINT32_MAX, false, false, true, 0, false, 0).seconds == 1, "pure countdown handles zero deadline");
  for (uint32_t elapsed : {0u, 4999u, 5000u, 5001u}) {
    auto b = stolo_select_banner(start+elapsed, false, false, false, 0, true, start);
    CHECK(b.kind == (elapsed < 5000 ? STOLO_BANNER_NEW_IDENTITY : STOLO_BANNER_RADIO), "identity notice exactly five seconds");
  }
  CHECK(stolo_select_banner(0, false, false, false, 0, true, 0).kind == STOLO_BANNER_NEW_IDENTITY, "identity completion at millis zero");
}

static void protocol() {
  fake_millis = 1000; boot_owned(); stolo_identity_notice = false;
  scp(SCP_HELLO);
  auto body = last_body();
  const size_t capability_offset = 1 + body[0] + 2 + 32 + 1;
  CHECK(replied(SCP_HELLO|SCP_REPLY) && body[capability_offset] == (HAS_DISPLAY ? 3 : 0), "real HELLO build capability byte");
  stolo_enroll_window_until = millis()+60000;
  CHECK(stolo_current_banner(millis(), false).kind == STOLO_BANNER_ENROLL, "opened window visible");
  ed_verify_result = false; scp(SCP_ENROLL, std::vector<uint8_t>(96, 0x12));
  CHECK(stolo_current_banner(millis(), false).kind == STOLO_BANNER_ENROLL, "failed enrollment leaves banner open");
  ed_verify_result = true; scp(SCP_HELLO); scp(SCP_ENROLL, std::vector<uint8_t>(96, 0x12));
  CHECK(replied(SCP_ENROLL|SCP_REPLY) && stolo_current_banner(millis(), false).kind == STOLO_BANNER_RADIO, "successful enrollment clears banner");
  stolo_enroll_window_until = millis()+60000; fake_millis += 60000; stolo_update();
  CHECK(stolo_enroll_window_until == 0 && stolo_current_banner(millis(), false).kind == STOLO_BANNER_RADIO, "expired window clears banner");

  for (auto key : stolo_slot_keys) stolo_prefs.data[key].push_back(0);
  stolo_store_init(); stolo_session_reset(STOLO_SRC_USB);
  CHECK(stolo_current_banner(millis(), false).kind == STOLO_BANNER_STORE_ERROR, "unreadable store has persistent error");
  stolo_enroll_window_until = millis()+60000; scp(SCP_HELLO);
  CHECK(stolo_current_banner(millis(), false).kind == STOLO_BANNER_RESCUE, "recovery window has rescue countdown");
  stolo_prefs.write_count=0; stolo_prefs.fail_call=1;
  scp(SCP_RESCUE, {'R','E','S','C','U','E'});
  CHECK(errored(SCP_ERR_STORE_FAILED) && !stolo_identity_notice && stolo_current_banner(millis(), false).kind == STOLO_BANNER_RESCUE, "failed rescue does not announce success");
  stolo_prefs.fail_call=-1;
  fake_millis = UINT32_MAX-1000; stolo_enroll_window_until = fake_millis+60000;
  scp(SCP_HELLO); scp(SCP_RESCUE, {'R','E','S','C','U','E'});
  CHECK(replied(SCP_RESCUE|SCP_REPLY) && stolo_current_banner(millis(), false).kind == STOLO_BANNER_NEW_IDENTITY, "successful rescue marks new identity");
  CHECK(stolo_current_banner(millis(), true).kind == STOLO_BANNER_PAIRING, "PIN overrides success notice");
  fake_millis += 4999;
  CHECK(stolo_current_banner(millis(), false).kind == STOLO_BANNER_NEW_IDENTITY, "success notice survives millis wrap");
  fake_millis += 1; stolo_update();
  CHECK(!stolo_identity_notice && stolo_current_banner(millis(), false).kind == STOLO_BANNER_RADIO, "success notice clears after five seconds");
}

int main() {
  priorities(); timing(); protocol();
  printf("OLED (HAS_DISPLAY=%d): %d passed, %d failed\n", HAS_DISPLAY, passes, failures);
  return failures ? 1 : 0;
}
