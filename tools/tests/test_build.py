"""Exercise persistent task results, directory exclusion, cancellation and resume."""
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('speaker_build', Path(__file__).resolve().parents[1] / 'build.py')
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


class BuildTasks(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='speaker-build-test-')
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.state = self.root / '.build/tasks'
        self.state.mkdir(parents=True)
        self.patches = [patch.object(build, 'ROOT', self.root), patch.object(build, 'STATE', self.state)]
        for item in self.patches:
            item.start()
            self.addCleanup(item.stop)
        self.idf = self.root / 'idf/tools/idf.py'
        self.idf.parent.mkdir(parents=True)
        self.idf.write_text('''import sys
from pathlib import Path
folder=Path(sys.argv[sys.argv.index('-B')+1]);folder.mkdir(parents=True,exist_ok=True)
print('check_sizes.py --offset 0 bootloader.bin',flush=True)
print('[1/4] Building test',flush=True)
print('[2/4] Linking C static library test.a',flush=True)
print('[3/4] Linking CXX executable lunaredu-smart-speaker.elf',flush=True)
print('[4/4] check_sizes.py lunaredu-smart-speaker.bin',flush=True)
(folder/'lunaredu-smart-speaker.bin').write_bytes(b'test-artifact')
''')
        self.environment = patch.dict(os.environ, {'IDF_PATH': str(self.idf.parents[1])})
        self.environment.start()
        self.addCleanup(self.environment.stop)

    def test_success_failure_and_incremental_resume(self):
        self.assertEqual(build.build(2), 0)
        previous = json.loads(next(self.state.glob('*.json')).read_text())
        self.assertEqual(previous['status'], 'succeeded')
        self.assertEqual(previous['artifact']['bytes'], 13)
        self.assertEqual([entry['phase'] for entry in previous['phases']],
                         ['configure', 'compile', 'link', 'validate'])
        self.assertEqual(previous['ninja'], {'completed': 4, 'total': 4})
        self.idf.write_text('raise SystemExit(7)\n')
        self.assertEqual(build.build(2, previous['id']), 7)
        records = [json.loads(file.read_text()) for file in self.state.glob('*.json')]
        failed = next(record for record in records if record['status'] == 'failed')
        self.assertEqual(failed['resumes'], previous['id'])
        self.assertTrue((self.root / '.build/firmware/lunaredu-smart-speaker.bin').is_file())

    def test_lock_excludes_another_worker(self):
        import fcntl
        with (self.root / '.build/firmware.lock').open('a+') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            with self.assertRaises(SystemExit):
                build.build(2)
        self.assertFalse(list(self.state.glob('*.json')))

    def test_cancel_waits_for_the_owned_process(self):
        record = {'id': str(build.uuid.uuid4()), 'status': 'running'}
        stop_file = self.state / (record['id'] + '.cancel')

        def cancel():
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline and not (self.root / 'ready').exists():
                time.sleep(0.02)
            stop_file.touch()

        thread = threading.Thread(target=cancel)
        thread.start()
        script = "from pathlib import Path; import time; Path('ready').touch(); time.sleep(30)"
        code, cancelled = build.execute([sys.executable, '-c', script], record, stop_file)
        thread.join()
        self.assertTrue(cancelled)
        self.assertNotEqual(code, 0)
        self.assertEqual(record['status'], 'cancelling')


if __name__ == '__main__':
    unittest.main()
