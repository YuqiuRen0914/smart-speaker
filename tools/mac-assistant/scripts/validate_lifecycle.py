"""Exercise the installed service, restart recovery and sleep-gap fencing."""
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import time
import requests
from service_ctl import command, DOMAIN, LABEL
from provision import ROOT


def ready(timeout=60):
    start = time.monotonic()
    while time.monotonic()-start < timeout:
        try:
            response = requests.get('http://127.0.0.1:8765/health', timeout=.5)
            if response.json().get('ready'):
                return
        except (requests.RequestException, ValueError):
            pass
        time.sleep(.5)
    raise RuntimeError('Service did not become ready')


def pid():
    output = subprocess.check_output(['launchctl', 'print', DOMAIN+'/'+LABEL], text=True)
    return int(re.search(r'\n\s*pid = (\d+)', output)[1])


def assertions_for(process):
    output = subprocess.run(['pgrep', '-fl', 'caffeinate'], capture_output=True, text=True).stdout
    return bool(re.search(r'-i -w ' + str(process) + r'\b', output))


if __name__ == '__main__':
    ready()
    before = pid()
    on_ac = "Now drawing from 'AC Power'" in subprocess.check_output(['pmset', '-g', 'batt'], text=True)
    if on_ac:
        assert assertions_for(before)
    command('stop')
    time.sleep(1)
    assert not assertions_for(before)
    try:
        requests.get('http://127.0.0.1:8765/health', timeout=1)
        raise AssertionError('Health endpoint survived stop')
    except requests.RequestException:
        pass
    command('start')
    ready()
    resumed = pid()
    assert resumed != before
    # Freeze only this service, never the Mac. A wall-clock gap must expire
    # sessions and force a broker/model restart just like a sleep/wake gap.
    os.kill(resumed, signal.SIGSTOP)
    try:
        time.sleep(6)
    finally:
        os.kill(resumed, signal.SIGCONT)
    time.sleep(2)
    ready()
    assert pid() != resumed
    report = dict(stop_releases_own_sleep_assertion=True, stop_closes_health=True,
                  restart_ready=True, simulated_sleep_gap_restarts_session=True,
                  ac_assertion_checked=on_ac, physical_sleep_and_unplug_tested=False)
    (ROOT/'results/service-lifecycle.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report))
