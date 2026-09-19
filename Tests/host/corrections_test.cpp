// Review C4-C11 regressions: production dispatcher, software verified only.
#include "dispatcher_env.h"
#include <algorithm>
static size_t call_at(const char* c) { auto it=std::find(calls.begin(),calls.end(),c); return it-calls.begin(); }
static size_t last_write() { size_t at=0; for(size_t i=0;i<calls.size();++i) if(calls[i]=="serial_write") at=i; return at; }
static void reply_drained_before(const char* action) {
  CHECK(called("stolo_transport_drain") && call_at("stolo_transport_drain")>last_write() && call_at(action)>call_at("stolo_transport_drain"), "reply -> drain -> link action (ordering, not delivery proof)");
}
static void test_usb_and_legacy() {
  boot_owned(); become_owner(); serial_callback(FEND); serial_callback(CMD_SF);
  stolo_usb_connection_boundary();
  CHECK(!stolo_is_owner(), "USB event immediately removes authority");
  serial_callback(12); CHECK(lora_sf==8 && !IN_FRAME, "USB event aborts unfinished frame in loop");
  become_owner(); fake_millis+=STOLO_USB_SESSION_IDLE_MS+1;
  kiss(CMD_SF,{12}); CHECK(lora_sf==8 && !stolo_is_owner(), "USB idle expiry is checked before refreshing activity");
  become_owner(); fake_millis+=20000; scp(SCP_GET_RADIO); fake_millis+=20000; stolo_update();
  CHECK(stolo_is_owner(), "USB public GET keepalive maintains active session");
  reset_out(); kiss(CMD_RESET,{CMD_RESET_BYTE});
  CHECK(called("hard_reset") && !stolo_is_owner(), "owner RESET executes and ends session");
  reset_out(); kiss(CMD_RESET,{CMD_RESET_BYTE}); CHECK(!called("hard_reset"), "guest RESET refused");
  memset(dev_sig,0xAA,sizeof(dev_sig)); reset_out(); kiss(CMD_DEV_SIG,std::vector<uint8_t>(64,0x11));
  CHECK(dev_sig[0]==0xAA && !called("device_save_signature"), "guest DEV_SIG cannot mutate RAM or persist");
  become_owner(); reset_out(); kiss(CMD_DEV_SIG,std::vector<uint8_t>(64,0x11));
  CHECK(dev_sig[0]==0x11 && called("device_save_signature"), "owner DEV_SIG reaches mutation");
  reset_out(); kiss(CMD_LEAVE,{0xFF}); CHECK(!stolo_is_owner(), "LEAVE executes then invalidates session");
}
static void test_set_truth_and_faults() {
  boot_owned(); become_owner(); memset(EEPROM.bytes,0,sizeof(EEPROM.bytes)); strcpy(wr_ssid,"OLD"); wr_psk[0]=0; wr_channel=1;
  ee_write_count=0; ee_fail_call=-1;
  scp(SCP_SET_WIFI,{SCP_W_SSID,3,'N','E','W',SCP_W_CHN,1,6,SCP_W_PSK,4,'p','a','s','s'});
  auto b=last_body();
  CHECK(replied(SCP_SET_WIFI|SCP_REPLY) && b[2]==6 && b[3]==3 && std::string(b.begin()+4,b.begin()+7)=="NEW" && b[7]==1 && b[b.size()-2]==14 && b.back()==SCP_PENDING_RUNTIME, "SET_WIFI reply describes accepted SSID/channel/PSK and pending runtime");
  reply_drained_before("wifi_remote_init");
  scp(SCP_GET_WIFI); b=last_body(); CHECK(b[2]==6 && b[7]==1 && std::string(b.begin()+4,b.begin()+7)=="NEW", "GET_WIFI observes effective settings after reload");
  strcpy(wr_ssid,"STALE"); wr_psk[0]=0; wr_channel=1;
  scp(SCP_SET_WIFI,{SCP_W_MODE,1,WR_WIFI_AP}); b=last_body();
  CHECK(b[2]==6 && b[7]==1 && std::string(b.begin()+4,b.begin()+7)=="NEW", "unspecified WiFi reply fields use persisted next image, not stale runtime");
  for (int failure: {1,2,33,34,66,67,68}) {
    ee_write_count=0; ee_fail_call=failure;
    scp(SCP_SET_WIFI,{SCP_W_SSID,3,'N','E','W',SCP_W_CHN,1,6,SCP_W_PSK,4,'p','a','s','s'});
    CHECK(errored(SCP_ERR_STORE_FAILED) && !called("wifi_remote_init"), "EEPROM write failure returns error 5 without runtime restart");
  }
  ee_fail_call=-1; bt_state=BT_STATE_ON; bt_enabled=true;
  scp(SCP_SET_BT,{SCP_B_ENABLED,1,0}); b=last_body();
  CHECK(b[6]==0 && b[7]==8 && b[8]==SCP_PENDING_RUNTIME && !bt_enabled, "SET_BT disable reply accepts disabled and marks pending");
  reply_drained_before("bt_stop");
  scp(SCP_GET_BT); CHECK(last_body()[6]==0 && last_body()[1]==BT_STATE_OFF, "GET_BT observes stopped runtime");
  bt_state=BT_STATE_ON; bt_enabled=true; ee_write_count=0; ee_fail_call=1;
  scp(SCP_SET_BT,{SCP_B_ENABLED,1,0}); CHECK(errored(SCP_ERR_STORE_FAILED) && !called("bt_stop") && bt_enabled, "BT EEPROM failure reported without stopping");
  ee_fail_call=-1;
  scp(SCP_SET_BT,{SCP_B_DEBOND,1,1}); reply_drained_before("bt_debond_all");
  scp(SCP_ENROLL,std::vector<uint8_t>(96,0x12)); reply_drained_before("bt_debond_all");
  become_owner(); scp(SCP_FORGET_OWNER); reply_drained_before("bt_debond_all");
}
static void test_compat_and_carriers() {
  boot_owned(); become_owner(); scp(SCP_FORGET_OWNER);
  CHECK(!stolo_cfg.owner_enrolled && stolo_ever_enrolled() && stolo_role()==STOLO_ROLE_GUEST, "FORGET_OWNER never restores factory compat");
  stolo_store_init(); stolo_session_reset(STOLO_SRC_USB);
  CHECK(stolo_role()==STOLO_ROLE_GUEST && !stolo_kiss_gate(CMD_CFG_READ), "ever-enrolled lock persists over restart");
  scp(SCP_HELLO); scp(SCP_ENROLL,std::vector<uint8_t>(96,0x12));
  CHECK(replied(SCP_ENROLL|SCP_REPLY), "unowned locked store still permits present-host SCP enrollment");
  as_source(STOLO_SRC_WIFI); scp(SCP_HELLO);
  for (uint8_t type : {SCP_AUTH,SCP_ENROLL,SCP_FORGET_OWNER,SCP_RESCUE,SCP_SET_RADIO,SCP_SET_WIFI,SCP_SET_BT,SCP_GET_WIFI,SCP_GET_BT}) {
    scp(type,std::vector<uint8_t>(96,0x11)); CHECK(errored(SCP_ERR_NOT_SUPPORTED), "raw WiFi control is not supported");
  }
  scp(SCP_GET_RADIO); CHECK(replied(SCP_GET_RADIO|SCP_REPLY), "WiFi public radio read remains available");
  as_source(STOLO_SRC_BLE); ble_authenticated=false; scp(SCP_HELLO); scp(SCP_AUTH,std::vector<uint8_t>(64,1));
  CHECK(errored(SCP_ERR_NOT_SUPPORTED), "unencrypted BLE cannot authenticate");
  ble_authenticated=true; become_owner(); CHECK(stolo_is_owner(), "encrypted BLE may authenticate");
  ble_authenticated=false; CHECK(!stolo_is_owner(), "loss of encrypted BLE state removes authority");
}
static void corrupt_store() { for (auto key:stolo_slot_keys) stolo_prefs.data[key].push_back(0); stolo_store_init(); stolo_session_reset(STOLO_SRC_USB); }
static void test_rescue_and_entropy() {
  boot_owned(); auto old=stolo_cfg; int boot_calls=boot_entropy_enables;
  for(int i=0;i<5;++i) { become_owner(); }
  CHECK(boot_entropy_enables==boot_calls && boot_entropy_enables==boot_entropy_disables, "runtime nonces never enable boot entropy");
  corrupt_store(); scp(SCP_HELLO); scp(SCP_RESCUE,{'R','E','S','C','U','E'});
  CHECK(errored(SCP_ERR_UNAUTHORIZED), "rescue denied without physical window");
  stolo_enroll_window_until=millis()+60000; scp(SCP_RESCUE);
  CHECK(errored(SCP_ERR_BAD_REQUEST), "rescue requires explicit destructive token");
  scp(SCP_RESCUE,{'R','E','S','C','U','E'});
  CHECK(replied(SCP_RESCUE|SCP_REPLY) && stolo_store_ok && !stolo_cfg.owner_enrolled && memcmp(old.node_pub,stolo_cfg.node_pub,32)!=0, "rescue replaces identity with an unowned readable store");
  CHECK(called("rescue_announce") && call_at("rescue_announce")<last_write() && !stolo_session.hello_seen && !stolo_enroll_window_open(), "rescue announces, consumes window and ends session");
  reply_drained_before("bt_debond_all");
  CHECK(boot_entropy_enables==boot_calls, "runtime rescue reuses seeded DRBG");
  scp(SCP_HELLO); scp(SCP_ENROLL,std::vector<uint8_t>(96,0x44));
  CHECK(replied(SCP_ENROLL|SCP_REPLY) && stolo_cfg.owner_enrolled, "rescued node enrolls a new owner end to end");
  stolo_store_init(); CHECK(stolo_store_ok && stolo_cfg.owner_pub[0]==0x44, "rescued enrollment survives restart");
  boot_owned(); stolo_prefs.fail_begin=true; stolo_store_init(); stolo_session_reset(STOLO_SRC_USB);
  scp(SCP_HELLO); CHECK(replied(SCP_HELLO|SCP_REPLY), "HELLO remains available after namespace-open failure");
  stolo_prefs.fail_begin=false; stolo_enroll_window_until=millis()+60000;
  scp(SCP_RESCUE,{'R','E','S','C','U','E'});
  CHECK(replied(SCP_RESCUE|SCP_REPLY) && stolo_store_ok, "explicit rescue retries namespace open");
  // Every persistence operation in rescue can fail and stays retryable.
  for(int fault=1;fault<=9;++fault) {
    boot_owned(); corrupt_store(); auto before=stolo_prefs.data;
    stolo_prefs.write_count=0; stolo_prefs.fail_call=fault;
    CHECK(!stolo_store_rescue(), "rescue reports each individual write/remove failure");
    stolo_prefs.fail_call=-1; stolo_store_init();
    CHECK(!stolo_store_ok && stolo_store_state==STOLO_STORE_RECOVERY, "interrupted rescue never silently creates factory compat");
    stolo_enroll_window_until=millis()+60000; scp(SCP_HELLO); scp(SCP_RESCUE,{'R','E','S','C','U','E'});
    CHECK(replied(SCP_RESCUE|SCP_REPLY) && stolo_store_ok, "physical rescue can be retried after every fault");
  }
}
int main() {
  test_usb_and_legacy(); test_set_truth_and_faults(); test_compat_and_carriers(); test_rescue_and_entropy();
  printf("%d passed, %d failed\n",passes,failures); return failures?1:0;
}
