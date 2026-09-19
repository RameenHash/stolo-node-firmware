// Full production serial_callback, with hardware effects stubbed.
#pragma once
#define STOLO_DISPATCH_TEST 1
#include "firmware_env.h"
#define STOLO_NOTE_SOURCE(src) stolo_note_source(src)
#define STOLO_FREQ_WRITE_OK(f) stolo_kiss_freq_write(f)
#define STOLO_TXP_WRITE_OK(p) stolo_kiss_txp_write(p)
#define STOLO_MAY_MUTATE() stolo_may_configure()
#define STOLO_END_SESSION() stolo_host_disconnected()
#define STOLO_DRAIN_TRANSPORT() stolo_transport_drain()
#define MTU 508
#define CMD_L 64
#define MIN_L 1
#define CONFIG_QUEUE_SIZE 1024
#define CONFIG_QUEUE_MAX_LENGTH 8
#define DEV_SIG_LEN 64
#define DEV_HASH_LEN 32
#define CABLE_STATE_CONNECTED 1
#define CABLE_STATE_DISCONNECTED 0
#define ROM_UNLOCK_BYTE 0xF8
#define ADDR_CONF_IP 0x42
#define ADDR_CONF_NM 0x46
#define HAS_BLUETOOTH 0
#define HAS_DISPLAY 0
#define HAS_NP 0
#define BT_STATE_OFF 0
#define MODEM 3
#define SX1262 3
#define SX1280 4
uint8_t cmdbuf[CMD_L], dev_sig[DEV_SIG_LEN], dev_firmware_hash_target[DEV_HASH_LEN];
uint8_t packet_queue[CONFIG_QUEUE_SIZE];
uint16_t queue_cursor=0, current_packet_start=0, queued_bytes=0, stolo_partial_data_bytes=0;
uint8_t queue_height=0;
int cable_state=0, current_rssi=0, last_rssi=0, last_rssi_raw=0, last_snr_raw=0;
bool firmware_update_mode=false;
// FIFO behaviour matches the production ring: tests observe admission/flush.
struct FIFOBuffer { std::vector<uint8_t> bytes; } serialFIFO;
inline void fifo_flush(FIFOBuffer* f) { f->bytes.clear(); }
inline bool fifo_isfull(FIFOBuffer* f) { return f->bytes.size() >= 512; }
inline void fifo_push(FIFOBuffer* f, uint8_t b) { f->bytes.push_back(b); }
inline uint8_t fifo_pop(FIFOBuffer* f) { auto b=f->bytes.front(); f->bytes.erase(f->bytes.begin()); return b; }
struct FIFOBuffer16 {} packet_starts, packet_lengths;
inline bool fifo16_isfull(FIFOBuffer16*) { return false; }
inline void fifo16_push(FIFOBuffer16*, uint16_t) {}
inline void set_implicit_length(uint8_t) { calls.push_back("set_implicit_length"); }
NOOP(kiss_indicate_implicit_length) NOOP(display_unblank) NOOP(kiss_indicate_stat_rx)
NOOP(kiss_indicate_stat_tx) NOOP(kiss_indicate_stat_rssi) NOOP(update_radio_lock)
NOOP(kiss_indicate_radio_lock) NOOP(kiss_indicate_detect) NOOP(promisc_enable)
NOOP(promisc_disable) NOOP(kiss_indicate_promisc) NOOP(kiss_indicate_ready)
NOOP(kiss_indicate_not_ready) NOOP(unlock_rom) NOOP(hard_reset) NOOP(kiss_dump_eeprom)
NOOP(kiss_dump_config) NOOP(kiss_indicate_version) NOOP(kiss_indicate_platform)
NOOP(kiss_indicate_mcu) NOOP(kiss_indicate_board) NOOP(eeprom_conf_save)
NOOP(eeprom_conf_delete) NOOP(kiss_indicate_fb) NOOP(kiss_indicate_disp)
NOOP(kiss_indicate_device_hash) NOOP(device_save_signature)
NOOP(kiss_indicate_target_fw_hash) NOOP(kiss_indicate_fw_hash)
NOOP(kiss_indicate_bootloader_hash) NOOP(kiss_indicate_partition_table_hash)
NOOP(device_save_firmware_hash)
inline void led_indicate_info(uint8_t) { calls.push_back("led_indicate_info"); }
inline uint8_t getRandom() { return 42; }
inline void kiss_indicate_random(uint8_t) {}
inline bool queue_full() { return false; }
inline void eeprom_write(uint8_t, uint8_t) { calls.push_back("eeprom_write"); }
inline void dia_conf_save(uint8_t) { calls.push_back("dia_conf_save"); }
#include "dispatcher.h"
inline void drain_input() { while (!serialFIFO.bytes.empty()) serial_callback(fifo_pop(&serialFIFO)); }
inline void kiss(uint8_t cmd, const std::vector<uint8_t>& payload) {
  serial_callback(FEND); serial_callback(cmd);
  for (auto b:payload) { if (b==FEND) { serial_callback(FESC); serial_callback(TFEND); } else if (b==FESC) { serial_callback(FESC); serial_callback(TFESC); } else serial_callback(b); }
  serial_callback(FEND);
}
