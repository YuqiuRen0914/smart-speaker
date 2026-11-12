"""Single-speaker Mac companion: authenticated bootstrap, MQTT control, Opus UDP.

No inference or cloud calls run on transport callbacks. All inference is owned by
one subprocess. Session/turn checks gate every output and device tool operation.
"""
import concurrent.futures
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import hmac
import json
import logging
import multiprocessing as mp
import os
from pathlib import Path
import queue
import re
import secrets
import signal
import socket
import subprocess
import threading
import time
import uuid

import paho.mqtt.client as mqtt

from deepseek_client import tool_reply, read_key
from lan_protocol import encode, decode
from opus_codec import decoder, encoder
from provision import ROOT, RUNTIME, provision
from voice_engine import inference_worker
from power_guard import PowerGuard


class Companion:
    def __init__(self, config):
        self.config = config
        self.lock = threading.RLock()
        self.infer_lock = threading.Lock()
        self.turn_slots = threading.BoundedSemaphore(2)
        self.pool = concurrent.futures.ThreadPoolExecutor(max_workers=2)
        self.stop = threading.Event()
        self.ready = False
        self.pending_hello = None
        self.session = None
        self.turn = 0
        self.listening = False
        self.audio = bytearray()
        self.pending_rpc = {}
        self.catalog = []
        self.device_id = None
        self.metrics = {}
        self.phase = 'starting'
        self.control_messages = 0
        self.udp_packets = 0
        self.last_error_stage = None
        self.heartbeat = time.time()
        context = mp.get_context('spawn')
        self.inbox, self.outbox = context.Queue(2), context.Queue(2)
        self.worker = context.Process(target=inference_worker,
            args=(self.inbox, self.outbox, config['backend']), daemon=True)
        self.worker.start()
        self.udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp.bind(('0.0.0.0', config['udp_port']))
        self.udp.settimeout(.5)
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id='feiyu-mac-server',
                                  clean_session=True)
        self.client.username_pw_set('server', config['server_password'])
        self.client.on_connect = self.on_connect
        self.client.on_disconnect = self.on_disconnect
        self.client.on_message = self.on_message
        self.client.max_queued_messages_set(16)
        self.client.connect('127.0.0.1', config['mqtt_port'], keepalive=10)
        self.client.loop_start()
        threading.Thread(target=self.receive_audio, daemon=True).start()
        kind, _, self.model_info = self.outbox.get(timeout=180)
        if kind != 'ready':
            raise RuntimeError('Voice model initialization failed')
        self.ready = True
        self.phase = 'idle'
        self.heartbeat = time.time()
        print('READY: Feiyu Cute F6 / SenseVoice / DeepSeek', flush=True)
        with self.lock:
            pending = self.pending_hello
            self.pending_hello = None
            if pending is not None:
                # A reconnect can arrive while the original models warm up.
                self.on_message(self.client, None, pending)

    def on_connect(self, client, userdata, flags, reason, properties):
        if reason == 0:
            client.subscribe('feiyu/up', qos=1)

    def on_disconnect(self, client, userdata, flags, reason, properties):
        with self.lock:
            self.invalidate()

    def invalidate(self):
        self.session = None
        self.phase = 'idle'
        self.listening = False
        self.audio.clear()
        self.catalog.clear()
        for future in self.pending_rpc.values():
            if not future.done():
                future.set_exception(RuntimeError('Session closed'))
        self.pending_rpc.clear()

    def current(self, identity):
        with self.lock:
            return (self.session is not None and identity == (self.session, self.turn)
                    and not self.stop.is_set() and abs(time.time() - self.heartbeat) < 5)

    def publish(self, identity, message):
        with self.lock:
            if not self.current(identity):
                return False
            message = dict(message, session_id=identity[0], turn_id=identity[1])
            result = self.client.publish('feiyu/down', json.dumps(message, ensure_ascii=False), qos=0, retain=False)
            return result.rc == mqtt.MQTT_ERR_SUCCESS

    def on_message(self, client, userdata, message):
        if message.retain or len(message.payload) > 65536:
            return
        try:
            data = json.loads(message.payload)
            if not isinstance(data, dict):
                return
            with self.lock:
                kind = data.get('type')
                self.control_messages += 1
                if kind == 'hello':
                    if not self.ready:
                        self.pending_hello = message
                        return
                    self.invalidate()
                    self.session, self.turn = uuid.uuid4().hex, 0
                    self.last_turn = 0
                    self.key = secrets.token_bytes(16)
                    self.nonce = b'\x01\x00\x00\x00' + secrets.token_bytes(4) + b'\x00' * 8
                    self.rx_sequence = self.tx_sequence = 0
                    self.peer = None
                    self.decoder = decoder()
                    self.publish((self.session, self.turn), dict(type='hello', version=3, transport='udp',
                        audio_params=dict(format='opus', sample_rate=24000, channels=1, frame_duration=60),
                        udp=dict(server=self.config['host'], port=self.config['udp_port'],
                                 key=self.key.hex(), nonce=self.nonce.hex())))
                    return
                if data.get('session_id') != self.session or self.session is None:
                    return
                if kind == 'mcp':
                    payload = data.get('payload', {})
                    if not isinstance(payload, dict):
                        return
                    future = self.pending_rpc.pop(payload.get('id'), None)
                    if future and not future.done():
                        future.set_result(payload)
                    return
                identity = (self.session, self.turn)
                incoming_turn = data.get('turn_id')
                if kind in ('abort', 'goodbye'):
                    if incoming_turn == self.turn:
                        self.turn = -1  # client next listen owns a fresh positive turn id
                        self.listening = False
                        self.audio.clear()
                        self.phase = 'idle'
                    return
                if kind != 'listen' or not isinstance(incoming_turn, int) or not 0 < incoming_turn < 2**32:
                    return
                state = data.get('state')
                if state in ('start', 'detect'):
                    if incoming_turn <= self.last_turn:
                        return
                    self.turn = incoming_turn
                    self.last_turn = incoming_turn
                    self.listening = state == 'start'
                    self.phase = 'listening' if self.listening else 'thinking'
                    self.audio.clear()
                    self.decoder = decoder()
                    self.started_listen = time.monotonic()
                    identity = (self.session, self.turn)
                    if state == 'detect':
                        text = data.get('text', '')
                        if isinstance(text, str) and 0 < len(text) <= 200:
                            self.submit(identity, text=text)
                elif state == 'stop' and incoming_turn == self.turn and self.listening:
                    # MQTT stop can overtake the final UDP frame. A bounded tail
                    # collection retains this turn only and cannot consume the next.
                    timer = threading.Timer(.15, self.finish_recording, args=(identity,))
                    timer.daemon = True
                    timer.start()
        except (ValueError, KeyError, TypeError):
            logging.warning('Rejected malformed control message')

    def finish_recording(self, identity):
        with self.lock:
            if not self.current(identity) or not self.listening:
                return
            self.listening = False
            pcm = bytes(self.audio)
            self.audio.clear()
            self.submit(identity, pcm=pcm)

    def receive_audio(self):
        while not self.stop.is_set():
            try:
                packet, peer = self.udp.recvfrom(2048)
                with self.lock:
                    if self.session is None:
                        continue
                    if packet[1:2] != b'\x00':
                        continue
                    sequence, turn, payload = decode(self.key, self.nonce, packet, self.rx_sequence)
                    if turn != self.turn:
                        # A fresh hello replaces the key/connection nonce, but
                        # firmware retains its monotonic turn counter. Its
                        # silence probe may precede the next listen/detect.
                        # Learn only the endpoint; never admit old-turn audio.
                        if self.peer is None and not self.listening:
                            self.peer = peer
                            self.rx_sequence = sequence
                            self.udp_packets += 1
                        continue
                    if self.peer is not None and peer != self.peer:
                        continue
                    self.rx_sequence = sequence
                    self.peer = peer
                    self.udp_packets += 1
                    if self.listening:
                        pcm = self.decoder.decode(payload, 960)
                        if len(self.audio) + len(pcm) <= 16000 * 2 * 30:
                            self.audio.extend(pcm)
                        else:
                            self.finish_recording((self.session, self.turn))
            except socket.timeout:
                continue
            except (ValueError, OSError):
                continue
            except Exception:
                logging.warning('Rejected invalid Opus frame')

    def rpc(self, identity, method, params):
        with self.lock:
            if not self.current(identity):
                raise RuntimeError('Turn cancelled')
            request_id = uuid.uuid4().hex
            future = concurrent.futures.Future()
            self.pending_rpc[request_id] = future
            if not self.publish(identity, dict(type='mcp', payload=dict(jsonrpc='2.0', id=request_id,
                                                                      method=method, params=params))):
                self.pending_rpc.pop(request_id, None)
                raise RuntimeError('Device disconnected')
        try:
            response = future.result(timeout=5)
            if 'error' in response:
                raise RuntimeError('Device operation failed')
            return response.get('result', {})
        finally:
            with self.lock:
                self.pending_rpc.pop(request_id, None)

    def infer(self, identity, kind, value):
        with self.infer_lock:
            if not self.current(identity):
                raise RuntimeError('Turn cancelled')
            self.inbox.put((kind, identity, value), timeout=1)
            try:
                result_kind, result_identity, result = self.outbox.get(timeout=60)
            except queue.Empty:
                self.ready = False
                self.stop.set() # supervisor terminates hung model process and broker
                raise RuntimeError('Voice inference timeout')
            if result_identity != identity or result_kind != kind:
                raise RuntimeError('Voice inference failed')
            return result

    def submit(self, identity, *, text=None, pcm=None):
        if not self.turn_slots.acquire(blocking=False):
            self.publish(identity, dict(type='mac_error', reason='busy'))
            return
        self.pool.submit(self.answer, identity, text, pcm)

    def answer(self, identity, text, pcm):
        started = time.perf_counter()
        try:
            metrics = {}
            if pcm is not None:
                self.set_phase(identity, 'recognizing')
                result = self.infer(identity, 'asr', pcm)
                text = result['text']
                metrics['asr_seconds'] = result['seconds']
            if not text or not self.current(identity):
                raise RuntimeError('No recognized speech')
            self.publish(identity, dict(type='stt', text=text))
            with self.lock:
                catalog = list(self.catalog)
            if not catalog:
                self.rpc(identity, 'initialize', dict(protocolVersion='2024-11-05', capabilities={},
                                                     clientInfo=dict(name='feiyu-mac', version='0.1')))
                catalog = self.rpc(identity, 'tools/list', {}).get('tools', [])[:48]
                with self.lock:
                    if self.current(identity):
                        self.catalog = catalog
            llm_start = time.perf_counter()
            self.set_phase(identity, 'thinking')
            reply = tool_reply(text, catalog,
                               lambda name, args: self.rpc(identity, 'tools/call', dict(name=name, arguments=args)),
                               lambda: self.current(identity))
            metrics['deepseek_seconds'] = time.perf_counter() - llm_start
            # The prompt constrains normal replies; bound model output anyway.
            reply = re.sub(r'[\x00-\x1f]', '', reply).strip()[:160]
            self.set_phase(identity, 'synthesizing')
            result = self.infer(identity, 'tts', reply)
            metrics['tts_seconds'] = result['seconds']
            metrics['first_audio_seconds'] = time.perf_counter() - started
            metrics['audio_seconds'] = len(result['pcm']) / 48000
            self.set_phase(identity, 'playing')
            self.play(identity, reply, result['pcm'])
            with self.lock:
                if self.current(identity):
                    self.metrics = metrics
            print('TURN_METRICS ' + json.dumps(metrics), flush=True)
        except Exception as error:
            if self.current(identity):
                self.last_error_stage = self.phase
            self.publish(identity, dict(type='mac_error', reason='unavailable'))
            logging.warning('Turn unavailable at %s: %s', self.phase, type(error).__name__)
        finally:
            self.set_phase(identity, 'idle')
            self.turn_slots.release()

    def set_phase(self, identity, phase):
        with self.lock:
            if self.current(identity):
                self.phase = phase

    def play(self, identity, text, pcm):
        codec = encoder()
        with self.lock:
            if not self.current(identity) or self.peer is None:
                raise RuntimeError('No current UDP peer')
        self.publish(identity, dict(type='tts', state='start'))
        self.publish(identity, dict(type='tts', state='sentence_start', text=text))
        deadline = time.monotonic() + .18
        for start in range(0, len(pcm), 2880):
            frame = pcm[start:start+2880].ljust(2880, b'\x00')
            packet = codec.encode(frame, 1440)
            self.stop.wait(max(0, deadline-time.monotonic()))
            with self.lock:
                if not self.current(identity):
                    return
                self.tx_sequence += 128
                wire = encode(self.key, self.nonce, self.tx_sequence, identity[1], packet, direction=1)
                self.udp.sendto(wire, self.peer)
            deadline += .06
        self.stop.wait(max(0, deadline-time.monotonic()) + .15)
        self.publish(identity, dict(type='tts', state='stop'))

    def close(self):
        self.ready = False
        self.stop.set()
        with self.lock:
            self.invalidate()
        self.client.disconnect()
        self.client.loop_stop()
        self.udp.close()
        self.worker.terminate()
        self.worker.join(5)
        self.pool.shutdown(wait=False, cancel_futures=True)


def http_handler(companion):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def respond(self, status, body):
            encoded = json.dumps(body).encode()
            self.send_response(status)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(encoded)))
            self.end_headers()
            self.wfile.write(encoded)

        def do_GET(self):
            if self.path == '/health' and self.client_address[0] == '127.0.0.1':
                self.respond(200, dict(ready=companion.ready, phase=companion.phase, metrics=companion.metrics,
                                      mqtt_connected=companion.client.is_connected(),
                                      session_ready=companion.session is not None,
                                      udp_ready=getattr(companion, 'peer', None) is not None,
                                      control_messages=companion.control_messages, udp_packets=companion.udp_packets,
                                      last_error_stage=companion.last_error_stage,
                                      backend=companion.config['backend']))
            else:
                self.respond(404, {})

        def do_POST(self):
            self.connection.settimeout(5)
            if self.path != '/xiaozhi/ota/' or not hmac.compare_digest(
                    self.headers.get('X-Voice-Token', '').encode(), companion.config['bootstrap_token'].encode()):
                self.respond(403, {})
                return
            try:
                length = int(self.headers.get('Content-Length', '0'))
                device = self.headers.get('Device-Id', '').lower()
                if not 0 < length <= 16384 or not re.fullmatch(r'(?:[0-9a-f]{2}:){5}[0-9a-f]{2}', device):
                    raise ValueError('Bad bootstrap')
                json.loads(self.rfile.read(length))
            except (ValueError, OSError):
                self.respond(400, {})
                return
            with companion.lock:
                if not companion.ready:
                    self.respond(503, {})
                    return
                if companion.device_id not in (None, device):
                    self.respond(409, {})
                    return
                companion.device_id = device
            config = companion.config
            self.respond(200, dict(mqtt=dict(endpoint=f'mqtt://{config["host"]}:{config["mqtt_port"]}',
                client_id='feiyu-speaker', username='speaker', password=config['device_password'],
                publish_topic='feiyu/up', subscribe_topic='feiyu/down'),
                server_time=dict(timestamp=int(time.time()*1000), timezone_offset=480)))
    return Handler


def main():
    logging.basicConfig(level=logging.WARNING)
    read_key()
    config = provision()
    broker = subprocess.Popen(['/opt/homebrew/opt/mosquitto/sbin/mosquitto', '-c', str(RUNTIME/'mosquitto.conf')])
    companion = None
    server = None
    power = None
    try:
        for _ in range(30):
            if broker.poll() is not None:
                raise RuntimeError('MQTT broker failed')
            try:
                with socket.create_connection(('127.0.0.1', config['mqtt_port']), timeout=.1):
                    break
            except OSError:
                time.sleep(.1)
        companion = Companion(config)
        power = PowerGuard()
        server = ThreadingHTTPServer(('0.0.0.0', config['http_port']), http_handler(companion))
        threading.Thread(target=server.serve_forever, daemon=True).start()
        def stopping(*args):
            companion.stop.set()
        signal.signal(signal.SIGTERM, stopping)
        signal.signal(signal.SIGINT, stopping)
        while not companion.stop.wait(.5):
            with companion.lock:
                if abs(time.time() - companion.heartbeat) >= 5:
                    companion.invalidate()
                    # Restart all transports after sleep. Broker closure makes the
                    # speaker reconnect too; no pre-sleep response can be replayed.
                    raise RuntimeError('Sleep/wake gap: fresh session required')
                companion.heartbeat = time.time()
            if not companion.worker.is_alive() or broker.poll() is not None:
                raise RuntimeError('Required service stopped')
    finally:
        if server:
            server.shutdown()
        if companion:
            companion.close()
        if power:
            power.close()
        broker.terminate()
        broker.wait(timeout=5)


if __name__ == '__main__':
    main()
