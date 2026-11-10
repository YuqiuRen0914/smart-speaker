"""Exercise actual production handlers with hardware boundaries replaced."""
from pathlib import Path
import os,subprocess,tempfile
app=Path(__file__).resolve().parents[1];repo=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='speaker-lyrics_api-') as tmp:
    binary=Path(tmp)/'test.exe'
    args=['g++','-std=c++17','-D_WIN32_WINNT=0x0601','-include',str(app/'test/host/host_mutex.h')]
    args += ['-I'+str(p) for p in [app/'test/host',app/'main/smart_speaker',repo/'lunaros/services/optional/service_audio/include',repo/'lunaros/os/core/include',repo/'lunaros/apps/optional/app_music/include',app/'managed_components/bblanchon__arduinojson/src']]
    args += ['-I'+str(repo/'lunaros/os/device_api/include')]
    args += [str(repo/'lunaros/apps/optional/app_music/src/lyrics.cpp'),str(app/'test/lyrics_api_test.cpp'),str(app/'main/smart_speaker/smart_speaker_lyrics_api.cpp'),str(repo/'lunaros/os/device_api/src/projections/device_projection_json.cpp'),'-o',str(binary)]

    subprocess.run(args,check=True)
    subprocess.run([str(binary)],cwd=tmp,check=True)
print('Actual lyrics_api handlers: all regression scenarios passed')
