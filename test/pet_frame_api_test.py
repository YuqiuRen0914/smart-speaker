"""Compile the production BigFish upload handler and resource owner for fault tests."""
from pathlib import Path
import os
import subprocess
import tempfile

app = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="speaker-pet-frame-api-") as tmp:
    binary = Path(tmp) / "test.exe"
    command = [os.environ.get("CXX", "g++"), "-std=c++17", "-Wall", "-Wextra", "-Werror"]
    command += ["-I" + str(app / p) for p in [
        "main", "main/smart_speaker", "main/product", "lunaros/include",
        "components_device/media/lunaredu_device_sdcard/include",
        "managed_components/bblanchon__arduinojson/src"]]
    command += [str(app / p) for p in [
        "test/pet_frame_api_test.cpp", "main/smart_speaker/smart_speaker_pet_frame_api.cpp",
        "main/product/desktop_resources.cpp", "lunaros/src/device_api/projections/device_projection_json.cpp"]]
    subprocess.run(command + ["-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
