from pathlib import Path
import os
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='speaker-capabilities-') as tmp:
    target=str(Path(tmp)/'test')
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-Wall','-Wextra','-Werror','-O2',
        '-I'+str(root/'test/host/lunarlink'),'-I'+str(root/'lunaros_apps/lunaredu_pet/src'),
        '-I'+str(root/'managed_components/bblanchon__arduinojson/src'),
        str(root/'test/lunarlink_capabilities_test.cpp'),
        str(root/'lunaros_apps/lunaredu_pet/src/interaction/conversation/lunarlink_capabilities.cpp'),
        str(root/'lunaros_apps/lunaredu_pet/src/interaction/transport/lunarlink_protocol.cpp'),'-o',target],check=True)
    subprocess.run([target],check=True)
