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
