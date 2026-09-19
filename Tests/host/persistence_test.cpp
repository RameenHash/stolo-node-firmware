// Actual EEPROM helpers, with flash commit failure and dirty-cache model.
#include <cstdint>
#include <cstdio>
#include <cstring>
#define MCU_VARIANT 1
#define MCU_ESP32 1
#define MCU_1284P 2
#define MCU_2560 3
#define MCU_NRF52 4
#define HAS_EEPROM 1
#define ADDR_CONF_WIFI 0
#define ADDR_CONF_BT 1
#define BT_ENABLE_BYTE 0x73
struct EE {
  uint8_t cache[2]={}, flash[2]={}; bool fail=false; int commits=0;
  uint8_t read(int p) { return cache[p]; }
  void write(int p,uint8_t b) { cache[p]=b; }
  bool commit() { ++commits; if(fail) return false; memcpy(flash,cache,2); return true; }
} EEPROM;
bool bt_enabled=true;
int eeprom_addr(int p) { return p; }
#include "persistence.h"
int main() {
  EEPROM.fail=true;
  if(wr_conf_save(2) || bt_conf_save(false) || !bt_enabled || EEPROM.flash[0]!=0) return 1;
  EEPROM.fail=false;
  if(!wr_conf_save(2) || EEPROM.flash[0]!=2 || EEPROM.commits!=3) return 2;
  if(!bt_conf_save(false) || bt_enabled) return 3;
  puts("4 persistence checks passed, 0 failed");
}
