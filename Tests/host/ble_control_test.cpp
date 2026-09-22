// Production BLE methods, fake GATT stack. Stack enforcement and scheduling
// remain device qualification gates; these assertions check our declarations.
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <map>
#include <functional>
#include <cstdio>
#define STOLO_BUILD 1
#include "StoloBleTrace.h"
#define BLE_BUFFER_SIZE 512
#define portENTER_CRITICAL(mux) (++*(mux))
#define portEXIT_CRITICAL(mux) (--*(mux))
enum esp_gatt_perm_t { ESP_GATT_PERM_WRITE_ENC_MITM=0x40, ESP_GATT_PERM_READ_ENC_MITM=0x04 };
static bool authenticated=true;
static bool bt_client_authenticated() { return authenticated; }
struct BLEUUID { std::string uuid; std::string toString() { return uuid; } };
struct BLE2902 {
  int permissions=0; bool notifications=false;
  void setAccessPermissions(esp_gatt_perm_t p) { permissions=p; }
  void setNotifications(bool n) { notifications=n; }
  bool getNotifications() { return notifications; }
};
struct BLECharacteristic {
  enum { PROPERTY_WRITE=1, PROPERTY_NOTIFY=2 };
  std::string uuid, value; int properties, permissions=0; void* callbacks=nullptr;
  BLE2902* descriptor=nullptr;
  std::vector<std::vector<uint8_t>> notifications;
  std::function<void()> notified;
  BLECharacteristic(std::string u,int p):uuid(u),properties(p) {}
  void setAccessPermissions(esp_gatt_perm_t p) { permissions=p; }
  void setCallbacks(void* p) { callbacks=p; }
  void addDescriptor(BLE2902* d) { descriptor=d; }
  BLEUUID getUUID() { return {uuid}; }
  std::string getValue() { return value; }
  void setValue(uint8_t* b,size_t n) { value.assign((char*)b,n); }
  void notify(bool) { notifications.emplace_back(value.begin(),value.end()); if(notified) notified(); }
};
struct BLEService {
  bool started=false; std::map<std::string,BLECharacteristic*> chars;
  BLECharacteristic* createCharacteristic(const char* u,int p) { return chars[u]=new BLECharacteristic(u,p); }
  void start() { started=true; }
};
struct BLEAdvertising {
  std::vector<std::string> uuids;
  void addServiceUUID(const char* u) { uuids.emplace_back(u); }
  void setMinPreferred(int) {} void setMaxPreferred(int) {} void setScanResponse(bool) {} void start() {}
};
struct BLEServer {
  uint16_t mtu=23; std::map<std::string,BLEService*> services;
  BLEService* createService(const char* u) { return services[u]=new BLEService; }
  uint16_t getConnId() { return 1; } uint16_t getPeerMTU(uint16_t) { return mtu; }
  void startAdvertising() {}
};
struct BLEDevice { static BLEAdvertising* getAdvertising() { static BLEAdvertising adv; return &adv; } };
static void bt_connect_callback(BLEServer*) { authenticated=false; }
static void bt_disconnect_callback(BLEServer*) { authenticated=false; }
// Declare the production class's interface around the extracted methods/FIFO.
// The FIFO itself is extracted verbatim too.
#include "ble_fifo.h"
struct BLESerial {
  BLEServer* ble_server; BLEAdvertising* ble_adv=nullptr;
  BLECharacteristic *CtrlCharacteristic=nullptr,*EventCharacteristic=nullptr;
  BLE2902* EventCCCD=nullptr;
  BLEFIFO<1024> control_rx; BLEFIFO<6144> rx_buffer;
  uint32_t control_generation=0; bool control_overflow=false; int control_mux=0;
  size_t transmitBufferLength=0,numAvailableLines=0;
  const char* BLE_RX_UUID="6e400002-b5a3-f393-e0a9-e50e24dcca9e";
  const char* BLE_SERIAL_SERVICE_UUID="6e400001-b5a3-f393-e0a9-e50e24dcca9e";
  bool connected() { return true; }
  void SetupControlService(); void resetControl(); uint32_t controlGeneration();
  int readControl(uint32_t* generation); void writeControl(const uint8_t*,size_t,uint32_t);
  void onWrite(BLECharacteristic*); void onConnect(BLEServer*); void onDisconnect(BLEServer*);
  void startAdvertising();
};
#include "ble_control.h"
static int passes=0,failures=0;
#define CHECK(c,label) do { if(c) ++passes; else { ++failures; fprintf(stderr,"FAIL: %s\n",label); } } while(0)
int main() {
  BLEServer server; BLESerial ble; ble.ble_server=&server; ble.SetupControlService();
  auto service=server.services["d8b6a9ad-bf50-45f0-997c-2b54d82bec2d"];
  CHECK(service && service->started && service->chars.size()==2,"one service, CTRL/EVENT only; no BULK");
  CHECK(ble.CtrlCharacteristic->uuid=="d8b6a9ad-bf50-45f0-997c-2b54d82bec2e" && ble.CtrlCharacteristic->properties==BLECharacteristic::PROPERTY_WRITE,"CTRL with-response writes only");
  CHECK(ble.CtrlCharacteristic->permissions==ESP_GATT_PERM_WRITE_ENC_MITM && ble.CtrlCharacteristic->callbacks==&ble,"CTRL requires MITM encrypted writes");
  CHECK(ble.EventCharacteristic->uuid=="d8b6a9ad-bf50-45f0-997c-2b54d82bec2f" && ble.EventCharacteristic->properties==BLECharacteristic::PROPERTY_NOTIFY,"EVENT notify only");
  CHECK(ble.EventCharacteristic->permissions==ESP_GATT_PERM_READ_ENC_MITM,"EVENT permission declaration");
  CHECK(ble.EventCCCD->permissions==(ESP_GATT_PERM_READ_ENC_MITM|ESP_GATT_PERM_WRITE_ENC_MITM),"EVENT CCCD read AND subscription write protected");
  ble.startAdvertising(); CHECK(ble.ble_adv->uuids==std::vector<std::string>{ble.BLE_SERIAL_SERVICE_UUID},"only NUS advertised");
  ble.CtrlCharacteristic->value="abc"; authenticated=false; ble.onWrite(ble.CtrlCharacteristic);
  uint32_t generation; CHECK(ble.readControl(&generation)==-1,"application guard drops unencrypted CTRL");
  authenticated=true; ble.onWrite(ble.CtrlCharacteristic);
  CHECK(ble.readControl(&generation)=='a' && generation==ble.controlGeneration(),"loop reads tagged CTRL queue");
  BLECharacteristic nus(ble.BLE_RX_UUID,1); nus.value="N"; ble.onWrite(&nus);
  CHECK(ble.rx_buffer.pop()=='N' && ble.readControl(&generation)=='b',"NUS and CTRL queue isolation");
  ble.CtrlCharacteristic->value=std::string(512,'x'); ble.onWrite(ble.CtrlCharacteristic); ble.onWrite(ble.CtrlCharacteristic);
  CHECK(ble.readControl(&generation)==-2 && ble.readControl(&generation)==-1,"queue overflow reports lost stream, never overwrites unread bytes");
  ble.CtrlCharacteristic->value="Z"; ble.onWrite(ble.CtrlCharacteristic);
  CHECK(ble.readControl(&generation)=='Z',"queue recovers after overflow");
  auto event=ble.EventCharacteristic; uint8_t bytes[255]; for(size_t i=0;i<sizeof(bytes);++i) bytes[i]=i;
  ble.writeControl(bytes,sizeof(bytes),ble.controlGeneration()); CHECK(event->notifications.empty(),"no output before subscribe");
  ble.EventCCCD->setNotifications(true);
  for(uint16_t mtu:{23,50,247,517}) {
    server.mtu=mtu; event->notifications.clear(); ble.writeControl(bytes,sizeof(bytes),ble.controlGeneration());
    std::vector<uint8_t> joined;
    for(auto& chunk:event->notifications) { CHECK(chunk.size()<=size_t(mtu-3) && chunk.size()<=512,"notification respects negotiated MTU"); joined.insert(joined.end(),chunk.begin(),chunk.end()); }
    CHECK(joined==std::vector<uint8_t>(bytes,bytes+sizeof(bytes)),"chunked EVENT bytes exactly reproduce frame");
  }
  ble.onWrite(ble.CtrlCharacteristic); auto old=ble.controlGeneration();
  ble.onDisconnect(&server);
  CHECK(ble.controlGeneration()!=old && ble.readControl(&generation)==-1 && !ble.EventCCCD->getNotifications() && event->value.empty(),"disconnect discards queue, subscription and last attribute value");
  ble.onConnect(&server); authenticated=true; ble.EventCCCD->setNotifications(true); event->notifications.clear();
  ble.writeControl(bytes,sizeof(bytes),old); CHECK(event->notifications.empty(),"stale reply cannot be sent on replacement link");
  server.mtu=23; event->notified=[&]() { ble.onDisconnect(&server); };
  ble.writeControl(bytes,sizeof(bytes),ble.controlGeneration());
  CHECK(event->notifications.size()==1,"disconnect during multi-chunk reply stops subsequent chunks");
  CHECK(ble.control_mux==0,"all queue critical sections released");
  printf("%d passed, %d failed\n",passes,failures); return failures?1:0;
}
