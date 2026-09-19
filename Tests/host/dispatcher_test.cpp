// Software verified: full serial_callback extraction, hardware/crypto stubs.
#include "dispatcher_env.h"
int main() {
  for (uint8_t from : {STOLO_SRC_USB, STOLO_SRC_BLE, STOLO_SRC_WIFI}) {
    for (uint8_t to : {STOLO_SRC_USB, STOLO_SRC_BLE, STOLO_SRC_WIFI}) {
      if (from == to) continue;
      boot_owned(); as_source(from); ble_authenticated = true;
      // Model an already authorized source for the boundary test, independent of carrier admission.
      stolo_session.authorized=true; stolo_session.auth_epoch=stolo_cfg.owner_epoch;
      serial_callback(FEND); serial_callback(CMD_SF);
      stolo_buffer_byte(from, 12); // old-source byte is still queued
      as_source(to);
      CHECK(serialFIFO.bytes.empty() && !IN_FRAME && !ESCAPE && command==CMD_UNKNOWN && frame_len==0, "source transition aborts parser and FIFO");
      serial_callback(12); serial_callback(FEND);
      CHECK(lora_sf==8 && !stolo_is_owner(), "partial SF cannot cross any transport transition");
      // Partial SCP and escape state are discarded too.
      as_source(from); serial_callback(FEND); serial_callback(CMD_STOLO); serial_callback(SCP_VERSION); serial_callback(FESC);
      as_source(to);
      CHECK(stolo_rx_len==0 && !stolo_rx_overflow && !ESCAPE, "SCP and escaping abort at transition");
    }
  }
  boot_owned(); become_owner(); serial_callback(FEND); serial_callback(CMD_SF);
  stolo_host_disconnected(); serial_callback(12);
  CHECK(lora_sf==8 && !IN_FRAME, "mid-frame disconnect aborts SF");
  become_owner(); serial_callback(FEND); serial_callback(CMD_SF);
  stolo_session.authorized=false; serial_callback(12);
  CHECK(lora_sf==8, "mutation rechecks role without a parser reset");
  become_owner(); kiss(CMD_SF,{12}); CHECK(lora_sf==12, "authorized complete SF still works");
  boot_owned(); serial_callback(FEND); serial_callback(CMD_DATA);
  for (int i=0; i<CONFIG_QUEUE_SIZE; ++i) serial_callback(0x42);
  CHECK(queued_bytes==CONFIG_QUEUE_SIZE && queue_cursor==current_packet_start, "partial data can wrap the entire queue");
  stolo_host_disconnected();
  CHECK(queued_bytes==0 && stolo_partial_data_bytes==0, "disconnect discards a full wrapped partial data frame");
  printf("%d passed, %d failed\n",passes,failures); return failures?1:0;
}
