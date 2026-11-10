"""Compile the production lifecycle with allocation, driver and task boundary fakes."""
from pathlib import Path
import subprocess,tempfile
app=Path(__file__).resolve().parents[1];repo=app
with tempfile.TemporaryDirectory(prefix='speaker-audio-lifecycle-') as tmp:
    binary=Path(tmp)/'test.exe'
    subprocess.run(['g++','-std=c++17','-I'+str(app/'test/host'),'-I'+str(app/'main/smart_speaker'),'-I'+str(repo/'lunaros/include'),str(app/'test/audio_lifecycle_test.cpp'),str(app/'main/smart_speaker/smart_speaker_audio_lifecycle.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('Actual audio lifecycle: allocation/channel/task failures, rollback, idempotence, exit timeout and retained-resource retry passed')
