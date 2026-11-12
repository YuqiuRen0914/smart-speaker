"""Physical-device regression: real DeepSeek/TTS and music share one output."""
import argparse
import json
from pathlib import Path
import time
import urllib.request

parser = argparse.ArgumentParser()
parser.add_argument('--host', default='192.168.31.169')
parser.add_argument('--text', required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

def api(module, method, body=None):
    data = json.dumps(body, ensure_ascii=False).encode() if body is not None else None
    request = urllib.request.Request(f'http://{args.host}/api/v1/modules/{module}/{method}',
        data=data, headers={'Content-Type': 'application/json'})
    with opener.open(request, timeout=4) as response:
        return json.load(response)['data']

def snapshot():
    return dict(assistant=api('xiaozhi', 'status.get'),
                music=api('smart_speaker', 'playlist.get')['status'])

report = dict(before=snapshot(), samples=[])
started = time.monotonic()
api('xiaozhi', 'conversation.test', dict(text=args.text))
spoke = False
last_phase = None
try:
    for _ in range(150):
        state = snapshot()
        state['seconds'] = time.monotonic() - started
        report['samples'].append(state)
        assistant, music = state['assistant'], state['music']
        phase = assistant['session_state']
        if phase != last_phase:
            print(dict(phase=phase, seconds=round(state['seconds'], 2),
                       music_playing=music['playing'], transcript=assistant['transcript_text']), flush=True)
            last_phase = phase
        spoke |= assistant['speaking']
        assert not (assistant['speaking'] and music['playing'] and not music['paused']), 'Voice and music overlap'
        for name in ('downlink_decode_failures', 'downlink_playback_failures', 'output_failures'):
            assert assistant[name] == report['before']['assistant'][name], name
        if spoke and not assistant['speaking'] and phase in ('inactive', 'ready_idle'):
            break
        time.sleep(.4)
    assert spoke, 'No physical voice playback observed'
    time.sleep(3)
    report['after'] = snapshot()
    assert report['after']['assistant']['output_frames'] > report['before']['assistant']['output_frames']
    assert report['after']['music']['playing'], 'Music did not start/resume after voice'
    report['passed'] = True
    print('PASS: voice output succeeded, no overlap, music started/resumed', flush=True)
finally:
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2))
