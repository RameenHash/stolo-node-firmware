# Copyright (C) 2024, Mark Qvist
# Modified by Stolo Systems Inc., 2026-09-19 — OLED host checks.

# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

# Version 2.0.17 of the Arduino ESP core is based on ESP-IDF v4.4.7
ARDUINO_ESP_CORE_VER = 2.0.17

# Version 3.2.0 of the Arduino ESP core is based on ESP-IDF v5.4.1
# ARDUINO_ESP_CORE_VER = 3.2.0

all: release

clean:
	-rm -r ./build
	-rm ./Release/rnode_firmware*

prep: prep-avr prep-esp32 prep-samd

prep-avr:
	arduino-cli core update-index --config-file arduino-cli.yaml
	arduino-cli core install arduino:avr --config-file arduino-cli.yaml

prep-esp32:
	arduino-cli core update-index --config-file arduino-cli.yaml
	arduino-cli core install esp32:esp32@$(ARDUINO_ESP_CORE_VER) --config-file arduino-cli.yaml
	arduino-cli lib install --no-deps "Adafruit BusIO@1.17.4" "Adafruit GFX Library@1.12.6" "Adafruit seesaw Library@1.7.9"
	arduino-cli lib install --no-deps "Adafruit SSD1306@2.5.17"
	arduino-cli lib install --no-deps "Adafruit SH110X@2.1.15"
	arduino-cli lib install --no-deps "Adafruit ST7735 and ST7789 Library@1.11.0"
	arduino-cli lib install --no-deps "Adafruit NeoPixel@1.15.5"
	arduino-cli lib install --no-deps "XPowersLib@0.3.3"
	arduino-cli lib install --no-deps "Crypto@0.4.0"

prep-samd:
	arduino-cli core update-index --config-file arduino-cli.yaml
	arduino-cli core install adafruit:samd --config-file arduino-cli.yaml

prep-nrf:
	arduino-cli core update-index --config-file arduino-cli.yaml
	arduino-cli core install rakwireless:nrf52 --config-file arduino-cli.yaml
	arduino-cli core install Heltec_nRF52:Heltec_nRF52 --config-file arduino-cli.yaml
	arduino-cli core install adafruit:nrf52 --config-file arduino-cli.yaml
	arduino-cli lib install --no-deps "Adafruit BusIO@1.17.4" "Adafruit GFX Library@1.12.6"
	arduino-cli lib install --no-deps "GxEPD2@1.6.9"
	arduino-cli config set library.enable_unsafe_install true
	arduino-cli lib install --no-deps --git-url https://github.com/liamcottle/esp8266-oled-ssd1306#e16cee124fe26490cb14880c679321ad8ac89c95
	pip install adafruit-nrfutil --upgrade

console-site:
	make -C Console clean site

spiffs: console-site spiffs-image 

spiffs-image:
	python Release/esptool/spiffsgen.py 1966080 ./Console/build Release/console_image.bin

upload-spiffs:
	@echo Deploying SPIFFS image...
	python ./Release/esptool/esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x210000 ./Release/console_image.bin

check_bt_buffers:
	@./esp32_btbufs.py ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/libraries/BluetoothSerial/src/BluetoothSerial.cpp

firmware:
	arduino-cli compile --log --fqbn unsignedio:avr:rnode

firmware-mega2560:
	arduino-cli compile --log --fqbn arduino:avr:mega

firmware-tbeam: check_bt_buffers
	arduino-cli compile --log --fqbn esp32:esp32:t-beam -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x33\""

firmware-tbeam_sx126x: check_bt_buffers
	arduino-cli compile --log --fqbn esp32:esp32:t-beam -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x33\" \"-DMODEM=0x03\""

firmware-t3s3:
	arduino-cli compile --log --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc" -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x42\" \"-DMODEM=0x03\""

firmware-t3s3_sx127x:
	arduino-cli compile --log --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc" -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x42\" \"-DMODEM=0x01\""

firmware-t3s3_sx1280_pa:
	arduino-cli compile --log --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc" -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x42\" \"-DMODEM=0x04\""

firmware-tdeck:
	arduino-cli compile --log --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc" -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x3B\""

firmware-tbeam_supreme:
	arduino-cli compile --log --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc" -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=-DBOARD_MODEL=0x3D"

firmware-lora32_v10: check_bt_buffers
	arduino-cli compile --log --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x39\""

firmware-lora32_v10_extled: check_bt_buffers
	arduino-cli compile --log --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x39\" \"-DEXTERNAL_LEDS=true\""

firmware-lora32_v20: check_bt_buffers
	arduino-cli compile --log --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x36\" \"-DEXTERNAL_LEDS=true\""

firmware-lora32_v21: check_bt_buffers
	arduino-cli compile --log --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x37\""

firmware-lora32_v21_extled: check_bt_buffers
	arduino-cli compile --log --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x37\" \"-DEXTERNAL_LEDS=true\""

firmware-lora32_v21_tcxo: check_bt_buffers
	arduino-cli compile --log --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x37\" \"-DENABLE_TCXO=true\""

firmware-heltec32_v2: check_bt_buffers
	arduino-cli compile --log --fqbn esp32:esp32:heltec_wifi_lora_32_V2 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x38\""

firmware-heltec32_v2_extled: check_bt_buffers
	arduino-cli compile --log --fqbn esp32:esp32:heltec_wifi_lora_32_V2 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x38\" \"-DEXTERNAL_LEDS=true\""

firmware-heltec32_v3:
	arduino-cli compile --log --fqbn esp32:esp32:heltec_wifi_lora_32_V3 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x3A\""

firmware-heltec32_v4:
	arduino-cli compile --log --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc" -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x3F\""

firmware-rnode_ng_20: check_bt_buffers
	arduino-cli compile --log --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x40\""

firmware-rnode_ng_21: check_bt_buffers
	arduino-cli compile --log --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x41\""

firmware-featheresp32: check_bt_buffers
	arduino-cli compile --log --fqbn esp32:esp32:featheresp32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x34\""

firmware-genericesp32: check_bt_buffers
	arduino-cli compile --log --fqbn esp32:esp32:esp32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x35\""

firmware-rak4631:
	arduino-cli compile --log --fqbn rakwireless:nrf52:WisCoreRAK4631Board -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x51\""

firmware-heltec_t114:
	arduino-cli compile --log --fqbn Heltec_nRF52:Heltec_nRF52:HT-n5262 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x3C\""

firmware-techo:
	arduino-cli compile --log --fqbn adafruit:nrf52:pca10056 -e --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x44\""

firmware-xiao_s3:
	arduino-cli compile --log --fqbn "esp32:esp32:XIAO_ESP32S3" -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x3E\""

upload:
	arduino-cli upload -p /dev/ttyUSB0 --fqbn unsignedio:avr:rnode

upload-mega2560:
	arduino-cli upload -p /dev/ttyACM0 --fqbn arduino:avr:mega

upload-tbeam:
	arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:t-beam
	@sleep 1
	rnodeconf /dev/ttyUSB0 --firmware-hash $$(./partition_hashes ./build/esp32.esp32.t-beam/RNode_Firmware.ino.bin)
	@sleep 3
	python ./Release/esptool/esptool.py --chip esp32 --port /dev/ttyACM0 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x210000 ./Release/console_image.bin

upload-tbeam_sx1262:
	arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:t-beam
	@sleep 1
	rnodeconf /dev/ttyACM0 --firmware-hash $$(./partition_hashes ./build/esp32.esp32.t-beam/RNode_Firmware.ino.bin)
	@sleep 3
	python ./Release/esptool/esptool.py --chip esp32 --port /dev/ttyACM0 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x210000 ./Release/console_image.bin

upload-lora32_v10:
	arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:ttgo-lora32
	@sleep 1
	rnodeconf /dev/ttyUSB0 --firmware-hash $$(./partition_hashes ./build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bin)
	@sleep 3
	python ./Release/esptool/esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x210000 ./Release/console_image.bin

upload-lora32_v20:
	arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:ttgo-lora32
	@sleep 1
	rnodeconf /dev/ttyUSB0 --firmware-hash $$(./partition_hashes ./build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bin)
	@sleep 3
	python ./Release/esptool/esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x210000 ./Release/console_image.bin

upload-lora32_v21:
	arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:ttgo-lora32
	@sleep 1
	rnodeconf /dev/ttyACM0 --firmware-hash $$(./partition_hashes ./build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bin)
	@sleep 3
	python ./Release/esptool/esptool.py --chip esp32 --port /dev/ttyACM0 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x210000 ./Release/console_image.bin

upload-heltec32_v2:
	arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:heltec_wifi_lora_32_V2
	@sleep 1
	rnodeconf /dev/ttyUSB0 --firmware-hash $$(./partition_hashes ./build/esp32.esp32.heltec_wifi_lora_32_V2/RNode_Firmware.ino.bin)
	@sleep 3
	python ./Release/esptool/esptool.py --chip esp32 --port /dev/ttyUSB1 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x210000 ./Release/console_image.bin

upload-heltec32_v3:
	arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:heltec_wifi_lora_32_V3
	@sleep 1
	rnodeconf /dev/ttyUSB0 --firmware-hash $$(./partition_hashes ./build/esp32.esp32.heltec_wifi_lora_32_V3/RNode_Firmware.ino.bin)
	@sleep 3
	python ./Release/esptool/esptool.py --chip esp32-s3 --port /dev/ttyUSB0 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x210000 ./Release/console_image.bin

upload-heltec32_v4:
	arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:esp32s3
	@sleep 1
	rnodeconf /dev/ttyACM0 --firmware-hash $$(./partition_hashes ./build/esp32.esp32.esp32s3/RNode_Firmware.ino.bin)
	@sleep 3
	python ./Release/esptool/esptool.py --chip esp32-s3 --port /dev/ttyACM0 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x210000 ./Release/console_image.bin

upload-tdeck:
	arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:esp32s3
	@sleep 1
	rnodeconf /dev/ttyACM0 --firmware-hash $$(./partition_hashes ./build/esp32.esp32.esp32s3/RNode_Firmware.ino.bin)
	@sleep 3
	python ./Release/esptool/esptool.py --chip esp32-s3 --port /dev/ttyACM0 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x210000 ./Release/console_image.bin

upload-tbeam_supreme:
	arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:esp32s3
	@sleep 1
	rnodeconf /dev/ttyACM0 --firmware-hash $$(./partition_hashes ./build/esp32.esp32.esp32s3/RNode_Firmware.ino.bin)
	@sleep 3
	python ./Release/esptool/esptool.py --chip esp32-s3 --port /dev/ttyACM0 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x210000 ./Release/console_image.bin

upload-rnode_ng_20:
	arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:ttgo-lora32
	@sleep 1
	rnodeconf /dev/ttyUSB0 --firmware-hash $$(./partition_hashes ./build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bin)
	@sleep 3
	python ./Release/esptool/esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x210000 ./Release/console_image.bin

upload-rnode_ng_21:
	arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:ttgo-lora32
	@sleep 1
	rnodeconf /dev/ttyACM0 --firmware-hash $$(./partition_hashes ./build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bin)
	@sleep 3
	python ./Release/esptool/esptool.py --chip esp32 --port /dev/ttyACM0 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x210000 ./Release/console_image.bin

upload-t3s3:
	arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:esp32s3
	@sleep 1
	rnodeconf /dev/ttyACM0 --firmware-hash $$(./partition_hashes ./build/esp32.esp32.esp32s3/RNode_Firmware.ino.bin)
	@sleep 3
	python ./Release/esptool/esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x210000 ./Release/console_image.bin

upload-featheresp32:
	arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:featheresp32
	@sleep 1
	rnodeconf /dev/ttyUSB0 --firmware-hash $$(./partition_hashes ./build/esp32.esp32.featheresp32/RNode_Firmware.ino.bin)
	@sleep 3
	python ./Release/esptool/esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x210000 ./Release/console_image.bin

upload-rak4631:
	arduino-cli upload -p /dev/ttyACM0 --fqbn rakwireless:nrf52:WisCoreRAK4631Board
	@sleep 1
	rnodeconf /dev/ttyACM0 --firmware-hash $$(./partition_hashes from_device /dev/ttyACM0)

upload-heltec_t114:
	arduino-cli upload -p /dev/ttyACM0 --fqbn Heltec_nRF52:Heltec_nRF52:HT-n5262
	@sleep 1
	rnodeconf /dev/ttyACM0 --firmware-hash $$(./partition_hashes from_device /dev/ttyACM0)

upload-techo:
	arduino-cli upload -p /dev/ttyACM0 --fqbn adafruit:nrf52:pca10056
	@sleep 6
	rnodeconf /dev/ttyACM0 --firmware-hash $$(./partition_hashes from_device /dev/ttyACM0)

upload-xiao_s3:
	arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:XIAO_ESP32S3
	@sleep 1
	rnodeconf /dev/ttyACM0 --firmware-hash $$(./partition_hashes ./build/esp32.esp32.XIAO_ESP32S3/RNode_Firmware.ino.bin)
	@sleep 3
	python ./Release/esptool/esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x210000 ./Release/console_image.bin

# ---------------------------------------------------------------------------
# Stolo additions (Modified by Stolo Systems Inc., 2026-09-14). The upstream
# upload-* recipes assume Linux (/dev/ttyACM0, `python`, ~/.arduino15). This
# is the same sequence for the bench Mac: flash the application, then write
# the firmware-hash target the EEPROM signature check compares against. The
# console partition (0x210000) is deliberately NOT rewritten here — the unit
# keeps the console image it shipped with until F9 regenerates it from this
# fork's source.
#
#   make flash-stolo-tbeam_supreme PORT=/dev/cu.usbmodem2101
STOLO_PY ?= python3
STOLO_RNODECONF ?= rnodeconf
PORT ?= /dev/cu.usbmodem2101

# The Stolo build of the T-Beam Supreme target: upstream's recipe plus
# -DSTOLO_BUILD=1, which compiles in everything Stolo.h and the
# STOLO_BUILD blocks change. Same build directory, so flash-stolo-* flashes
# whichever of the two was built last.
firmware-stolo-tbeam_supreme:
	arduino-cli compile --log --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc" -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=-DBOARD_MODEL=0x3D -DSTOLO_BUILD=1"

# Bench diagnostics only: plaintext USB output is not a KISS/SCP stream.
firmware-stolo-tbeam_supreme-ble-trace:
	arduino-cli compile --log --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc" -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=-DBOARD_MODEL=0x3D -DSTOLO_BUILD=1 -DSTOLO_BLE_TRACE=1"

flash-stolo-tbeam_supreme:
	arduino-cli upload -p $(PORT) --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc" --config-file arduino-cli.yaml
	@sleep 2
	$(STOLO_RNODECONF) $(PORT) --firmware-hash $$($(STOLO_PY) ./partition_hashes ./build/esp32.esp32.esp32s3/RNode_Firmware.ino.bin)

release: release-all

release-all: console-site spiffs-image release-tbeam release-tbeam_sx1262 release-lora32_v10 release-lora32_v20 release-lora32_v21 release-lora32_v10_extled release-lora32_v20_extled release-lora32_v21_extled release-lora32_v21_tcxo release-featheresp32 release-genericesp32 release-heltec32_v2 release-heltec32_v3 release-heltec32_v4 release-heltec32_v2_extled release-heltec_t114 release-techo release-rnode_ng_20 release-rnode_ng_21 release-t3s3 release-t3s3_sx127x release-t3s3_sx1280_pa release-tdeck release-tbeam_supreme release-rak4631 release-xiao_s3 release-hashes

release-hashes:
	python ./release_hashes.py > ./Release/release.json

release-rnode:
	arduino-cli compile --fqbn unsignedio:avr:rnode -e
	cp build/unsignedio.avr.rnode/RNode_Firmware.ino.hex Release/rnode_firmware.hex
	rm -r build

release-tbeam: check_bt_buffers
	arduino-cli compile --fqbn esp32:esp32:t-beam -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x33\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_tbeam.boot_app0
	cp build/esp32.esp32.t-beam/RNode_Firmware.ino.bin build/rnode_firmware_tbeam.bin
	cp build/esp32.esp32.t-beam/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_tbeam.bootloader
	cp build/esp32.esp32.t-beam/RNode_Firmware.ino.partitions.bin build/rnode_firmware_tbeam.partitions
	zip --junk-paths ./Release/rnode_firmware_tbeam.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_tbeam.boot_app0 build/rnode_firmware_tbeam.bin build/rnode_firmware_tbeam.bootloader build/rnode_firmware_tbeam.partitions
	rm -r build

release-tbeam_sx1262: check_bt_buffers
	arduino-cli compile --fqbn esp32:esp32:t-beam -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x33\" \"-DMODEM=0x03\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_tbeam_sx1262.boot_app0
	cp build/esp32.esp32.t-beam/RNode_Firmware.ino.bin build/rnode_firmware_tbeam_sx1262.bin
	cp build/esp32.esp32.t-beam/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_tbeam_sx1262.bootloader
	cp build/esp32.esp32.t-beam/RNode_Firmware.ino.partitions.bin build/rnode_firmware_tbeam_sx1262.partitions
	zip --junk-paths ./Release/rnode_firmware_tbeam_sx1262.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_tbeam_sx1262.boot_app0 build/rnode_firmware_tbeam_sx1262.bin build/rnode_firmware_tbeam_sx1262.bootloader build/rnode_firmware_tbeam_sx1262.partitions
	rm -r build

release-lora32_v10: check_bt_buffers
	arduino-cli compile --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x39\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_lora32v10.boot_app0
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bin build/rnode_firmware_lora32v10.bin
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_lora32v10.bootloader
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.partitions.bin build/rnode_firmware_lora32v10.partitions
	zip --junk-paths ./Release/rnode_firmware_lora32v10.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_lora32v10.boot_app0 build/rnode_firmware_lora32v10.bin build/rnode_firmware_lora32v10.bootloader build/rnode_firmware_lora32v10.partitions
	rm -r build

release-lora32_v20: check_bt_buffers
	arduino-cli compile --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x36\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_lora32v20.boot_app0
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bin build/rnode_firmware_lora32v20.bin
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_lora32v20.bootloader
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.partitions.bin build/rnode_firmware_lora32v20.partitions
	zip --junk-paths ./Release/rnode_firmware_lora32v20.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_lora32v20.boot_app0 build/rnode_firmware_lora32v20.bin build/rnode_firmware_lora32v20.bootloader build/rnode_firmware_lora32v20.partitions
	rm -r build

release-lora32_v21: check_bt_buffers
	arduino-cli compile --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x37\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_lora32v21.boot_app0
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bin build/rnode_firmware_lora32v21.bin
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_lora32v21.bootloader
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.partitions.bin build/rnode_firmware_lora32v21.partitions
	zip --junk-paths ./Release/rnode_firmware_lora32v21.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_lora32v21.boot_app0 build/rnode_firmware_lora32v21.bin build/rnode_firmware_lora32v21.bootloader build/rnode_firmware_lora32v21.partitions
	rm -r build

release-lora32_v10_extled: check_bt_buffers
	arduino-cli compile --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x39\" \"-DEXTERNAL_LEDS=true\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_lora32v10.boot_app0
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bin build/rnode_firmware_lora32v10.bin
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_lora32v10.bootloader
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.partitions.bin build/rnode_firmware_lora32v10.partitions
	zip --junk-paths ./Release/rnode_firmware_lora32v10.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_lora32v10.boot_app0 build/rnode_firmware_lora32v10.bin build/rnode_firmware_lora32v10.bootloader build/rnode_firmware_lora32v10.partitions
	rm -r build

release-lora32_v20_extled: check_bt_buffers
	arduino-cli compile --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x36\" \"-DEXTERNAL_LEDS=true\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_lora32v20.boot_app0
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bin build/rnode_firmware_lora32v20.bin
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_lora32v20.bootloader
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.partitions.bin build/rnode_firmware_lora32v20.partitions
	zip --junk-paths ./Release/rnode_firmware_lora32v20_extled.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_lora32v20.boot_app0 build/rnode_firmware_lora32v20.bin build/rnode_firmware_lora32v20.bootloader build/rnode_firmware_lora32v20.partitions
	rm -r build

release-lora32_v21_extled: check_bt_buffers
	arduino-cli compile --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x37\" \"-DEXTERNAL_LEDS=true\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_lora32v21.boot_app0
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bin build/rnode_firmware_lora32v21.bin
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_lora32v21.bootloader
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.partitions.bin build/rnode_firmware_lora32v21.partitions
	zip --junk-paths ./Release/rnode_firmware_lora32v21_extled.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_lora32v21.boot_app0 build/rnode_firmware_lora32v21.bin build/rnode_firmware_lora32v21.bootloader build/rnode_firmware_lora32v21.partitions
	rm -r build

release-lora32_v21_tcxo: check_bt_buffers
	arduino-cli compile --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x37\" \"-DENABLE_TCXO=true\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_lora32v21_tcxo.boot_app0
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bin build/rnode_firmware_lora32v21_tcxo.bin
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_lora32v21_tcxo.bootloader
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.partitions.bin build/rnode_firmware_lora32v21_tcxo.partitions
	zip --junk-paths ./Release/rnode_firmware_lora32v21_tcxo.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_lora32v21_tcxo.boot_app0 build/rnode_firmware_lora32v21_tcxo.bin build/rnode_firmware_lora32v21_tcxo.bootloader build/rnode_firmware_lora32v21_tcxo.partitions
	rm -r build

release-heltec32_v2: check_bt_buffers
	arduino-cli compile --fqbn esp32:esp32:heltec_wifi_lora_32_V2 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x38\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_heltec32v2.boot_app0
	cp build/esp32.esp32.heltec_wifi_lora_32_V2/RNode_Firmware.ino.bin build/rnode_firmware_heltec32v2.bin
	cp build/esp32.esp32.heltec_wifi_lora_32_V2/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_heltec32v2.bootloader
	cp build/esp32.esp32.heltec_wifi_lora_32_V2/RNode_Firmware.ino.partitions.bin build/rnode_firmware_heltec32v2.partitions
	zip --junk-paths ./Release/rnode_firmware_heltec32v2.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_heltec32v2.boot_app0 build/rnode_firmware_heltec32v2.bin build/rnode_firmware_heltec32v2.bootloader build/rnode_firmware_heltec32v2.partitions
	rm -r build

release-heltec32_v3: check_bt_buffers
	arduino-cli compile --fqbn esp32:esp32:heltec_wifi_lora_32_V3 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x3A\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_heltec32v3.boot_app0
	cp build/esp32.esp32.heltec_wifi_lora_32_V3/RNode_Firmware.ino.bin build/rnode_firmware_heltec32v3.bin
	cp build/esp32.esp32.heltec_wifi_lora_32_V3/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_heltec32v3.bootloader
	cp build/esp32.esp32.heltec_wifi_lora_32_V3/RNode_Firmware.ino.partitions.bin build/rnode_firmware_heltec32v3.partitions
	zip --junk-paths ./Release/rnode_firmware_heltec32v3.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_heltec32v3.boot_app0 build/rnode_firmware_heltec32v3.bin build/rnode_firmware_heltec32v3.bootloader build/rnode_firmware_heltec32v3.partitions
	rm -r build

release-heltec32_v4: check_bt_buffers
	arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc" -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x3F\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_heltec32v4pa.boot_app0
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.bin build/rnode_firmware_heltec32v4pa.bin
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_heltec32v4pa.bootloader
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.partitions.bin build/rnode_firmware_heltec32v4pa.partitions
	zip --junk-paths ./Release/rnode_firmware_heltec32v4pa.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_heltec32v4pa.boot_app0 build/rnode_firmware_heltec32v4pa.bin build/rnode_firmware_heltec32v4pa.bootloader build/rnode_firmware_heltec32v4pa.partitions
	rm -r build

release-heltec32_v2_extled: check_bt_buffers
	arduino-cli compile --fqbn esp32:esp32:heltec_wifi_lora_32_V2 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x38\" \"-DEXTERNAL_LEDS=true\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_heltec32v2.boot_app0
	cp build/esp32.esp32.heltec_wifi_lora_32_V2/RNode_Firmware.ino.bin build/rnode_firmware_heltec32v2.bin
	cp build/esp32.esp32.heltec_wifi_lora_32_V2/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_heltec32v2.bootloader
	cp build/esp32.esp32.heltec_wifi_lora_32_V2/RNode_Firmware.ino.partitions.bin build/rnode_firmware_heltec32v2.partitions
	zip --junk-paths ./Release/rnode_firmware_heltec32v2.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_heltec32v2.boot_app0 build/rnode_firmware_heltec32v2.bin build/rnode_firmware_heltec32v2.bootloader build/rnode_firmware_heltec32v2.partitions
	rm -r build

release-rnode_ng_20: check_bt_buffers
	arduino-cli compile --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x40\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_ng20.boot_app0
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bin build/rnode_firmware_ng20.bin
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_ng20.bootloader
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.partitions.bin build/rnode_firmware_ng20.partitions
	zip --junk-paths ./Release/rnode_firmware_ng20.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_ng20.boot_app0 build/rnode_firmware_ng20.bin build/rnode_firmware_ng20.bootloader build/rnode_firmware_ng20.partitions
	rm -r build

release-rnode_ng_21: check_bt_buffers
	arduino-cli compile --fqbn esp32:esp32:ttgo-lora32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x41\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_ng21.boot_app0
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bin build/rnode_firmware_ng21.bin
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_ng21.bootloader
	cp build/esp32.esp32.ttgo-lora32/RNode_Firmware.ino.partitions.bin build/rnode_firmware_ng21.partitions
	zip --junk-paths ./Release/rnode_firmware_ng21.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_ng21.boot_app0 build/rnode_firmware_ng21.bin build/rnode_firmware_ng21.bootloader build/rnode_firmware_ng21.partitions
	rm -r build

release-t3s3:
	arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc" -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x42\" \"-DMODEM=0x03\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_t3s3.boot_app0
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.bin build/rnode_firmware_t3s3.bin
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_t3s3.bootloader
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.partitions.bin build/rnode_firmware_t3s3.partitions
	zip --junk-paths ./Release/rnode_firmware_t3s3.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_t3s3.boot_app0 build/rnode_firmware_t3s3.bin build/rnode_firmware_t3s3.bootloader build/rnode_firmware_t3s3.partitions
	rm -r build

release-t3s3_sx1280_pa:
	arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc" -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x42\" \"-DMODEM=0x04\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_t3s3_sx1280_pa.boot_app0
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.bin build/rnode_firmware_t3s3_sx1280_pa.bin
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_t3s3_sx1280_pa.bootloader
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.partitions.bin build/rnode_firmware_t3s3_sx1280_pa.partitions
	zip --junk-paths ./Release/rnode_firmware_t3s3_sx1280_pa.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_t3s3_sx1280_pa.boot_app0 build/rnode_firmware_t3s3_sx1280_pa.bin build/rnode_firmware_t3s3_sx1280_pa.bootloader build/rnode_firmware_t3s3_sx1280_pa.partitions
	rm -r build

release-t3s3_sx127x:
	arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc" -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x42\" \"-DMODEM=0x01\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_t3s3_sx127x.boot_app0
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.bin build/rnode_firmware_t3s3_sx127x.bin
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_t3s3_sx127x.bootloader
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.partitions.bin build/rnode_firmware_t3s3_sx127x.partitions
	zip --junk-paths ./Release/rnode_firmware_t3s3_sx127x.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_t3s3_sx127x.boot_app0 build/rnode_firmware_t3s3_sx127x.bin build/rnode_firmware_t3s3_sx127x.bootloader build/rnode_firmware_t3s3_sx127x.partitions
	rm -r build

release-tdeck:
	arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc" -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x3B\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_tdeck.boot_app0
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.bin build/rnode_firmware_tdeck.bin
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_tdeck.bootloader
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.partitions.bin build/rnode_firmware_tdeck.partitions
	zip --junk-paths ./Release/rnode_firmware_tdeck.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_tdeck.boot_app0 build/rnode_firmware_tdeck.bin build/rnode_firmware_tdeck.bootloader build/rnode_firmware_tdeck.partitions
	rm -r build

release-tbeam_supreme:
	arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc" -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x3D\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_tbeam_supreme.boot_app0
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.bin build/rnode_firmware_tbeam_supreme.bin
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_tbeam_supreme.bootloader
	cp build/esp32.esp32.esp32s3/RNode_Firmware.ino.partitions.bin build/rnode_firmware_tbeam_supreme.partitions
	zip --junk-paths ./Release/rnode_firmware_tbeam_supreme.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_tbeam_supreme.boot_app0 build/rnode_firmware_tbeam_supreme.bin build/rnode_firmware_tbeam_supreme.bootloader build/rnode_firmware_tbeam_supreme.partitions
	rm -r build

release-featheresp32: check_bt_buffers
	arduino-cli compile --fqbn esp32:esp32:featheresp32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x34\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_featheresp32.boot_app0
	cp build/esp32.esp32.featheresp32/RNode_Firmware.ino.bin build/rnode_firmware_featheresp32.bin
	cp build/esp32.esp32.featheresp32/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_featheresp32.bootloader
	cp build/esp32.esp32.featheresp32/RNode_Firmware.ino.partitions.bin build/rnode_firmware_featheresp32.partitions
	zip --junk-paths ./Release/rnode_firmware_featheresp32.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_featheresp32.boot_app0 build/rnode_firmware_featheresp32.bin build/rnode_firmware_featheresp32.bootloader build/rnode_firmware_featheresp32.partitions
	rm -r build

release-genericesp32: check_bt_buffers
	arduino-cli compile --fqbn esp32:esp32:esp32 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x35\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_esp32_generic.boot_app0
	cp build/esp32.esp32.esp32/RNode_Firmware.ino.bin build/rnode_firmware_esp32_generic.bin
	cp build/esp32.esp32.esp32/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_esp32_generic.bootloader
	cp build/esp32.esp32.esp32/RNode_Firmware.ino.partitions.bin build/rnode_firmware_esp32_generic.partitions
	zip --junk-paths ./Release/rnode_firmware_esp32_generic.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_esp32_generic.boot_app0 build/rnode_firmware_esp32_generic.bin build/rnode_firmware_esp32_generic.bootloader build/rnode_firmware_esp32_generic.partitions
	rm -r build

release-mega2560:
	arduino-cli compile --fqbn arduino:avr:mega -e --build-property "compiler.cpp.extra_flags=\"-DMODEM=0x01\""
	cp build/arduino.avr.mega/RNode_Firmware.ino.hex Release/rnode_firmware_m2560.hex
	rm -r build

release-rak4631:
	arduino-cli compile --fqbn rakwireless:nrf52:WisCoreRAK4631Board -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x51\""
	cp build/rakwireless.nrf52.WisCoreRAK4631Board/RNode_Firmware.ino.hex build/rnode_firmware_rak4631.hex
	adafruit-nrfutil dfu genpkg --dev-type 0x0052 --application build/rnode_firmware_rak4631.hex Release/rnode_firmware_rak4631.zip

release-heltec_t114:
	arduino-cli compile --fqbn Heltec_nRF52:Heltec_nRF52:HT-n5262 -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x3C\""
	cp build/Heltec_nRF52.Heltec_nRF52.HT-n5262/RNode_Firmware.ino.hex build/rnode_firmware_heltec_t114.hex
	adafruit-nrfutil dfu genpkg --dev-type 0x0052 --application build/rnode_firmware_heltec_t114.hex Release/rnode_firmware_heltec_t114.zip

release-techo:
	arduino-cli compile --log --fqbn adafruit:nrf52:pca10056 -e --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x44\""
	cp build/adafruit.nrf52.pca10056/RNode_Firmware.ino.hex build/rnode_firmware_techo.hex
	adafruit-nrfutil dfu genpkg --dev-type 0x0052 --application build/rnode_firmware_techo.hex Release/rnode_firmware_techo.zip

release-xiao_s3:
	arduino-cli compile --fqbn "esp32:esp32:XIAO_ESP32S3" -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\"-DBOARD_MODEL=0x3E\""
	cp ~/.arduino15/packages/esp32/hardware/esp32/$(ARDUINO_ESP_CORE_VER)/tools/partitions/boot_app0.bin build/rnode_firmware_xiao_esp32s3.boot_app0
	cp build/esp32.esp32.XIAO_ESP32S3/RNode_Firmware.ino.bin build/rnode_firmware_xiao_esp32s3.bin
	cp build/esp32.esp32.XIAO_ESP32S3/RNode_Firmware.ino.bootloader.bin build/rnode_firmware_xiao_esp32s3.bootloader
	cp build/esp32.esp32.XIAO_ESP32S3/RNode_Firmware.ino.partitions.bin build/rnode_firmware_xiao_esp32s3.partitions
	zip --junk-paths ./Release/rnode_firmware_xiao_esp32s3.zip ./Release/esptool/esptool.py ./Release/console_image.bin build/rnode_firmware_xiao_esp32s3.boot_app0 build/rnode_firmware_xiao_esp32s3.bin build/rnode_firmware_xiao_esp32s3.bootloader build/rnode_firmware_xiao_esp32s3.partitions
	rm -r build
# Host tests against the REAL Stolo dispatcher and store (Tests/host):
# hardware, persistence and Ed25519 are stubs, so these prove state
# transitions and policy ("software verified"), never ESP32 behaviour
# ("device qualified", see the plan's testing queue).
test-host:
	python3 Tools/oled_bitmaps.py --check
	@mkdir -p build/host
	g++ -std=c++17 -Wall -Wno-unused-function -I. -ITests/host -ITests/host/stubs -o build/host/authority_test Tests/host/authority_test.cpp
	g++ -std=c++17 -Wall -Wno-unused-function -I. -ITests/host -ITests/host/stubs -o build/host/store_test Tests/host/store_test.cpp
	python3 Tests/host/extract_dispatcher.py
	g++ -std=c++17 -Wall -Wno-unused-function -I. -ITests/host -ITests/host/stubs -Ibuild/host -o build/host/dispatcher_test Tests/host/dispatcher_test.cpp
	g++ -std=c++17 -Wall -Wno-unused-function -I. -ITests/host -ITests/host/stubs -Ibuild/host -o build/host/corrections_test Tests/host/corrections_test.cpp
	g++ -std=c++17 -Wall -Ibuild/host -o build/host/persistence_test Tests/host/persistence_test.cpp
	g++ -std=c++17 -Wall -Wno-unused-function -I. -ITests/host -ITests/host/stubs -o build/host/compat_disabled_test Tests/host/compat_disabled_test.cpp
	g++ -std=c++17 -Wall -Wno-unused-function -DARDUINO_USB_MODE=1 -I. -ITests/host -ITests/host/stubs -Ibuild/host -o build/host/transport_hw_test Tests/host/transport_test.cpp
	g++ -std=c++17 -Wall -Wno-unused-function -DARDUINO_USB_MODE=0 -I. -ITests/host -ITests/host/stubs -Ibuild/host -o build/host/transport_tiny_test Tests/host/transport_test.cpp
	g++ -std=c++17 -Wall -Wno-unused-function -I. -ITests/host -ITests/host/stubs -Ibuild/host -o build/host/control_test Tests/host/control_test.cpp
	g++ -std=c++17 -Wall -I. -Ibuild/host -o build/host/ble_control_test Tests/host/ble_control_test.cpp
	g++ -std=c++17 -Wall -Ibuild/host -o build/host/ble_trace_test Tests/host/ble_trace_test.cpp
	g++ -std=c++17 -Wall -Wno-unused-function -DHAS_DISPLAY=1 -I. -ITests/host -ITests/host/stubs -o build/host/display_test Tests/host/display_test.cpp
	g++ -std=c++17 -Wall -Wno-unused-function -DHAS_DISPLAY=0 -I. -ITests/host -ITests/host/stubs -o build/host/display_headless_test Tests/host/display_test.cpp
	./build/host/display_test
	./build/host/display_headless_test
	./build/host/ble_control_test
	./build/host/ble_trace_test
	./build/host/control_test
	./build/host/transport_hw_test
	./build/host/transport_tiny_test
	python3 Tests/host/tool_test.py
	./build/host/persistence_test
	./build/host/compat_disabled_test
	./build/host/corrections_test
	./build/host/dispatcher_test
	./build/host/store_test
	./build/host/authority_test
