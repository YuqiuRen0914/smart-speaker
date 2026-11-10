"""Build the speaker's factory font publication; never modify input assets."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import zlib


def seed(data, output, font):
    data, output, font = data.resolve(), output.resolve(), font.resolve()
    if output.name != 'storage-pool-seed' or output == data or output in data.parents or data in output.parents:
        raise ValueError('Unsafe staging directory')
    output.mkdir(parents=True, exist_ok=True)
    for child in output.iterdir():
        if child.is_dir():
            shutil.rmtree(child)
        else:
            child.unlink()
    shutil.copytree(data, output, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns('__pycache__', '*.pyc', '.DS_Store'))
    for directory in ('user', 'apps', 'managed', 'cache'):
        (output / directory).mkdir(exist_ok=True)
    original = font.read_bytes()
    compressed = zlib.compress(original, 9)
    content = b'LRZ1' + struct.pack('<III', 1, len(original), len(compressed)) + compressed
    assert zlib.decompress(content[16:]) == original
    digest = hashlib.sha256(content).hexdigest()
    (output / 'managed/fonts.bin').write_bytes(content)
    (output / 'managed/fonts.json').write_text(json.dumps({
        'schema': 1, 'ready': True, 'revision': digest[:16], 'url': '',
        'sha256': digest, 'bytes': len(content),
    }), encoding='utf-8')
    shutil.copyfile(font.parent / 'LICENSE.txt', output / 'managed/fonts.LICENSE.txt')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('data', 'output', 'font'):
        parser.add_argument('--' + name, required=True, type=Path)
    args = parser.parse_args()
    seed(args.data, args.output, args.font)
