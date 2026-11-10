"""Compile the real transfer engine, including MP3 normalization and accounting."""
from pathlib import Path
import subprocess, tempfile
app=Path(__file__).resolve().parents[1]
repo=app
with tempfile.TemporaryDirectory(prefix='speaker-transfer-') as directory:
    binary=Path(directory)/'test.exe'
    args=['g++','-std=c++17','-pthread','-D_WIN32_WINNT=0x0601','-include',str(repo/'lunaros_apps/lunaredu_pet/test/host/mutex.h')]
    args += ['-I'+str(p) for p in [app/'test/host',app/'main/smart_speaker',repo/'lunaros/include',app/'managed_components/bblanchon__arduinojson/src']]
    args += [str(app/'test/transfer_test.cpp'),str(app/'main/smart_speaker/smart_speaker_transfer.cpp'),'-o',str(binary)]
    subprocess.run(args,check=True)
    subprocess.run([str(binary)],cwd=directory,check=True)
print('Actual transfer engine: dismissal, batch expiry, clock wrap, partial/full reads and MP3 normalization passed')
