#!/usr/bin/env python3
# Copyright (C) 2026, Stolo Systems Inc.
# Part of Stolo Node Firmware; GNU GPL v3 or later. See LICENSE.
"""Read-only sustained NUS/CTRL KISS GET_RADIO benchmark via iPhone USB forwards.
No RF packets or configuration writes. The bridge's single-client sockets are
occupied during the run; app backend clients must not compete for them.
"""
import argparse, concurrent.futures, datetime, json, math, socket, sys, time


def escape(b):
    return b.replace(b'\xdb', b'\xdb\xdd').replace(b'\xc0', b'\xdb\xdc')


def run(name, port, seconds, batch, parity):
    result = dict(channel=name, requests=0, replies=0, tx_bytes=0, rx_bytes=0,
                  non_scp_frames=0, integrity_errors=0, error=None)
    start = time.monotonic()
    buf = bytearray()
    baseline = None
    seq = parity
    initializing = True
    try:
        with socket.create_connection(('127.0.0.1', port), timeout=5) as sock:
            sock.settimeout(5)
            start = time.monotonic()
            while time.monotonic() - start < seconds:
                wanted = []
                payload = b''
                for _ in range(1 if initializing else batch):
                    seq = (seq + 2) % 256
                    wanted.append(seq)
                    payload += b'\xc0\x7a' + escape(bytes([2, 1 if initializing else 0x10, seq])) + b'\xc0'
                sock.sendall(payload)
                result['tx_bytes'] += len(payload)
                if not initializing: result['requests'] += len(wanted)
                deadline = time.monotonic() + 5
                while wanted:
                    if time.monotonic() > deadline:
                        raise TimeoutError('reply batch did not complete in 5 seconds')
                    data = sock.recv(4096)
                    if not data:
                        raise EOFError('iPhone bridge closed the socket')
                    result['rx_bytes'] += len(data)
                    buf += data
                    while 0xc0 in buf:
                        first = buf.index(0xc0)
                        if first:
                            raise ValueError('bytes outside a KISS frame')
                        try: end = buf.index(0xc0, 1)
                        except ValueError: break
                        raw = bytes(buf[1:end]); del buf[:end]
                        if not raw:
                            continue
                        frame = bytearray()
                        i = 0
                        while i < len(raw):
                            b = raw[i]; i += 1
                            if b == 0xdb:
                                if i >= len(raw) or raw[i] not in (0xdc, 0xdd):
                                    raise ValueError('invalid KISS escape')
                                b = 0xc0 if raw[i] == 0xdc else 0xdb; i += 1
                            frame.append(b)
                        if frame[0] != 0x7a:
                            result['non_scp_frames'] += 1
                            continue
                        if len(frame) >= 5 and frame[2] == 0x7f:
                            raise RuntimeError('SCP error: ' + bytes(frame[5:]).decode('utf8', 'replace'))
                        expected_type = 0x81 if initializing else 0x90
                        if len(frame) < 4 or frame[1] != 2 or frame[2] != expected_type or (not initializing and len(frame) != 30):
                            raise ValueError('unexpected SCP version/type/length')
                        if not wanted or frame[3] != wanted.pop(0):
                            raise ValueError('missing, duplicate or reordered response sequence')
                        if initializing: continue
                        body = bytes(frame[4:])
                        if baseline is None: baseline = body
                        if body != baseline:
                            raise ValueError('GET_RADIO payload changed during read-only run')
                        result['replies'] += 1
                if initializing:
                    initializing = False
                    start = time.monotonic()
                    result['tx_bytes'] = result['rx_bytes'] = 0
    except Exception as exc:
        result['error'] = f'{type(exc).__name__}: {exc}'
        result['integrity_errors'] += isinstance(exc, ValueError)
    elapsed = max(time.monotonic() - start, 1e-9)
    result['elapsed_s'] = round(elapsed, 6)
    result['reply_payload_Bps'] = round(result['replies'] * 26 / elapsed, 2)
    result['requests_per_s'] = round(result['replies'] / elapsed, 2)
    result['pass'] = result['error'] is None and result['requests'] > 0 and result['requests'] == result['replies']
    return result

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--seconds', type=float, default=60)
    p.add_argument('--batch', type=int, default=8)
    p.add_argument('--nus', type=int, default=17633)
    p.add_argument('--ctrl', type=int, default=17634)
    a = p.parse_args()
    if not math.isfinite(a.seconds) or a.seconds <= 0 or not 1 <= a.batch <= 64:
        p.error('--seconds must be positive and --batch must be 1..64')
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        jobs = [pool.submit(run, 'NUS', a.nus, a.seconds, a.batch, 0),
                pool.submit(run, 'CTRL', a.ctrl, a.seconds, a.batch, 1)]
        results = [job.result() for job in jobs]
    print(json.dumps(dict(utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                         workload='read-only KISS SCP GET_RADIO, concurrent channels',
                         seconds=a.seconds, batch=a.batch, results=results), indent=2))
    return 0 if all(r['pass'] for r in results) else 1


if __name__ == '__main__':
    sys.exit(main())
