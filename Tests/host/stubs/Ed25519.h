#pragma once
// Host-test stub: signature verification is a switch, and the message the
// firmware asked to verify is captured so tests can pin the transcript.
#include <cstdint>
#include <cstring>
#include <vector>
inline bool ed_verify_result = true;
inline std::vector<uint8_t> ed_last_msg;
inline std::vector<uint8_t> ed_last_pub;
struct Ed25519 {
  static void derivePublicKey(uint8_t* pub, const uint8_t* priv) { memcpy(pub, priv, 32); }
  static bool verify(const uint8_t* /*sig*/, const uint8_t* pub, const uint8_t* msg, size_t len) {
    ed_last_msg.assign(msg, msg + len); ed_last_pub.assign(pub, pub + 32); return ed_verify_result;
  }
};
