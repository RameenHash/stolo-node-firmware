#!/usr/bin/env python3
# Copyright (C) 2026, Stolo Systems Inc.
# Part of Stolo Node Firmware, a fork of RNode Firmware
# (Copyright (C) 2024, Mark Qvist). GNU GPL v3 or later; see LICENSE.
"""stolo-node-tool: the Stolo Control Protocol (SCP) over a serial port.

The host half of StoloProtocol.h. Speaks SCP inside KISS command 0x7A to a
Stolo Node over USB. Owns the port while it runs — RNS must not be attached
to the same port at the same time (one process owns a serial port).

    stolo_node_tool.py keygen owner.key
    stolo_node_tool.py hello
    stolo_node_tool.py enroll-owner --key owner.key
    stolo_node_tool.py enroll-owner --key new-owner.key --handover-from owner.key
    stolo_node_tool.py get-radio
    stolo_node_tool.py set-radio --key owner.key --frequency 915000000 --txpower 17
    stolo_node_tool.py get-wifi --key owner.key      (sensitive reads need the owner on an owned node)
    stolo_node_tool.py set-wifi --key owner.key --mode ap --ssid "Stolo Node" --psk secret --channel 6
    stolo_node_tool.py get-bt
    stolo_node_tool.py set-bt --key owner.key --pairing on --window 120
    stolo_node_tool.py faults
    stolo_node_tool.py forget-owner --key owner.key

Port: --port, else $STOLO_PORT, else /dev/cu.usbmodem2101.
"""
import argparse
import json
import os
import struct
import sys
import time

import serial
from cryptography.hazmat.primitives.asymmetric import ed25519

FEND, FESC, TFEND, TFESC = 0xC0, 0xDB, 0xDC, 0xDD
CMD_STOLO = 0x7A
SCP_VERSION = 0x02
REPLY = 0x80
T = dict(HELLO=0x01, AUTH=0x02, ENROLL=0x03, FORGET_OWNER=0x04, GET_RADIO=0x10, SET_RADIO=0x11,
         GET_WIFI=0x20, SET_WIFI=0x21, GET_BT=0x30, SET_BT=0x31, GET_FAULTS=0x40, ERROR=0x7F)
ERRORS = {1: "bad request", 2: "unauthorized", 3: "out of band", 4: "not supported", 5: "store failed", 6: "enrollment closed",
          7: "store unreadable: physical recovery required"}
WIFI_MODES = {"off": 0, "sta": 1, "ap": 2}
BT_STATES = {0: "off", 1: "on", 2: "pairing", 3: "connected", 0xFF: "n/a"}


class SCPError(Exception):
    pass


def escape(data):
    out = bytearray()
    for b in data:
        if b == FEND: out += bytes([FESC, TFEND])
        elif b == FESC: out += bytes([FESC, TFESC])
        else: out.append(b)
    return bytes(out)


def tlv(tag, value):
    return bytes([tag, len(value)]) + value


class Node:
    def __init__(self, port, timeout=3.0):
        self.ser = serial.Serial(port, 115200, timeout=0.1)
        self.timeout = timeout
        self.seq = 0
        self.buf = bytearray()
        self.source = None

    def close(self):
        self.ser.close()

    def send(self, mtype, body=b""):
        self.seq = (self.seq + 1) & 0xFF
        payload = bytes([SCP_VERSION, mtype, self.seq]) + body
        self.ser.write(bytes([FEND, CMD_STOLO]) + escape(payload) + bytes([FEND]))
        return self.seq

    def frames(self, deadline):
        """Yield unescaped KISS frames (command byte first) until the deadline."""
        while time.time() < deadline:
            chunk = self.ser.read(256)
            if chunk:
                self.buf += chunk
            while True:
                try:
                    start = self.buf.index(FEND)
                except ValueError:
                    self.buf.clear(); break
                try:
                    end = self.buf.index(FEND, start + 1)
                except ValueError:
                    del self.buf[:start]; break
                raw = bytes(self.buf[start + 1:end]); del self.buf[:end]
                if not raw:
                    continue
                out, esc = bytearray(), False
                for b in raw:
                    if esc: out.append(FEND if b == TFEND else FESC if b == TFESC else b); esc = False
                    elif b == FESC: esc = True
                    else: out.append(b)
                yield bytes(out)

    def request(self, mtype, body=b""):
        seq = self.send(mtype, body)
        for frame in self.frames(time.time() + self.timeout):
            if frame[0] != CMD_STOLO or len(frame) < 4:
                continue
            ver, rtype, rseq, rbody = frame[1], frame[2], frame[3], frame[4:]
            if rseq != seq:
                continue
            if rtype == T["ERROR"]:
                raise SCPError(f"{ERRORS.get(rbody[0], rbody[0])}: {rbody[1:].decode('utf-8', 'replace')}")
            if rtype == (mtype | REPLY):
                return rbody
        raise SCPError(f"no reply to 0x{mtype:02x} within {self.timeout:g} s")

    # ── messages ────────────────────────────────────────────────────
    def hello(self):
        b = self.request(T["HELLO"])
        n = b[0]; fw = b[1:1 + n].decode(); i = 1 + n
        maj, mn = b[i], b[i + 1]; i += 2
        node_pub = b[i:i + 32]; i += 32
        flags = b[i]; attest = b[i + 1]; i += 2
        epoch = struct.unpack(">I", b[i:i + 4])[0]; i += 4
        nonce = b[i:i + 16]; i += 16
        source = b[i] if i < len(b) else None
        self.node_pub, self.nonce, self.epoch = node_pub, nonce, epoch
        self.source = source
        return dict(stolo_fw=fw, rnode_fw=f"{maj}.{mn}", node_pub=node_pub.hex(),
                    owner_enrolled=bool(flags & 1), authorized=bool(flags & 2),
                    enroll_window_open=bool(flags & 4), store_ok=bool(flags & 8),
                    compat_mode=bool(flags & 16),
                    attestation=attest, owner_epoch=epoch, nonce=nonce.hex(),
                    source={0: "usb", 1: "ble", 2: "wifi"}.get(source, source))

    def auth(self, key):
        # v2 transcript: domain || node_pub || nonce || epoch || source. node_pub stops a
        # challenge for node A being answered for node B under the same key; source binds
        # the answer to the carrier it was issued on.
        self.hello()
        msg = (b"stolo-auth-v2" + self.node_pub + self.nonce + struct.pack(">I", self.epoch)
               + bytes([self.source if self.source is not None else 0]))
        b = self.request(T["AUTH"], key.sign(msg))
        if not b[0]:
            raise SCPError("authentication refused (wrong owner key?)")
        # A successful AUTH issues a fresh nonce so a following ENROLL (hand-over) needs no HELLO.
        if len(b) >= 18:
            self.nonce = b[2:18]
        return True

    def enroll(self, key):
        # No HELLO here unless the node key is still unknown: HELLO clears
        # authorization, and a hand-over is an ENROLL sent from the session
        # the current owner just authenticated (whose AUTH reply carried the nonce).
        if getattr(self, "node_pub", None) is None:
            self.hello()
        owner_pub = key.public_key().public_bytes_raw()
        # v2: the proof is fresh — it signs the session nonce and the current epoch, so a
        # captured proof cannot be replayed when enrollment reopens.
        proof = key.sign(b"stolo-enroll-v2" + self.node_pub + owner_pub + self.nonce + struct.pack(">I", self.epoch))
        b = self.request(T["ENROLL"], owner_pub + proof)
        return dict(ok=bool(b[0]), owner_epoch=struct.unpack(">I", b[1:5])[0])

    def forget_owner(self):
        return dict(ok=bool(self.request(T["FORGET_OWNER"])[0]))

    @staticmethod
    def _radio(b):
        f, bw = struct.unpack(">II", b[:8]); sf, cr, txp = b[8], b[9], b[10]
        st, lt = struct.unpack(">HH", b[11:15]); state, model = b[15], b[16]
        lo, hi = struct.unpack(">II", b[17:25]); maxp = b[25]
        return dict(frequency=f, bandwidth=bw, spreadingfactor=sf, codingrate=cr, txpower=txp,
                    airtime_limit_short=st / 100.0, airtime_limit_long=lt / 100.0, radio_on=bool(state),
                    model=f"0x{model:02x}", band=None if lo == 0 else dict(low_hz=lo, high_hz=hi, max_txpower_dbm=maxp))

    def get_radio(self):
        return self._radio(self.request(T["GET_RADIO"]))

    def set_radio(self, **kw):
        body = b""
        if kw.get("frequency") is not None: body += tlv(1, struct.pack(">I", kw["frequency"]))
        if kw.get("bandwidth") is not None: body += tlv(2, struct.pack(">I", kw["bandwidth"]))
        if kw.get("sf") is not None: body += tlv(3, bytes([kw["sf"]]))
        if kw.get("cr") is not None: body += tlv(4, bytes([kw["cr"]]))
        if kw.get("txpower") is not None: body += tlv(5, bytes([kw["txpower"]]))
        if kw.get("airtime_short") is not None: body += tlv(6, struct.pack(">H", int(round(kw["airtime_short"] * 100))))
        if kw.get("airtime_long") is not None: body += tlv(7, struct.pack(">H", int(round(kw["airtime_long"] * 100))))
        if kw.get("state") is not None: body += tlv(8, bytes([1 if kw["state"] else 0]))
        return self._radio(self.request(T["SET_RADIO"], body))

    @staticmethod
    def _wifi(b):
        if not b[0]: return dict(supported=False)
        b = b[1:]
        mode, chn, n = b[0], b[1], b[2]; ssid = b[3:3 + n].decode("utf-8", "replace"); i = 3 + n
        psk_set, state = b[i], b[i + 1]; ip = struct.unpack(">I", b[i + 2:i + 6])[0]
        return dict(supported=True, mode={0: "off", 1: "sta", 2: "ap"}.get(mode, mode), channel=chn, ssid=ssid,
                    psk_set=bool(psk_set), state={0: "off", 1: "on", 2: "connected"}.get(state, state),
                    ip=".".join(str((ip >> s) & 0xFF) for s in (0, 8, 16, 24)))

    def get_wifi(self):
        return self._wifi(self.request(T["GET_WIFI"]))

    def set_wifi(self, mode=None, ssid=None, psk=None, channel=None):
        body = b""
        if mode is not None: body += tlv(1, bytes([WIFI_MODES[mode]]))
        if ssid is not None: body += tlv(2, ssid.encode("utf-8"))
        if psk is not None: body += tlv(3, psk.encode("utf-8"))
        if channel is not None: body += tlv(4, bytes([channel]))
        return self._wifi(self.request(T["SET_WIFI"], body))

    @staticmethod
    def _bt(b):
        if not b[0]: return dict(supported=False)
        b = b[1:]
        return dict(supported=True, state=BT_STATES.get(b[0], b[0]), bonds=b[1], pairing_open=bool(b[2]),
                    window_s=struct.unpack(">H", b[3:5])[0], enabled=bool(b[5]))

    def get_bt(self):
        return self._bt(self.request(T["GET_BT"]))

    def set_bt(self, pairing=None, debond=False, window=None, enabled=None):
        body = b""
        if pairing is not None: body += tlv(1, bytes([1 if pairing else 0]))
        if debond: body += tlv(2, b"\x01")
        if window is not None: body += tlv(3, struct.pack(">H", window))
        if enabled is not None: body += tlv(4, bytes([1 if enabled else 0]))
        return self._bt(self.request(T["SET_BT"], body))

    def faults(self):
        b = self.request(T["GET_FAULTS"])
        names = {1: "power-on", 3: "software", 4: "panic", 5: "int-wdt", 6: "task-wdt", 7: "wdt", 8: "deep-sleep", 9: "brownout", 10: "sdio"}
        return [names.get(x, x) for x in b[1:1 + b[0]]]


def load_key(path):
    seed = bytes.fromhex(open(path).read().strip())
    return ed25519.Ed25519PrivateKey.from_private_bytes(seed)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default=os.environ.get("STOLO_PORT", "/dev/cu.usbmodem2101"))
    ap.add_argument("--key", help="owner key file (from keygen) for anything that changes the node")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--handover-from", metavar="KEYFILE",
                    help="enroll-owner: authenticate with the CURRENT owner key first (hand-over on an owned node)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("keygen").add_argument("file")
    for name in ("hello", "get-radio", "get-wifi", "get-bt", "faults", "enroll-owner", "forget-owner"):
        sub.add_parser(name)
    r = sub.add_parser("set-radio")
    r.add_argument("--frequency", type=int); r.add_argument("--bandwidth", type=int); r.add_argument("--sf", type=int)
    r.add_argument("--cr", type=int); r.add_argument("--txpower", type=int)
    r.add_argument("--airtime-short", type=float); r.add_argument("--airtime-long", type=float)
    r.add_argument("--state", choices=["on", "off"])
    w = sub.add_parser("set-wifi")
    w.add_argument("--mode", choices=list(WIFI_MODES)); w.add_argument("--ssid"); w.add_argument("--psk"); w.add_argument("--channel", type=int)
    b = sub.add_parser("set-bt")
    b.add_argument("--pairing", choices=["on", "off"]); b.add_argument("--debond", action="store_true")
    b.add_argument("--window", type=int); b.add_argument("--enabled", choices=["on", "off"])
    a = ap.parse_args(argv)

    if a.cmd == "keygen":
        key = ed25519.Ed25519PrivateKey.generate()
        with open(a.file, "w") as f:
            f.write(key.private_bytes_raw().hex() + "\n")
        os.chmod(a.file, 0o600)
        out = dict(file=a.file, owner_pub=key.public_key().public_bytes_raw().hex())
    else:
        node = Node(a.port)
        try:
            needs_key = a.cmd in ("enroll-owner", "forget-owner", "set-radio", "set-wifi", "set-bt")
            key = load_key(a.key) if a.key else None
            if needs_key and key is None:
                ap.error(f"{a.cmd} needs --key")
            if a.cmd == "hello": out = node.hello()
            elif a.cmd == "enroll-owner":
                if a.handover_from:
                    node.auth(load_key(a.handover_from))
                out = node.enroll(key)
            elif a.cmd == "get-radio": out = node.get_radio()
            elif a.cmd in ("get-wifi", "get-bt"):
                # Sensitive reads: owner (or an unowned node in compat mode) only.
                if key is not None:
                    node.auth(key)
                out = node.get_wifi() if a.cmd == "get-wifi" else node.get_bt()
            elif a.cmd == "faults": out = dict(reset_reasons=node.faults())
            else:
                node.auth(key)
                if a.cmd == "forget-owner": out = node.forget_owner()
                elif a.cmd == "set-radio":
                    out = node.set_radio(frequency=a.frequency, bandwidth=a.bandwidth, sf=a.sf, cr=a.cr, txpower=a.txpower,
                                         airtime_short=a.airtime_short, airtime_long=a.airtime_long,
                                         state=None if a.state is None else a.state == "on")
                elif a.cmd == "set-wifi": out = node.set_wifi(a.mode, a.ssid, a.psk, a.channel)
                elif a.cmd == "set-bt":
                    out = node.set_bt(None if a.pairing is None else a.pairing == "on", a.debond, a.window,
                                      None if a.enabled is None else a.enabled == "on")
        except SCPError as e:
            print(f"error: {e}", file=sys.stderr)
            return 2
        finally:
            node.close()
    print(json.dumps(out, indent=None if a.json else 2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
