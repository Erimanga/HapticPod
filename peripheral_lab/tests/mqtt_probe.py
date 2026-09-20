#!/usr/bin/env python3
"""Optional LIVE public-broker probe, using production MQTT codec, not PAN/lwIP.
Publishes one synthetic, non-retained message to test.mosquitto.org:1883.
"""
import ctypes
import os
from pathlib import Path
import secrets
import shlex
import socket
import subprocess
import tempfile
import time

root = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='lab-mqtt-probe-') as tmp:
    lib = Path(tmp) / 'mqtt.so'
    subprocess.run([os.environ.get('CC', 'cc'), *shlex.split(os.environ.get('CFLAGS', '')),
                    '-shared', '-fPIC', '-I' + str(root.parent / 'src'),
                    str(root / 'mqtt_probe_bridge.c'), str(root.parent / 'src/network/lab_mqtt.c'),
                    '-o', str(lib)], check=True)
    codec = ctypes.CDLL(str(lib))
    codec.probe_start.argtypes = [ctypes.c_char_p, ctypes.c_uint]
    codec.probe_tx.restype = ctypes.POINTER(ctypes.c_uint8)
    codec.probe_tx_len.restype = ctypes.c_uint
    codec.probe_feed.argtypes = [ctypes.c_void_p, ctypes.c_uint]
    codec.probe_error.restype = codec.probe_topic.restype = ctypes.c_char_p
    codec.probe_start(('host' + secrets.token_hex(9)).encode(), secrets.randbits(32))
    print('Public plaintext broker: test.mosquitto.org:1883', flush=True)
    print('Synthetic probe topic:', codec.probe_topic().decode(), flush=True)
    deadline = time.monotonic() + 35
    with socket.create_connection(('test.mosquitto.org', 1883), timeout=8) as sock:
        while not codec.probe_done():
            error = codec.probe_error()
            if error:
                raise RuntimeError(error.decode())
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError('MQTT probe exceeded 35 seconds')
            sock.settimeout(min(8, remaining))
            n = codec.probe_tx_len()
            if n:
                sock.sendall(ctypes.string_at(codec.probe_tx(), n))
                codec.probe_sent()
                if not codec.probe_done():
                    codec.probe_feed(None, 0)
            else:
                packet = sock.recv(128)
                if not packet:
                    raise ConnectionError('Broker closed before verified echo')
                codec.probe_feed(ctypes.c_char_p(packet), len(packet))
    print('PASS: live public broker CONNECT, SUBACK, exact publish/subscribe echo and DISCONNECT; desktop network only')
