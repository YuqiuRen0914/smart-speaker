"""Validate the actual ESP-SR wake model publication with its device reader."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('seed_storage', ROOT/'tools/assets/seed-storage.py')
assets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(assets)
model = ROOT/'managed_components/espressif__esp-sr/model/wakenet_model/wn9s_nihaoxiaozhi'
source = r'''
#include "delivery/resource/managed_asset_formats.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>
using lunaredu::lunaros::internal::managed_asset_format_valid;
int main(int argc,char **argv) {
  assert(argc==2);
  std::ifstream input(argv[1],std::ios::binary);
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(input)),{});
  assert(managed_asset_format_valid("model",data.data(),data.size()));
  assert(!managed_asset_format_valid("model",data.data(),data.size()-1));
  data[0]=0;assert(!managed_asset_format_valid("model",data.data(),data.size()));
}
'''
with tempfile.TemporaryDirectory(prefix='speaker-wake-model-') as folder:
    root = Path(folder)
    (root/'model.bin').write_bytes(assets.pack_wake_model(model))
    (root/'test.cpp').write_text(source)
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-O2',
                    '-I'+str(ROOT/'lunaros/src'),str(root/'test.cpp'),'-o',str(root/'test')],check=True)
    subprocess.run([str(root/'test'),str(root/'model.bin')],check=True)
print('Wake model: actual 你好小智 ESP-SR package passes production format validation; truncation rejected')
