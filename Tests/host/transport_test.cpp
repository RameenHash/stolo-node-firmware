// Real transport drain and USB event hooks, fake hardware: ordering only.
#define STOLO_REAL_TRANSPORT_TEST 1
#include "firmware_env.h"
#include <algorithm>
struct USB { void flush() { calls.push_back("usb_flush"); } } Serial;
struct WiFi { void flush() { calls.push_back("wifi_flush"); } } connection;
void bt_flush() { calls.push_back("bt_flush"); }
#include "transport.h"
using esp_event_base_t=const char*;
enum { ARDUINO_HW_CDC_CONNECTED_EVENT=1, ARDUINO_HW_CDC_BUS_RESET_EVENT=2,
       ARDUINO_USB_CDC_CONNECTED_EVENT=3, ARDUINO_USB_CDC_DISCONNECTED_EVENT=4, ARDUINO_USB_CDC_LINE_STATE_EVENT=5 };
struct arduino_usb_cdc_event_data_t { struct { bool dtr, rts; } line_state; };
#include "usb_events.h"
int main() {
  boot_owned(); as_source(STOLO_SRC_BLE); ble_authenticated=true; become_owner();
  scp(SCP_SET_BT,{SCP_B_ENABLED,1,0});
  auto pos=[](const char* c) { return std::find(calls.begin(),calls.end(),c)-calls.begin(); };
  CHECK(called("bt_flush") && called("delay:100") && pos("bt_flush")<pos("delay:100") && pos("delay:100")<pos("bt_stop"), "BLE flush plus bounded grace period precedes stop");
  as_source(STOLO_SRC_WIFI); reset_out(); stolo_transport_drain(); CHECK(called("wifi_flush"), "WiFi flush hook");
  as_source(STOLO_SRC_USB); reset_out(); stolo_transport_drain(); CHECK(called("usb_flush"), "USB flush hook");
  arduino_usb_cdc_event_data_t data={{false,false}};
  #if ARDUINO_USB_MODE
    const int events[]={ARDUINO_HW_CDC_CONNECTED_EVENT,ARDUINO_HW_CDC_BUS_RESET_EVENT};
  #else
    const int events[]={ARDUINO_USB_CDC_CONNECTED_EVENT,ARDUINO_USB_CDC_DISCONNECTED_EVENT,ARDUINO_USB_CDC_LINE_STATE_EVENT};
  #endif
  for(auto event:events) {
    become_owner(); CHECK(stolo_is_owner(), "owner before USB event");
    stolo_usb_event(nullptr,nullptr,event,&data); CHECK(!stolo_is_owner(), "production USB event hook revokes authority");
    CHECK(stolo_poll_session() && !stolo_session.authorized, "loop processes USB boundary");
  }
  #if !ARDUINO_USB_MODE
    become_owner(); data.line_state.dtr=true; stolo_usb_event(nullptr,nullptr,ARDUINO_USB_CDC_LINE_STATE_EVENT,&data);
    CHECK(stolo_is_owner(), "DTR-high alone does not terminate session");
  #endif
  printf("%d passed, %d failed\n",passes,failures); return failures?1:0;
}
