from pathlib import Path
import os
import shlex
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
flags=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','openssl'],text=True))
with tempfile.TemporaryDirectory(prefix='speaker-mac-') as tmp:
    target=str(Path(tmp)/'test')
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-Wall','-Wextra','-Werror','-O2',
        '-DASSISTANT_USE_MAC=1','-DASSISTANT_USE_LUNARLINK=0',
        '-I'+str(root/'test/host/mac_assistant'),'-I'+str(root/'lunaros_apps/lunaredu_pet/src'),
        str(root/'test/mac_assistant_protocol_test.cpp'),
        str(root/'lunaros_apps/lunaredu_pet/src/interaction/transport/xiaozhi_official_network_xiaozhi_mqtt_udp_channel.cpp'),
        *flags,'-o',target],check=True)
    subprocess.run([target],check=True)
