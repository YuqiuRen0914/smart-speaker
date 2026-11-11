from pathlib import Path
import os
import shlex
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
flags=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','openssl'],text=True))
with tempfile.TemporaryDirectory(prefix='speaker-lunarlink-') as tmp:
    target=str(Path(tmp)/'test')
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-Wall','-Wextra','-Werror','-O2',
        '-I'+str(root/'test/host/lunarlink'),'-I'+str(root/'lunaros_apps/lunaredu_pet/src'),
        '-I'+str(root/'managed_components/bblanchon__arduinojson/src'),
        str(root/'test/lunarlink_protocol_test.cpp'),
        *[str(root/'lunaros_apps/lunaredu_pet/src/interaction/transport'/name) for name in
          ['lunarlink_protocol.cpp','network_xiaozhi_protocol.cpp','network_xiaozhi_mqtt_udp_channel.cpp']],
        *flags,'-o',target],check=True)
    subprocess.run([target],check=True)
