"""Compile the real transfer engine, including the shared MP3 importer and accounting."""
from pathlib import Path
import os
import subprocess, tempfile
app=Path(__file__).resolve().parents[1]
repo=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='speaker-transfer-') as directory:
    binary=Path(directory)/'test.exe'
    args=['g++','-std=c++17','-pthread','-D_WIN32_WINNT=0x0601','-include',str(app/'test/host/host_mutex.h')]
    args += ['-I'+str(p) for p in [app/'test/host',app/'main/smart_speaker',repo/'lunaros/services/optional/service_audio/include',repo/'lunaros/os/core/include',repo/'lunaros/apps/optional/app_music/include',repo/'lunaros/os/device_api/include',app/'managed_components/bblanchon__arduinojson/src']]
    args += [str(repo/'lunaros/services/optional/service_audio/src/audio_import.cpp'),str(app/'test/transfer_test.cpp'),str(app/'main/smart_speaker/smart_speaker_transfer.cpp'),'-o',str(binary)]
    subprocess.run(args,check=True)
    subprocess.run([str(binary)],cwd=directory,check=True)
print('Actual transfer adapter: safe names, byte-preserving fragmented/body import, shared validation, progress and failed-stream cleanup passed')
