"""A device keeps its turn counter when reopening a fresh encrypted session."""
import socket
import threading
import time
import unittest
import json
from types import SimpleNamespace

from lan_protocol import encode
from lan_service import Companion


class ProbeTest(unittest.TestCase):
    def test_hello_is_retained_during_model_warmup(self):
        service = Companion.__new__(Companion)
        service.lock = threading.RLock()
        service.ready = False
        service.control_messages = 0
        service.pending_hello = None
        message = SimpleNamespace(retain=False, payload=json.dumps({'type': 'hello'}).encode())
        service.on_message(None, None, message)
        self.assertIs(service.pending_hello, message)

    def test_reopen_probe_is_address_only_and_old_audio_stays_rejected(self):
        service = Companion.__new__(Companion)
        service.stop = threading.Event()
        service.lock = threading.RLock()
        service.session = 'new-session'
        service.turn = 0
        service.listening = False
        service.key = bytes(range(16))
        service.nonce = bytes.fromhex('01000000112233440000000000000000')
        service.rx_sequence = service.udp_packets = 0
        service.peer = None
        service.audio = bytearray()
        service.decoder = type('Decoder', (), {'decode': lambda _, data, size: data})()
        service.udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        service.udp.bind(('127.0.0.1', 0))
        service.udp.settimeout(.05)
        sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sender.bind(('127.0.0.1', 0))
        thread = threading.Thread(target=service.receive_audio)
        thread.start()
        try:
            def send(turn, sequence, payload):
                sender.sendto(encode(service.key, service.nonce, sequence, turn, payload, direction=0),
                              service.udp.getsockname())
                time.sleep(.08)
            send(7, 128, b'probe')
            self.assertEqual(service.peer, sender.getsockname())
            self.assertEqual(service.turn, 0)
            self.assertEqual(service.audio, b'')
            with service.lock:
                service.turn = 8
                service.listening = True
            send(7, 256, b'stale')
            send(8, 384, b'current')
            self.assertEqual(service.audio, b'current')
            send(8, 384, b'replay')
            self.assertEqual(service.audio, b'current')
        finally:
            service.stop.set()
            thread.join(timeout=1)
            service.udp.close()
            sender.close()


if __name__ == '__main__':
    unittest.main()
