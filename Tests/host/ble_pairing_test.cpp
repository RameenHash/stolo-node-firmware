// Software verified: extracted ESP32 BLE callbacks, fake clock/stack/display.
#include <cstdint>
#include <cstdio>
#define STOLO_BUILD 1
#include "StoloBleTrace.h"
enum { BT_STATE_OFF, BT_STATE_ON, BT_STATE_PAIRING, BT_STATE_CONNECTED };
#define CABLE_STATE_DISCONNECTED 0
#define BT_PAIRING_TIMEOUT 120000u
static uint32_t now=1000, bt_pairing_started=1000, bt_ssp_pin=123456, pairing_pin=123456;
static uint8_t bt_state=BT_STATE_PAIRING, cable_state=0;
static bool bt_allow_pairing=true, ble_authenticated=false;
static int bonds=0, boundaries=0, disconnects=0, pin_notifications=0, security_setups=0;
static uint32_t millis() { return now; }
static bool stolo_bt_has_bonds() { return bonds>0; }
static void display_unblank() {}
static void kiss_indicate_btpin() { ++pin_notifications; }
static void stolo_ble_connection_boundary() { ++boundaries; }
static void bt_update_passkey() { bt_ssp_pin=pairing_pin=234567; }
static void bt_security_setup() { ++security_setups; }
struct { void disconnect() { ++disconnects; } } SerialBT;
struct BLEServer { uint16_t getConnId() { return 1; } } server;
struct esp_ble_auth_cmpl_t { bool success; uint8_t fail_reason=0x51, auth_mode=0; };
#include "ble_pairing.h"
static int passes=0,failures=0;
#define CHECK(c,label) do { if(c) ++passes; else { ++failures; fprintf(stderr,"FAIL: %s\n",label); } } while(0)
static void open_window(int bond_count=0, uint32_t started=1000) {
  bonds=bond_count; now=started; bt_pairing_started=started;
  bt_state=BT_STATE_PAIRING; bt_allow_pairing=true; ble_authenticated=false;
  bt_ssp_pin=pairing_pin=123456; disconnects=boundaries=pin_notifications=security_setups=0;
}
int main() {
  open_window(); bt_connect_callback(&server);
  CHECK(boundaries==1 && bt_state==BT_STATE_PAIRING && bt_allow_pairing && !ble_authenticated,
        "fresh connect revokes protocol authority without closing pairing");
  CHECK(bt_security_request_callback(), "fresh pairing security request accepted");
  bt_passkey_notify_callback(654321);
  CHECK(bt_ssp_pin==654321 && pin_notifications==1 && disconnects==0, "stack passkey replaces display code");
  now+=BT_PAIRING_TIMEOUT+1;
  bt_authentication_complete_callback({false});
  CHECK(bt_allow_pairing && bt_state==BT_STATE_PAIRING && bt_ssp_pin==234567 && !ble_authenticated,
        "no-bond authentication failure keeps new code and pairing open past window duration");
  CHECK(bt_pairing_started==1000 && disconnects==0 && security_setups==1,
        "failure does not extend deadline or request a disconnect");
  bt_disconnect_callback(&server);
  CHECK(bt_allow_pairing && bt_state==BT_STATE_PAIRING && boundaries==2 && bt_security_request_callback(),
        "peer disconnect preserves retry permission and posts protocol boundary");
  bt_authentication_complete_callback({true});
  CHECK(ble_authenticated && bt_state==BT_STATE_CONNECTED && !bt_allow_pairing && bt_ssp_pin==0 && disconnects==0,
        "successful pairing keeps link and clears code/window");
  bonds=1; bt_disconnect_callback(&server);
  CHECK(bt_state==BT_STATE_ON && !bt_allow_pairing && !ble_authenticated, "bonded disconnect stays closed");
  open_window(1); now+=BT_PAIRING_TIMEOUT-1; bt_authentication_complete_callback({false});
  CHECK(bt_state==BT_STATE_PAIRING && bt_allow_pairing && bt_pairing_started==1000,
        "bonded window retains remaining time after failure");
  now+=1; bt_authentication_complete_callback({false});
  CHECK(bt_state==BT_STATE_ON && !bt_allow_pairing && bt_ssp_pin==0,
        "failure at bonded deadline closes window");
  open_window(0); bt_allow_pairing=false; bt_authentication_complete_callback({false});
  CHECK(!bt_allow_pairing && bt_state==BT_STATE_ON && bt_ssp_pin==0 && !bt_security_request_callback(),
        "failure never reopens explicitly closed window even without bonds");
  bt_passkey_notify_callback(123456);
  CHECK(disconnects==1 && pin_notifications==0, "passkey notification after explicit close rejects link");
  open_window(1, UINT32_MAX-60000); now+=119999;
  CHECK(stolo_bt_pairing_window_open(), "bonded retry deadline handles millis wrap");
  now+=1; CHECK(!stolo_bt_pairing_window_open(), "bonded window expires across wrap");
  open_window(); bt_disconnect_callback(&server);
  CHECK(bt_state==BT_STATE_PAIRING && bt_ssp_pin==123456 && bt_allow_pairing,
        "cancel before auth completion preserves pairing display and permission");
  printf("%d passed, %d failed\n",passes,failures); return failures?1:0;
}
