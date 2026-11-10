"""Local ESP-IDF build tasks: durable records, directory lock and safe cancellation."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import queue
import re
import signal
import subprocess
import sys
import threading
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
STATE = ROOT / '.build/tasks'


def now():
    return datetime.now(timezone.utc).isoformat()


def save(record):
    filename = STATE / (record['id'] + '.json')
    temporary = filename.with_suffix('.tmp')
    temporary.write_text(json.dumps(record, ensure_ascii=False, indent=2) + '\n')
    temporary.replace(filename)


def task_file(identity):
    return STATE / (str(uuid.UUID(identity)) + '.json')


def sample_tree(pid):
    try:
        result = subprocess.run(['ps', '-axo', 'pid=,ppid=,%cpu=,rss='],
                                text=True, capture_output=True, check=True, timeout=3)
        rows = [row.split() for row in result.stdout.splitlines()]
        members = {pid}
        while True:
            following = members | {int(p) for p, parent, *_ in rows if int(parent) in members}
            if following == members:
                break
            members = following
        selected = [row for row in rows if int(row[0]) in members]
        if not selected:
            return None
        return {'at': now(), 'cpu_percent': sum(float(row[2]) for row in selected),
                'rss_bytes': sum(int(row[3]) * 1024 for row in selected)}
    except (OSError, ValueError, subprocess.SubprocessError):
        return None  # Unsupported sampling is unknown, never reported as zero.


def request_stop(child):
    if child.poll() is None:
        if os.name == 'posix':
            os.killpg(child.pid, signal.SIGINT)
        else:
            child.send_signal(signal.CTRL_BREAK_EVENT)


def execute(command, record, stop_file, environment=None):
    messages = queue.Queue()
    options = {'start_new_session': True} if os.name == 'posix' else {'creationflags': subprocess.CREATE_NEW_PROCESS_GROUP}
    child = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             text=True, bufsize=1, env=environment, **options)
    record['pid'] = child.pid

    def read():
        for line in child.stdout:
            messages.put(line)
        messages.put(None)

    reader = threading.Thread(target=read, daemon=True)
    reader.start()
    finished = cancelled = False
    sampled = 0.0
    try:
        with (STATE / (record['id'] + '.log')).open('a') as log:
            while not finished:
                try:
                    if stop_file.exists() and not cancelled:
                        request_stop(child)
                        cancelled = True
                        record['status'] = 'cancelling'
                        save(record)
                    line = messages.get(timeout=0.2)
                    if line is None:
                        finished = True
                    else:
                        print(line, end='', flush=True)
                        log.write(line)
                        log.flush()
                        progress = re.search(r'\[(\d+)/(\d+)\]', line)
                        if progress:
                            record['ninja'] = {'completed': int(progress[1]), 'total': int(progress[2])}
                        phase = ('validate' if ('check_sizes.py' in line and 'bootloader' not in line) or 'Project build complete' in line
                             else 'link' if re.search(r'Linking .* executable .*lunaredu-smart-speaker\.elf', line)
                             else 'compile' if progress and record.get('phase') not in ('link', 'validate') else None)
                        if phase and record.get('phase') != phase:
                            record['phase'] = phase
                            record.setdefault('phases', []).append({'phase': phase, 'at': now()})
                except queue.Empty:
                    pass
                except KeyboardInterrupt:
                    request_stop(child)
                    cancelled = True
                    record['status'] = 'cancelling'
                if time.monotonic() - sampled >= 2:
                    sample = sample_tree(child.pid)
                    record['sample_at'] = now()
                    record['sample'] = sample
                    if sample:
                        record['peak_rss_bytes'] = max(record.get('peak_rss_bytes', 0), sample['rss_bytes'])
                    save(record)
                    sampled = time.monotonic()
        code = child.wait()  # Hold the directory lock until the owned process exits.
        reader.join()
        child.stdout.close()
    except BaseException:
        request_stop(child)
        child.wait()
        reader.join()
        child.stdout.close()
        raise
    return code, cancelled


def build(jobs, resume=None):
    STATE.mkdir(parents=True, exist_ok=True)
    import fcntl  # The monitored entry supports macOS/Linux; idf.py also supports Windows.
    with (ROOT / '.build/firmware.lock').open('a+') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise SystemExit('Another build owns this directory; inspect tools/build.py list')
        idf = Path(os.environ.get('IDF_PATH', '')) / 'tools/idf.py'
        if not idf.is_file():
            raise SystemExit('Load ESP-IDF 6.0.1 export.sh before building')
        record = {'id': str(uuid.uuid4()), 'status': 'running', 'started_at': now(),
                  'executor': 'local', 'phase': 'configure', 'phases': [{'phase': 'configure', 'at': now()}],
                  'jobs': jobs, 'resumes': resume, 'sample': None}
        save(record)
        print('Build task: ' + record['id'], flush=True)
        command = [sys.executable, str(idf), '-B', str(ROOT / '.build/firmware'),
                   '-D', 'SDKCONFIG=' + str(ROOT / '.build/firmware/sdkconfig'),
                   '-D', 'IDF_TARGET=esp32s3', 'build']
        environment = dict(os.environ, IDF_PY_BUILD_JOBS=str(jobs), NINJA_STATUS='[%f/%t] ')
        try:
            code, cancelled = execute(command, record, STATE / (record['id'] + '.cancel'), environment)
            record.update(status='cancelled' if cancelled else 'succeeded' if code == 0 else 'failed',
                          exit_code=code, finished_at=now())
            if code == 0 and not cancelled:
                binary = ROOT / '.build/firmware/lunaredu-smart-speaker.bin'
                record['artifact'] = {'path': str(binary.relative_to(ROOT)), 'bytes': binary.stat().st_size,
                                      'sha256': hashlib.sha256(binary.read_bytes()).hexdigest()}
            save(record)
            return 130 if cancelled else code
        except Exception as error:
            record.update(status='failed', error=str(error), finished_at=now())
            save(record)
            raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('build', 'list', 'status', 'cancel', 'resume'), nargs='?', default='build')
    parser.add_argument('id', nargs='?')
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    if args.action == 'build':
        return build(args.jobs)
    if args.action == 'list':
        print(json.dumps([json.loads(f.read_text()) for f in sorted(STATE.glob('*.json'))], indent=2))
        return 0
    if not args.id:
        parser.error('A task UUID is required')
    file = task_file(args.id)
    record = json.loads(file.read_text())
    if args.action == 'status':
        print(json.dumps(record, indent=2))
    elif args.action == 'cancel':
        if record['status'] not in ('running', 'cancelling'):
            parser.error('Task has already exited')
        file.with_suffix('.cancel').touch()
        print('Cancellation requested; the owner retains its lock until exit')
    elif args.action == 'resume':
        if record['status'] in ('running', 'cancelling'):
            parser.error('Wait for the previous task to exit before resuming')
        return build(args.jobs, record['id'])
    return 0


if __name__ == '__main__':
    sys.exit(main())
