"""Compile the production lifecycle with allocation, driver and task boundary fakes."""
from pathlib import Path
import os
import subprocess,tempfile
app=Path(__file__).resolve().parents[1];repo=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='speaker-audio-lifecycle-') as tmp:
    binary=Path(tmp)/'test.exe'
    subprocess.run(['g++','-std=c++20','-pthread','-I'+str(app/'test/host'),'-I'+str(repo/'lunaros/services/optional/service_audio/include'),'-I'+str(repo/'lunaros/services/optional/service_audio/src/backends'),'-I'+str(repo/'lunaros/os/core/include'),str(repo/'lunaros/services/optional/service_audio/test/dual_i2s_lifecycle_test.cpp'),str(repo/'lunaros/services/optional/service_audio/src/backends/dual_i2s_lifecycle.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('Actual audio lifecycle: allocation/channel/task failures, rollback, idempotence, exit timeout and retained-resource retry passed')
