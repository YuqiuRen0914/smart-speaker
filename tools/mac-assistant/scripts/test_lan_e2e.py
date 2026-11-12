"""Real LAN protocol + ASR + paid DeepSeek + original TTS integration test.

The peer simulates firmware transport; it does not validate the physical mic.
Only test transcripts/audio are saved. No real device tools are executed.
"""
import argparse
import json
from pathlib import Path
import queue
import socket
import threading
import time
import wave
import requests
import paho.mqtt.client as mqtt
from lan_protocol import encode, decode
from opus_codec import opuslib
from provision import RUNTIME


class Peer:
    def __init__(self):
        config = json.loads((RUNTIME/'service.json').read_text())
        denied = requests.post('http://127.0.0.1:8765/xiaozhi/ota/', json={}, timeout=3)
        assert denied.status_code == 403
        response = requests.post('http://127.0.0.1:8765/xiaozhi/ota/', json={'version': 2},
            headers={'X-Voice-Token': config['bootstrap_token'], 'Device-Id': '02:00:00:00:00:01'}, timeout=3)
        response.raise_for_status()
        transport = response.json()['mqtt']
        self.messages = queue.Queue()
        self.hello = None
        self.current_turn = 0
        self.events = []
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=transport['client_id'], clean_session=True)
        self.client.username_pw_set(transport['username'], transport['password'])
        self.client.on_connect = lambda c, u, f, r, p: c.subscribe('feiyu/down')
        self.client.on_message = self.on_message
        self.client.connect('127.0.0.1', config['mqtt_port'])
        self.client.loop_start()
        self.udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp.bind(('127.0.0.1', 0))
        self.udp.settimeout(.1)
        self.udp_port = config['udp_port']
        time.sleep(.3)
        self.client.publish('feiyu/up', json.dumps(dict(type='hello', version=3, transport='udp',
            audio_params=dict(format='opus', sample_rate=16000, channels=1, frame_duration=60))))
        self.hello = self.messages.get(timeout=5)
        assert self.hello['type'] == 'hello'
        self.key = bytes.fromhex(self.hello['udp']['key'])
        self.nonce = bytes.fromhex(self.hello['udp']['nonce'])
        self.tx_sequence = self.rx_sequence = 0

    def on_message(self, client, userdata, wire):
        message = json.loads(wire.payload)
        if message['type'] == 'mcp':
            payload = message['payload']
            result = {'tools': []} if payload['method'] == 'tools/list' else {'protocolVersion': '2024-11-05'}
            self.send(dict(type='mcp', payload=dict(jsonrpc='2.0', id=payload['id'], result=result)))
        else:
            self.events.append((time.perf_counter(), message))
            self.messages.put(message)

    def send(self, message):
        self.client.publish('feiyu/up', json.dumps(dict(message,
            session_id=self.hello['session_id'], turn_id=self.current_turn)), qos=0)

    def audio(self, pcm):
        codec = opuslib.Encoder(16000, 1, opuslib.APPLICATION_AUDIO)
        deadline = time.monotonic()
        for offset in range(0, len(pcm), 1920):
            frame = pcm[offset:offset+1920].ljust(1920, b'\x00')
            packet = codec.encode(frame, 960)
            self.tx_sequence += 128
            self.udp.sendto(encode(self.key, self.nonce, self.tx_sequence, self.current_turn, packet),
                            ('127.0.0.1', self.udp_port))
            deadline += .06
            time.sleep(max(0, deadline-time.monotonic()))

    def receive(self, output, timeout=50):
        codec = opuslib.Decoder(24000, 1)
        pcm = bytearray()
        started, first = time.perf_counter(), None
        transcript = {}
        while time.perf_counter()-started < timeout:
            try:
                packet, _ = self.udp.recvfrom(2048)
                seq, turn, opus = decode(self.key, self.nonce, packet, self.rx_sequence)
                self.rx_sequence = seq
                if turn == self.current_turn:
                    first = first or time.perf_counter()-started
                    pcm.extend(codec.decode(opus, 1440))
            except socket.timeout:
                pass
            while not self.messages.empty():
                event = self.messages.get_nowait()
                if event.get('turn_id') != self.current_turn:
                    continue
                assert event['type'] != 'mac_error', event
                if event['type'] == 'stt':
                    transcript['recognized'] = event['text']
                if event['type'] == 'tts' and event['state'] == 'sentence_start':
                    transcript['reply'] = event['text']
                if event['type'] == 'tts' and event['state'] == 'stop':
                    assert pcm and first is not None
                    with wave.open(str(output), 'wb') as stream:
                        stream.setnchannels(1); stream.setsampwidth(2); stream.setframerate(24000)
                        stream.writeframes(pcm)
                    return dict(transcript, first_audio_seconds=first, audio_seconds=len(pcm)/48000,
                                wav=str(output.resolve()), received_frames=len(pcm)//2880)
        raise TimeoutError('No complete spoken response')

    def close(self):
        self.client.disconnect()
        self.client.loop_stop()
        self.udp.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--input', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    args.output.mkdir(exist_ok=False, parents=True)
    import soundfile as sf
    import numpy as np
    import scipy.signal
    audio, rate = sf.read(args.input, dtype='float32')
    audio = scipy.signal.resample_poly(audio, 16000, rate)
    pcm = (np.clip(audio, -1, 1)*32767).astype('<i2').tobytes()
    peer = Peer()
    try:
        peer.current_turn = 1
        peer.send(dict(type='listen', state='start', mode='auto'))
        time.sleep(.2)
        peer.audio(pcm)
        peer.send(dict(type='listen', state='stop'))
        report = peer.receive(args.output/'reply.wav')
        # Start a paid request then cancel immediately; no old TTS may arrive.
        peer.current_turn = 2
        peer.send(dict(type='listen', state='detect', text='一句话介绍你自己。'))
        for _ in range(100):
            health = requests.get('http://127.0.0.1:8765/health', timeout=2).json()
            if health.get('phase') == 'synthesizing':
                break
            time.sleep(.1)
        else:
            raise AssertionError('Did not reach synthesis phase for cancellation test')
        peer.send(dict(type='abort'))
        cancelled_at = time.perf_counter()
        time.sleep(4)
        stale = [event for at, event in peer.events if at > cancelled_at + .3 and
                 event.get('turn_id') == 2 and event['type'] == 'tts']
        assert not stale, 'Cancelled response replayed'
        report['cancel_late_tts_rejected'] = True
        report['cancel_during_synthesis'] = True
        peer.current_turn = 3
        peer.send(dict(type='listen', state='detect', text='用一句话说主人晚安。'))
        # Valid silent Opus probe for the current UDP turn.
        peer.audio(b'\x00' * 1920)
        report['after_cancel'] = peer.receive(args.output/'after-cancel.wav')
        (args.output/'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2))
        print(json.dumps(report, ensure_ascii=False))
    finally:
        peer.close()
