"""User login service lifecycle. No system daemon, elevated access or cloud key copy."""
import argparse
import json
import os
from pathlib import Path
import plistlib
import subprocess
import shutil
import urllib.request
from provision import ROOT, RUNTIME, private_write

LABEL = 'local.shengyu.feiyu-voice'
DOMAIN = f'gui/{os.getuid()}'
PLIST = Path.home() / 'Library/LaunchAgents' / (LABEL + '.plist')
INSTALL = Path.home() / 'Library/Application Support/ShengyuVoice'


def command(action):
    if action == 'install':
        INSTALL.mkdir(parents=True, exist_ok=True)
        if ROOT != INSTALL:
            for name in ('.runtime', '.venv'):
                source, destination = ROOT/name, INSTALL/name
                if not destination.exists():
                    shutil.move(str(source), str(destination))
                    source.symlink_to(destination, target_is_directory=True)
                elif source.resolve() != destination.resolve():
                    raise RuntimeError('Existing installation directory conflicts with workspace')
            shutil.copytree(ROOT/'scripts', INSTALL/'scripts', dirs_exist_ok=True,
                            ignore=shutil.ignore_patterns('__pycache__'))
            broker = INSTALL/'.runtime/mosquitto.conf'
            private_write(broker, broker.read_text().replace(str(ROOT), str(INSTALL)))
        PLIST.parent.mkdir(parents=True, exist_ok=True)
        for name in ('service.log', 'service-error.log'):
            path = RUNTIME/name
            if not path.exists():
                private_write(path, '')
        config = dict(Label=LABEL, ProgramArguments=[str(INSTALL/'.venv/bin/python'),
                       str(INSTALL/'scripts/lan_service.py')], WorkingDirectory=str(INSTALL),
                      EnvironmentVariables={'PYTHONUNBUFFERED': '1', 'OMP_NUM_THREADS': '4',
                                            'TOKENIZERS_PARALLELISM': 'false'},
                      RunAtLoad=True, KeepAlive=True, ThrottleInterval=15,
                      ExitTimeOut=10, AbandonProcessGroup=False,
                      StandardOutPath=str(INSTALL/'.runtime/service.log'),
                      StandardErrorPath=str(INSTALL/'.runtime/service-error.log'))
        private_write(PLIST, plistlib.dumps(config).decode())
        print('Login service installed. Start it with service_ctl.py start.')
    elif action == 'start':
        if subprocess.run(['launchctl', 'print', DOMAIN+'/'+LABEL], capture_output=True).returncode == 0:
            print('Service is already loaded.')
            return
        subprocess.run(['launchctl', 'bootstrap', DOMAIN, str(PLIST)], check=True)
        print('Starting Feiyu voice service; models need a short warm-up.')
    elif action == 'stop':
        loaded = subprocess.run(['launchctl', 'print', DOMAIN+'/'+LABEL], capture_output=True).returncode == 0
        if loaded:
            subprocess.run(['launchctl', 'bootout', DOMAIN+'/'+LABEL], check=True)
        print('Stopped. Idle-sleep assertion released with the service.')
    elif action == 'restart':
        loaded = subprocess.run(['launchctl', 'print', DOMAIN+'/'+LABEL], capture_output=True).returncode == 0
        if loaded:
            subprocess.run(['launchctl', 'kickstart', '-k', DOMAIN+'/'+LABEL], check=True)
        else:
            command('start')
        print('Restart requested.')
    elif action == 'status':
        try:
            with urllib.request.urlopen('http://127.0.0.1:8765/health', timeout=2) as response:
                print(json.dumps(json.load(response), ensure_ascii=False, indent=2))
        except OSError:
            result = subprocess.run(['launchctl', 'print', DOMAIN+'/'+LABEL], capture_output=True, text=True)
            print('Starting or unavailable' if result.returncode == 0 else 'Stopped')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['install', 'start', 'stop', 'restart', 'status'])
    command(parser.parse_args().action)
