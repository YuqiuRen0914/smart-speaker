"""Stop/start only the Mac service; verify local music and device continuity."""
import json
from pathlib import Path
import subprocess
import sys
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

def get(path):
    with opener.open(path, timeout=3) as response:
        return json.load(response)

def state():
    base = 'http://192.168.31.169/api/v1/modules/'
    return dict(assistant=get(base+'xiaozhi/status.get')['data'],
                music=get(base+'smart_speaker/playlist.get')['data']['status'],
                system=get(base+'smart_speaker/debug.get')['data']['system'])

ctl = [sys.executable, str(ROOT/'scripts/service_ctl.py')]
report = dict(before=state())
try:
    try:
        subprocess.run(ctl+['stop'], check=True)
        for _ in range(15):
            time.sleep(1)
            report['offline'] = state()
            if not report['offline']['assistant']['transport_connected']:
                break
        assert not report['offline']['assistant']['transport_connected']
        time.sleep(3)
        report['offline_later'] = state()
        assert report['offline_later']['music']['playing']
        assert report['offline_later']['music']['position_ms'] > report['offline']['music']['position_ms']
        print('PASS: local music continued with Mac service stopped', flush=True)
    finally:
        subprocess.run(ctl+['start'], check=True)
    for _ in range(60):
        time.sleep(1)
        try:
            h = get('http://127.0.0.1:8765/health')
            if h['ready'] and h['session_ready'] and h['udp_ready']:
                report['after'] = state()
                if report['after']['assistant']['ready']:
                    report['health'] = h
                    break
        except OSError:
            continue
    assert 'health' in report, 'Device did not reconnect automatically'
    assert report['after']['system']['uptime_ms'] > report['before']['system']['uptime_ms'], 'Device rebooted'
    assert report['after']['music']['playing']
    assert report['after']['assistant']['output_frames'] == report['before']['assistant']['output_frames'], 'Old response replayed'
    report['passed'] = True
    print('PASS: automatic reconnect, no device reboot, no stale speech, music continued', flush=True)
finally:
    (ROOT/'results/device-reconnect-final.json').write_text(json.dumps(report, ensure_ascii=False, indent=2))
