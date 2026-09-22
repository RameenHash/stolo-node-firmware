// Software verified only: extracted production tracing with a fake USB sink.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#define portMUX_TYPE int
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) (++*(mux))
#define portEXIT_CRITICAL(mux) (--*(mux))
static uint8_t bt_state = 2;
static bool bt_allow_pairing = true, ble_authenticated = false;
static uint32_t bt_ssp_pin = 123456, now = 42;
static bool stolo_ble_boundary_pending = false;
static unsigned char bt_dh[16] = {};
static uint32_t millis() { return now; }
static int esp_ble_get_bond_device_num() { return 0; }
struct {
  int capacity = 256;
  std::vector<std::string> writes;
  int availableForWrite() { return capacity; }
  size_t write(const uint8_t* bytes, size_t length) {
    writes.emplace_back((const char*)bytes, length); return length;
  }
} Serial;
#include "ble_trace.h"
static int passes = 0, failures = 0;
#define CHECK(c,label) do { if(c) ++passes; else { ++failures; fprintf(stderr,"FAIL: %s\n",label); } } while(0)
int main() {
  bt_dh[14] = 0x11; bt_dh[15] = 0xde;
  stolo_ble_trace("onConnect.enter", 0);
  bt_state = 1; bt_allow_pairing = false; bt_ssp_pin = 0; now = 900;
  Serial.capacity = 0; stolo_ble_trace_drain();
  CHECK(Serial.writes.empty() && stolo_ble_trace_tail == 0, "USB backpressure retains the record");
  Serial.capacity = 256; stolo_ble_trace_drain();
  CHECK(Serial.writes.size() == 1, "one complete line written when space returns");
  auto line = Serial.writes[0];
  CHECK(line.find("t=42 seq=1 radio=11DE drop=0") != std::string::npos &&
        line.find("state=2 allow=1 auth=0 pin_present=1") != std::string::npos,
        "record preserves callback-time state and identifies the radio");
  CHECK(line.find("123456") == std::string::npos && line.back() == '\n', "passkey omitted from complete line");
  for (int i=0; i<66; ++i) stolo_ble_trace("burst", i);
  CHECK(stolo_ble_trace_head - stolo_ble_trace_tail == 64 && stolo_ble_trace_dropped == 2,
        "full queue counts dropped records without overwriting evidence");
  stolo_ble_trace_drain();
  CHECK(Serial.writes.size() == 9 && Serial.writes.back().find("drop=2") != std::string::npos,
        "drain is bounded to eight records and exposes loss");
  for (int i=0; i<7; ++i) stolo_ble_trace_drain();
  stolo_ble_trace("after_overflow", 0); stolo_ble_trace_drain();
  CHECK(Serial.writes.back().find("seq=68") != std::string::npos, "sequence gap reveals overflow");
  CHECK(stolo_ble_trace_mux == 0 && stolo_ble_trace_head == stolo_ble_trace_tail,
        "all critical sections released and queued records drained");
  printf("%d passed, %d failed\n", passes, failures); return failures ? 1 : 0;
}
