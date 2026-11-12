from pathlib import Path
import os
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='speaker-mac-bootstrap-') as tmp:
    fixture=Path(tmp)/'interaction/transport/assistant_transport.h'
    fixture.parent.mkdir(parents=True)
    fixture.write_text('#pragma once\n#include "lunaredu_lunaros/network/http_client.h"\n')
    target=str(Path(tmp)/'bootstrap')
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-Wall','-Wextra','-Werror','-O2',
        '-DASSISTANT_USE_MAC=1','-DASSISTANT_USE_LUNARLINK=0',
        '-DCONFIG_LUNAREDU_ASSISTANT_MAC_TOKEN="'+'a'*64+'"',
        '-DCONFIG_LUNAREDU_ASSISTANT_MAC_URL="http://speaker.local:8765/xiaozhi/ota/"',
        '-I'+tmp,'-I'+str(root/'test/host/lunarlink'),'-I'+str(root/'lunaros_apps/lunaredu_pet/src'),
        '-I'+str(root/'lunaros/include'),'-I'+str(root/'lunaros/src'),
        '-I'+str(root/'managed_components/bblanchon__arduinojson/src'),
        str(root/'test/mac_assistant_bootstrap_test.cpp'),
        str(root/'lunaros_apps/lunaredu_pet/src/interaction/transport/xiaozhi_official_assistant_activation_client.cpp'),
        str(root/'lunaros/src/network/http/http_transport_policy.cpp'),
        '-o',target],check=True)
    subprocess.run([target],check=True)
    target=str(Path(tmp)/'policy')
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-Wall','-Wextra','-Werror','-O2',
        '-I'+str(root/'lunaros/include'),'-I'+str(root/'lunaros/src'),
        str(root/'lunaros/test/http_transport_policy_test.cpp'),
        str(root/'lunaros/src/network/http/http_transport_policy.cpp'),'-o',target],check=True)
    subprocess.run([target],check=True)
