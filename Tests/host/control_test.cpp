// Production CTRL parser/common dispatcher and full NUS KISS dispatcher.
// Hardware/crypto fakes: software verified, not device qualified.
#include "dispatcher_env.h"
#include <algorithm>

static std::vector<uint8_t> frame(uint8_t cmd, const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> wire={FEND,cmd};
  for (auto b:payload) {
    if (b==FEND || b==FESC) { wire.push_back(FESC); wire.push_back(b==FEND?TFEND:TFESC); }
    else wire.push_back(b);
  }
  wire.push_back(FEND); return wire;
}
static void feed(const std::vector<uint8_t>& wire) { for(auto b:wire) stolo_ctrl_feed(b,fake_ble_link); }
static void ctrl(uint8_t type, const std::vector<uint8_t>& body={}, uint8_t seq=1) {
  reset_out(); std::vector<uint8_t> payload={SCP_VERSION,type,seq};
  payload.insert(payload.end(),body.begin(),body.end()); feed(frame(CMD_STOLO,payload));
  CHECK(out.empty(), "CTRL request writes no serial bytes");
}
static std::vector<uint8_t> response() {
  std::vector<uint8_t> decoded; bool escape=false;
  for (size_t i=2;i+1<event_out.size();++i) {
    auto b=event_out[i];
    if (escape) { decoded.push_back(b==TFEND?FEND:b==TFESC?FESC:b); escape=false; }
    else if (b==FESC) escape=true;
    else decoded.push_back(b);
  }
  return decoded;
}
static bool event_reply(uint8_t type) { auto b=response(); return b.size()>=3 && b[0]==SCP_VERSION && b[1]==(type|SCP_REPLY); }
static bool event_error(uint8_t code) { auto b=response(); return b.size()>=4 && b[1]==SCP_ERROR && b[3]==code; }
static void ble_guest() { boot_owned(); ble_authenticated=true; as_source(STOLO_SRC_BLE); ctrl(SCP_HELLO); }
static void owner() { ctrl(SCP_HELLO); ctrl(SCP_AUTH,std::vector<uint8_t>(64,1)); CHECK(stolo_is_owner(),"CTRL AUTH is BLE owner"); }
static size_t at(const char* name) { return std::find(calls.begin(),calls.end(),name)-calls.begin(); }
static void ordered(const char* action) {
  CHECK(called("event_write") && called("stolo_transport_drain") && called(action)
    && at("event_write")<at("stolo_transport_drain") && at("stolo_transport_drain")<at(action),"EVENT -> drain -> link action (not delivery proof)");
}
static void test_roundtrips_and_authority() {
  ble_guest(); CHECK(event_reply(SCP_HELLO) && stolo_role()==STOLO_ROLE_GUEST,"bonded nonowner HELLO is guest");
  ctrl(SCP_GET_RADIO,{},FEND); CHECK(event_reply(SCP_GET_RADIO) && response()[2]==FEND,"escaped sequence round-trips through EVENT");
  for(auto type:{SCP_GET_WIFI,SCP_GET_BT,SCP_SET_RADIO,SCP_SET_WIFI,SCP_SET_BT,SCP_FORGET_OWNER,SCP_RESCUE}) {
    ctrl(type); CHECK(event_error(SCP_ERR_UNAUTHORIZED),"guest privileged SCP operation refused");
  }
  ctrl(SCP_ENROLL,std::vector<uint8_t>(96,1)); CHECK(event_error(SCP_ERR_ENROLL_CLOSED),"guest cannot replace owner");
  ctrl(SCP_HELLO); ed_verify_result=false; ctrl(SCP_AUTH,std::vector<uint8_t>(64,1));
  CHECK(event_reply(SCP_AUTH) && response()[3]==0 && !stolo_is_owner(),"bad signature cannot authenticate");
  ed_verify_result=true; owner();
  ctrl(SCP_GET_WIFI); CHECK(event_reply(SCP_GET_WIFI),"owner GET_WIFI");
  ctrl(SCP_GET_BT); CHECK(event_reply(SCP_GET_BT),"owner GET_BT");
  ctrl(SCP_SET_WIFI,{SCP_W_SSID,2,FEND,FESC}); CHECK(event_reply(SCP_SET_WIFI),"escaped SET_WIFI round-trip");
  CHECK((uint8_t)wr_ssid[0]==FEND && (uint8_t)wr_ssid[1]==FESC,"CTRL payload unescapes independently");
  bt_state=BT_STATE_CONNECTED;
  ctrl(SCP_SET_BT,{SCP_B_PAIRING,1,1}); CHECK(event_error(SCP_ERR_BAD_REQUEST),"pairing-open while connected retains existing error 1");
  ctrl(SCP_SET_BT,{SCP_B_WINDOW,2,0,60}); CHECK(event_reply(SCP_SET_BT) && stolo_cfg.bt_window_s==60,"connected owner may set pairing-window duration");
  ctrl(SCP_SET_RADIO,{SCP_R_STATE,1,0}); CHECK(event_reply(SCP_SET_RADIO) && !radio_online,"owner radio lifecycle admitted");
  reset_out(); kiss(CMD_FREQUENCY,{0x36,0x89,0xCA,0xC0});
  CHECK(lora_freq==915000000 && called("setFrequency"),"AUTH on CTRL authorizes NUS frequency path");
  ctrl(SCP_LEAVE,{1}); CHECK(event_error(SCP_ERR_BAD_REQUEST) && stolo_is_owner(),"malformed LEAVE does not revoke");
  ctrl(SCP_LEAVE); CHECK(event_reply(SCP_LEAVE) && !stolo_session.hello_seen && !stolo_is_owner(),"SCP LEAVE revokes shared authority without link action");
  reset_out(); kiss(CMD_RADIO_STATE,{1}); kiss(CMD_RESET,{CMD_RESET_BYTE});
  CHECK(!called("startRadio") && !called("hard_reset"),"guest NUS radio lifecycle/reset refused");
  owner(); reset_out(); scp(SCP_GET_RADIO); CHECK(replied(SCP_GET_RADIO|SCP_REPLY) && event_out.empty(),"NUS SCP still replies on NUS");
  reset_out(); uint8_t body=FEND; stolo_scp_event(0x50,&body,1);
  CHECK(out.empty() && response()==std::vector<uint8_t>({SCP_VERSION,0x50,0,FEND}),"unsolicited BLE event uses EVENT");
  ctrl(SCP_HELLO); CHECK(!stolo_is_owner(),"second HELLO revokes owner");
}
static void test_interleaving_and_faults() {
  ble_guest();
  auto hello=frame(CMD_STOLO,{SCP_VERSION,SCP_HELLO,FESC});
  reset_out(); for(size_t i=0;i<hello.size()/2;++i) stolo_ctrl_feed(hello[i],fake_ble_link);
  auto start=queue_cursor; kiss(CMD_DATA,{0x11,FEND,0x22,FESC,0x33});
  for(size_t i=hello.size()/2;i<hello.size();++i) stolo_ctrl_feed(hello[i],fake_ble_link);
  CHECK(event_reply(SCP_HELLO) && response()[2]==FESC,"chunked CTRL frame survives interleaved NUS data");
  CHECK(packet_queue[start]==0x11 && packet_queue[start+1]==FEND && packet_queue[start+3]==FESC,"NUS payload is intact");
  // Reverse interleave: CTRL HELLO and LEAVE cannot truncate an unfinished NUS frame.
  serial_callback(FEND); serial_callback(CMD_DATA); serial_callback(0x55); serial_callback(FESC);
  ctrl(SCP_HELLO); ctrl(SCP_LEAVE);
  CHECK(IN_FRAME && ESCAPE && command==CMD_DATA,"CTRL HELLO/LEAVE preserves unfinished NUS data/escape state");
  serial_callback(TFEND); serial_callback(0x66); serial_callback(FEND);
  CHECK(stolo_partial_data_bytes==0,"NUS data frame finishes after CTRL session operations");
  ctrl(SCP_HELLO);
  // Two simultaneous SCP reassemblies have their own payload buffers too.
  auto nus=frame(CMD_STOLO,{SCP_VERSION,SCP_GET_RADIO,0x44});
  for(size_t i=0;i<nus.size()-1;++i) serial_callback(nus[i]);
  ctrl(SCP_GET_BT); CHECK(event_error(SCP_ERR_UNAUTHORIZED),"CTRL error while NUS SCP is incomplete");
  reset_out(); serial_callback(nus.back()); CHECK(replied(SCP_GET_RADIO|SCP_REPLY) && out[4]==0x44 && event_out.empty(),"NUS SCP buffer/sink preserved");
  auto faults=stolo_ctrl_parser.faults; reset_out(); feed(frame(CMD_FREQUENCY,{1,2,3,4}));
  CHECK(stolo_ctrl_parser.faults==faults+1 && event_out.empty() && out.empty(),"non-SCP CTRL frame dropped and counted once");
  feed({FEND,CMD_STOLO,SCP_VERSION,SCP_HELLO,1,FESC,0x01,FEND});
  CHECK(stolo_ctrl_parser.faults==faults+2,"invalid escape counted");
  feed({FEND,CMD_STOLO,SCP_VERSION,SCP_HELLO,1,FESC,FEND});
  CHECK(stolo_ctrl_parser.faults==faults+3,"dangling escape counted");
  feed(frame(CMD_STOLO,std::vector<uint8_t>(STOLO_MSG_MAX+1,1)));
  CHECK(stolo_ctrl_parser.faults==faults+4,"oversized carrier dropped and counted");
  ctrl(SCP_GET_RADIO); CHECK(event_reply(SCP_GET_RADIO),"parser recovers at next FEND");
  reset_out(); feed(frame(CMD_STOLO,{SCP_VERSION})); CHECK(event_error(SCP_ERR_BAD_REQUEST),"short SCP gets EVENT error");
}
static void test_boundaries_and_ordering() {
  ble_guest(); owner(); fake_millis+=STOLO_SESSION_IDLE_MS+1;
  reset_out(); kiss(CMD_SF,{12}); CHECK(!stolo_is_owner() && lora_sf==8,"idle expiry revokes NUS and CTRL authority");
  ctrl(SCP_HELLO); CHECK(stolo_role()==STOLO_ROLE_GUEST,"HELLO after idle is guest");
  owner(); feed({FEND,CMD_STOLO,SCP_VERSION,SCP_SET_BT,1,FESC});
  stolo_host_disconnected(); CHECK(!stolo_ctrl_parser.in_frame && !stolo_is_owner(),"disconnect clears CTRL partial frame and authority");
  bt_state=2; bt_allow_pairing=true; ble_authenticated=false;
  stolo_session.source=STOLO_SRC_BLE; stolo_ble_connection_boundary();
  CHECK(stolo_poll_session() && bt_state==2 && bt_allow_pairing && !ble_authenticated,
        "real BLE boundary teardown preserves in-flight pairing state and permission");
  bt_state=BT_STATE_CONNECTED; bt_allow_pairing=false; ble_authenticated=true;
  ctrl(SCP_HELLO); CHECK(stolo_role()==STOLO_ROLE_GUEST,"HELLO after disconnect is guest");
  owner(); stolo_ble_connection_boundary(); ++fake_ble_link;
  CHECK(!stolo_is_owner(),"callback boundary immediately revokes owner");
  stolo_poll_session(); reset_out();
  for(auto b:frame(CMD_STOLO,{SCP_VERSION,SCP_HELLO,1})) stolo_ctrl_feed(b,fake_ble_link-1);
  CHECK(event_out.empty() && !stolo_session.hello_seen,"old-link bytes cannot enter new session");
  owner(); ble_authenticated=false; CHECK(stolo_role()==STOLO_ROLE_GUEST,"lost BLE authentication removes owner");
  ctrl(SCP_GET_WIFI); CHECK(event_out.empty(),"unauthenticated CTRL dropped at application guard");
  boot_unowned(); as_source(STOLO_SRC_BLE); CHECK(stolo_role()==STOLO_ROLE_GUEST,"unbonded link has no factory compat");
  ble_authenticated=true; CHECK(stolo_role()==STOLO_ROLE_COMPAT,"bonded factory link gets decided once-only compat");
  ble_guest(); owner(); ctrl(SCP_SET_BT,{SCP_B_ENABLED,1,0}); CHECK(event_reply(SCP_SET_BT),"disable reply on EVENT"); ordered("bt_stop");
  ctrl(SCP_SET_BT,{SCP_B_DEBOND,1,1}); ordered("bt_debond_all");
  ctrl(SCP_ENROLL,std::vector<uint8_t>(96,0x22)); CHECK(event_reply(SCP_ENROLL),"ENROLL reply sink survives session reset"); ordered("bt_debond_all");
  owner(); ctrl(SCP_FORGET_OWNER); CHECK(event_reply(SCP_FORGET_OWNER),"FORGET reply sink survives session reset"); ordered("bt_debond_all");
  CHECK(stolo_role()==STOLO_ROLE_GUEST,"FORGET never reopens compat on BLE");
}
int main() {
  test_roundtrips_and_authority(); test_interleaving_and_faults(); test_boundaries_and_ordering();
  printf("%d passed, %d failed\n",passes,failures); return failures?1:0;
}
