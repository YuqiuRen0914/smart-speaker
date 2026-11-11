import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('seed_storage', ROOT/'tools/assets/seed-storage.py')
assets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(assets)


class SeedStorageTests(unittest.TestCase):
    def model(self, root):
        model = root/'wn9s_nihaoxiaozhi'
        model.mkdir()
        (model/'_MODEL_INFO_').write_text('wakenet_你好小智', encoding='utf-8')
        (model/'wn9_data').write_bytes(bytes(range(256)))
        return model

    def test_publication_retains_seed_and_contains_verified_model(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            data = root/'data'; data.mkdir(); (data/'kept.txt').write_text('seed')
            font = root/'font.ttf'; font.write_bytes(b'font-fixture')
            license = root/'LICENSE.txt'; license.write_text('test-license')
            output = root/'storage-pool-seed'
            model = self.model(root)
            assets.seed(data, output, font, model, license)
            record = json.loads((output/'managed/model.json').read_text())
            content = (output/'managed/model.bin').read_bytes()
            self.assertTrue(record['ready'])
            self.assertEqual(record['bytes'], len(content))
            self.assertEqual(record['sha256'], hashlib.sha256(content).hexdigest())
            self.assertEqual((output/'kept.txt').read_text(), 'seed')
            self.assertEqual((output/'managed/model.LICENSE.txt').read_text(), 'test-license')
            count, name, files = struct.unpack_from('<I32sI', content)
            self.assertEqual(count, 1)
            self.assertEqual(name.rstrip(b'\0').decode(), model.name)
            self.assertEqual(files, 2)
            for index in range(files):
                name, offset, size = struct.unpack_from('<32sII', content, 40+40*index)
                self.assertEqual(content[offset:offset+size], (model/name.rstrip(b'\0').decode()).read_bytes())
            self.assertEqual(assets.pack_wake_model(model), content)

    def test_wrong_word_or_nested_payload_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            model = self.model(Path(folder))
            (model/'_MODEL_INFO_').write_text('another word')
            with self.assertRaises(ValueError): assets.pack_wake_model(model)
            (model/'_MODEL_INFO_').write_text('你好小智', encoding='utf-8')
            (model/'nested').mkdir()
            with self.assertRaises(ValueError): assets.pack_wake_model(model)

    def test_unsafe_staging_is_rejected_before_mutating_input(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            data = root/'storage-pool-seed'; data.mkdir()
            sentinel = data/'kept.txt'; sentinel.write_text('original')
            font = root/'font.ttf'; font.write_bytes(b'font-fixture')
            with self.assertRaises(ValueError): assets.seed(data, data, font)
            self.assertEqual(sentinel.read_text(), 'original')
