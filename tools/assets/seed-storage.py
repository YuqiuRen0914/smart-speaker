"""Build factory font and wake-model publications; never modify input assets."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import zlib


def pack_wake_model(directory):
    """ESP-SR model_pack_t, little endian; one explicitly selected public model."""
    directory = directory.resolve()
    if directory.name != 'wn9s_nihaoxiaozhi':
        raise ValueError('Expected the product wake model wn9s_nihaoxiaozhi')
    info = (directory / '_MODEL_INFO_').read_text(encoding='utf-8')
    if '你好小智' not in info:
        raise ValueError('The wake model does not advertise 你好小智')
    files = sorted(directory.iterdir(), key=lambda path: path.name)
    if not files or len(files) > 16 or any(not path.is_file() for path in files):
        raise ValueError('Invalid model file set')
    header_size = 4 + 36 + 40 * len(files)
    header = struct.pack('<I32sI', 1, directory.name.encode('ascii'), len(files))
    payload = bytearray()
    for path in files:
        name = path.name.encode('ascii')
        content = path.read_bytes()
        if len(name) >= 32 or not content:
            raise ValueError('Invalid model file')
        header += struct.pack('<32sII', name, header_size + len(payload), len(content))
        payload.extend(content)
    return header + payload


def publish(output, asset_id, content):
    digest = hashlib.sha256(content).hexdigest()
    (output / f'managed/{asset_id}.bin').write_bytes(content)
    (output / f'managed/{asset_id}.json').write_text(json.dumps({
        'schema': 1, 'ready': True, 'revision': digest[:16], 'url': '',
        'sha256': digest, 'bytes': len(content),
    }), encoding='utf-8')


def seed(data, output, font, wake_model=None, model_license=None):
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
    publish(output, 'fonts', content)
    if wake_model is not None:
        model = pack_wake_model(wake_model)
        publish(output, 'model', model)
        if model_license is None:
            raise ValueError('The model license is required')
        shutil.copyfile(model_license, output / 'managed/model.LICENSE.txt')
    shutil.copyfile(font.parent / 'LICENSE.txt', output / 'managed/fonts.LICENSE.txt')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('data', 'output', 'font'):
        parser.add_argument('--' + name, required=True, type=Path)
    parser.add_argument('--wake-model', type=Path)
    parser.add_argument('--model-license', type=Path)
    args = parser.parse_args()
    seed(args.data, args.output, args.font, args.wake_model, args.model_license)
