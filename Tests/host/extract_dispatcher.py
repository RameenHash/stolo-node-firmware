#!/usr/bin/env python3
"""Compile exact production functions: no copied/hand-edited parser fixture."""
from pathlib import Path
import re
source = Path('RNode_Firmware.ino').read_text()
names = ('stolo_parser_abort', 'stolo_buffer_byte', 'serial_callback')
parts = []
for name in names:
    match = re.search(r'^void ' + name + r'\([^\n]*\) \{\n.*?^\}', source, re.M | re.S)
    if not match:
        raise SystemExit(f'missing production function: {name}')
    parts.append(match.group())
Path('build/host/dispatcher.h').write_text('// Generated from RNode_Firmware.ino; do not edit.\n'+'\n\n'.join(parts)+'\n')

source = Path('Utilities.h').read_text()
parts = []
for name in ('eeprom_update', 'wr_conf_save', 'bt_conf_save'):
    match = re.search(r'^bool ' + name + r'\([^\n]*\) \{\n.*?^\}', source, re.M | re.S)
    if not match:
        raise SystemExit(f'missing production helper: {name}')
    parts.append(match.group())
Path('build/host/persistence.h').write_text('\n\n'.join(parts)+'\n')

source = Path('RNode_Firmware.ino').read_text()
for name, output in [('stolo_transport_drain', 'transport.h'), ('stolo_usb_event', 'usb_events.h')]:
    match = re.search(r'^void ' + name + r'\([^\n]*\) \{\n.*?^\}', source, re.M | re.S)
    if not match:
        raise SystemExit(f'missing production hook: {name}')
    Path('build/host/' + output).write_text(match.group()+'\n')

# Compile real GATT setup, queue and notification code against a fake stack.
source = Path('BLESerial.h').read_text()
fifo = re.search(r'template <size_t n>\nclass BLEFIFO \{.*?^\};', source, re.M | re.S)
if not fifo:
    raise SystemExit('missing production BLEFIFO')
Path("build/host/ble_fifo.h").write_text(fifo.group()+"\n")
parts = []
source = Path('BLESerial.cpp').read_text()
for name in ('SetupControlService', 'resetControl', 'controlGeneration',
             'readControl', 'writeControl', 'onWrite', 'onConnect', 'onDisconnect',
             'startAdvertising'):
    match = re.search(r'^(?:void|int|uint32_t) BLESerial::' + name + r'\([^\n]*\) \{\n.*?^\}', source, re.M | re.S)
    if not match:
        raise SystemExit(f'missing production BLE method: {name}')
    parts.append(match.group())
Path('build/host/ble_control.h').write_text('\n\n'.join(parts)+'\n')

# Trace queue and USB drain, compiled with a fake serial sink.
source = Path('Bluetooth.h').read_text()
start = source.index('      struct StoloBleTraceRecord {')
end = source.index('\n    #endif', start)
Path('build/host/ble_trace.h').write_text(source[start:end] + '\n')

# Real ESP32 BLE callbacks; do not substitute Classic BT/NRF variants.
source = source[source.index('    bool bt_setup_hw(); void bt_security_setup();'):]
parts = []
for name in ('stolo_bt_pairing_window_open', 'bt_passkey_notify_callback',
             'bt_security_request_callback', 'bt_authentication_complete_callback',
             'bt_connect_callback', 'bt_disconnect_callback'):
    match = re.search(r'^    (?:void|bool) ' + name + r'\([^\n]*\) \{\n.*?^    \}', source, re.M | re.S)
    if not match:
        # The window helper lives one preprocessor block deeper.
        match = re.search(r'^      bool ' + name + r'\([^\n]*\) \{\n.*?^      \}', source, re.M | re.S)
    if not match:
        raise SystemExit(f'missing production BLE callback: {name}')
    parts.append(match.group())
Path('build/host/ble_pairing.h').write_text('\n\n'.join(parts) + '\n')

source = Path('Display.h').read_text()
match = re.search(r'^void draw_pairing_pin\([^\n]*\) \{\n.*?^\}', source, re.M | re.S)
if not match:
    raise SystemExit('missing production PIN renderer')
Path('build/host/pairing_display.h').write_text(match.group() + '\n')
