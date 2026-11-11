"""Compile and exercise production cover handlers and the real sidecar store."""
from pathlib import Path
import os
import subprocess
import tempfile

app = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="speaker-cover-api-") as tmp:
    binary = Path(tmp) / "test.exe"
    command = [os.environ.get("CXX", "g++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
               "-include", str(app / "lunaros_apps/lunaredu_pet/test/host/mutex.h")]
    command += ["-I" + str(app / p) for p in ["test/host", "main/smart_speaker", "lunaros/include",
                                                        "managed_components/bblanchon__arduinojson/src"]]
    command += [str(app / p) for p in ["test/cover_api_test.cpp", "main/smart_speaker/smart_speaker_cover.cpp",
                                     "main/smart_speaker/smart_speaker_cover_api.cpp",
                                     "lunaros/src/device_api/projections/device_projection_json.cpp"]]
    subprocess.run(command + ["-o", str(binary)], check=True)
    subprocess.run([str(binary)], cwd=tmp, check=True)
