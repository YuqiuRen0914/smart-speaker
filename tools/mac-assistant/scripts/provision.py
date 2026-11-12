"""Provision private LAN transport credentials; never copy the DeepSeek key."""
import json
import os
from pathlib import Path
import secrets
import subprocess

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / '.runtime'


def private_write(path, content):
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    os.fchmod(fd, 0o600)
    with os.fdopen(fd, 'w') as stream:
        stream.write(content)


def provision():
    RUNTIME.mkdir(exist_ok=True)
    path = RUNTIME / 'service.json'
    if path.exists():
        return json.loads(path.read_text())
    host = subprocess.check_output(['scutil', '--get', 'LocalHostName'], text=True).strip() + '.local'
    config = dict(host=host, http_port=8765, mqtt_port=18883, udp_port=18884,
                  bootstrap_token=secrets.token_hex(32), device_password=secrets.token_hex(32),
                  server_password=secrets.token_hex(32), backend='mps-hybrid')
    private_write(path, json.dumps(config, indent=2))
    private_write(RUNTIME / 'mqtt-passwords',
                  'speaker:' + config['device_password'] + '\nserver:' + config['server_password'] + '\n')
    subprocess.run(['/opt/homebrew/opt/mosquitto/bin/mosquitto_passwd', '-U',
                    str(RUNTIME / 'mqtt-passwords')], check=True, stdout=subprocess.DEVNULL)
    private_write(RUNTIME / 'mqtt-acl', 'user speaker\ntopic write feiyu/up\ntopic read feiyu/down\n'
                  'user server\ntopic read feiyu/up\ntopic write feiyu/down\n')
    private_write(RUNTIME / 'mosquitto.conf',
                  f'listener {config["mqtt_port"]}\nallow_anonymous false\npersistence false\n'
                  f'password_file {RUNTIME / "mqtt-passwords"}\nacl_file {RUNTIME / "mqtt-acl"}\n'
                  'max_connections 4\nmax_packet_size 65536\nmax_queued_messages 8\nlog_type error\n')
    private_write(RUNTIME / 'sdkconfig.mac',
                  'CONFIG_LUNAREDU_ASSISTANT_MAC=y\n'
                  '# CONFIG_LUNAREDU_ASSISTANT_XIAOZHI is not set\n'
                  '# CONFIG_LUNAREDU_ASSISTANT_LUNARLINK is not set\n'
                  f'CONFIG_LUNAREDU_ASSISTANT_MAC_URL="http://{host}:8765/xiaozhi/ota/"\n'
                  f'CONFIG_LUNAREDU_ASSISTANT_MAC_TOKEN="{config["bootstrap_token"]}"\n')
    return config


if __name__ == '__main__':
    provision()
    print('Private transport configuration ready; no cloud key copied.')
