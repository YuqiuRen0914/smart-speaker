"""Run the shared playback coordinator and its preserved product behavior cases."""
from pathlib import Path
import os,subprocess,tempfile
repo=Path(__file__).resolve().parents[1]
owner=repo/'lunaros/apps/optional/app_music'
with tempfile.TemporaryDirectory(prefix='shared-playback-') as tmp:
    binary=Path(tmp)/'test'
    subprocess.run([os.environ.get('CXX','g++'),'-std=c++17','-Wall','-Wextra','-Werror','-pthread',
      '-I'+str(owner/'include'),str(owner/'src/playback_coordinator.cpp'),str(owner/'src/playback_mode.cpp'),
      str(owner/'test/playback_coordinator_test.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
