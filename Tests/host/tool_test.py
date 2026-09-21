"""Software verified: tool framing, close and accepted/pending reply decoding."""
import importlib.util
from pathlib import Path
import sys
import types
import unittest
# No pip dependencies are required for host tests; cryptography is not exercised.
sys.modules['serial'] = types.SimpleNamespace(SerialException=OSError)
for module in ['cryptography', 'cryptography.hazmat', 'cryptography.hazmat.primitives', 'cryptography.hazmat.primitives.asymmetric']:
    sys.modules[module] = types.ModuleType(module)
sys.modules['cryptography.hazmat.primitives.asymmetric'].ed25519 = types.SimpleNamespace()
spec = importlib.util.spec_from_file_location('stolo_tool', Path('Tools/stolo_node_tool.py'))
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)

class ToolTests(unittest.TestCase):
    def test_hello_display_capabilities(self):
        node = object.__new__(tool.Node)
        for caps in [0, 3]:
            body = bytes([1])+b'x'+bytes([1, 86])+bytes(32)+bytes([8, caps])+bytes(4)+bytes(16)+bytes([0])
            node.request = lambda *_: body
            reply = node.hello()
            self.assertEqual(reply['capabilities'], caps)
            self.assertEqual(reply['display_present'], bool(caps & 1))
            self.assertEqual(reply['enroll_window_display'], bool(caps & 2))
            self.assertNotIn('attestation', reply)

    def test_close_leave_flush_release(self):
        calls = []
        class Serial:
            is_open = True
            def write(self, data): calls.append(data)
            def flush(self): calls.append('flush')
            def close(self): calls.append('close')
        node = object.__new__(tool.Node); node.ser = Serial(); node.close()
        self.assertEqual(calls, [bytes([tool.FEND, tool.CMD_LEAVE, 0xff, tool.FEND]), 'flush', 'close'])
    def test_close_after_device_loss(self):
        calls = []
        class Serial:
            is_open = True
            def write(self, data): raise OSError('disconnected')
            def close(self): calls.append('close')
        node = object.__new__(tool.Node); node.ser = Serial(); node.close()
        self.assertEqual(calls, ['close'])
    def test_set_reply_pending(self):
        node = object.__new__(tool.Node)
        node.request = lambda *_: bytes([1, 2, 6, 3])+b'NEW'+bytes([1, 1, 0, 0, 0, 0, 14, 1])
        self.assertEqual(node.set_wifi(ssid='NEW')['accepted_mask'], 14)
        self.assertTrue(node.set_wifi(ssid='NEW')['runtime_pending'])
        node.request = lambda *_: bytes([1, 3, 1, 0, 0, 60, 0, 8, 1])
        reply = node.set_bt(enabled=False)
        self.assertFalse(reply['enabled']); self.assertTrue(reply['runtime_pending']); self.assertEqual(reply['accepted_mask'], 8)
    def test_rescue_explicit_token_and_new_key(self):
        node = object.__new__(tool.Node); calls=[]
        node.hello = lambda: calls.append('hello')
        def request(t, b):
            calls.append((t, b)); return bytes([1])+b'\x11'*32
        node.request=request
        reply=node.rescue()
        self.assertEqual(calls,['hello',(tool.T['RESCUE'],b'RESCUE')])
        self.assertTrue(reply['identity_replaced']); self.assertFalse(reply['owner_enrolled'])

unittest.main()
