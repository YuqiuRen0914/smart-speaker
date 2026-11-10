"""Compile the production decoder wrapper against a no-progress decoder double."""
from pathlib import Path
import subprocess
import tempfile

source = Path(__file__).resolve().parents[1] / 'main/smart_speaker/smart_speaker_mp3_guard.cpp'
with tempfile.TemporaryDirectory(prefix='speaker-mp3-guard-') as directory:
    root = Path(directory)
    (root / 'freertos').mkdir()
    (root / 'mp3dec.h').write_text('typedef void *HMP3Decoder;\n')
    (root / 'freertos/FreeRTOS.h').write_text('')
    (root / 'freertos/task.h').write_text('void vTaskDelay(unsigned);\n')
    test = root / 'test.cpp'
    test.write_text(r'''
#include <cassert>
static int result = 0, consumed = 0, yields = 0;
void vTaskDelay(unsigned ticks) { assert(ticks == 1); ++yields; }
extern "C" int __real_MP3Decode(void *, unsigned char **p, int *n, short *, int) {
    *p += consumed; *n -= consumed; return result;
}
extern "C" int __wrap_MP3Decode(void *, unsigned char **, int *, short *, int);
int main() {
    unsigned char data[16] = {}, *p = data; int n = 16; short output[2] = {};
    consumed = 4;
    assert(__wrap_MP3Decode(nullptr, &p, &n, output, 0) == 0);
    assert(p == data + 4 && n == 12 && yields == 0);
    result = -6; consumed = 0;
    assert(__wrap_MP3Decode(nullptr, &p, &n, output, 0) == -6);
    assert(p == data + 5 && n == 11 && yields == 1);
    consumed = 2;
    __wrap_MP3Decode(nullptr, &p, &n, output, 0);
    assert(p == data + 7 && n == 9 && yields == 2);
    consumed = 0; n = 0;
    __wrap_MP3Decode(nullptr, &p, &n, output, 0);
    assert(p == data + 7 && n == 0 && yields == 3);
}
''')
    binary = root / 'test.exe'
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I' + str(root), str(source), str(test), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('MP3 decoder guard: success, no-progress error, consumed error and EOF passed')
