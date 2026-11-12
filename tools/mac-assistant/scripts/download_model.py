"""Download pinned Feiyu release files, atomically publishing verified bytes."""
import concurrent.futures
import hashlib
import json
import pathlib
import subprocess
import urllib.parse

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / '.runtime' / 'FeiyuVoice-CosyVoice3'
INVENTORY = ROOT / '.runtime/model-source-inventory.json'
if INVENTORY.exists():
    FILES = json.loads(INVENTORY.read_text())
else:
    raw = subprocess.check_output(['curl', '-L', '--fail', '--silent', '--show-error',
        '--connect-timeout', '15', '--max-time', '60',
        'https://modelscope.cn/api/v1/models/Esmerubphire/FeiyuVoice-CosyVoice3/repo/files?Revision=master&Recursive=true'])
    FILES = json.loads(raw)['Data']['Files']


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(8 * 1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def fetch(row):
    path = OUT / row['Path']
    path.resolve().relative_to(OUT.resolve())
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.stat().st_size == row['Size'] and digest(path) == row['Sha256']:
        print('verified existing ' + row['Path'], flush=True)
        return
    url = ('https://modelscope.cn/models/Esmerubphire/FeiyuVoice-CosyVoice3/resolve/'
           + row['Revision'] + '/' + urllib.parse.quote(row['Path']))
    temporary = path.with_name(path.name + '.partial')
    print('downloading ' + row['Path'] + ' ' + str(row['Size']), flush=True)
    subprocess.run(['curl', '-L', '--fail', '--silent', '--show-error', '--retry', '3',
                    '--connect-timeout', '15', '--max-time', '1800', '-C', '-',
                    url, '-o', str(temporary)], check=True)
    if temporary.stat().st_size != row['Size'] or digest(temporary) != row['Sha256']:
        raise RuntimeError('size/SHA256 mismatch: ' + row['Path'])
    temporary.replace(path)
    print('verified ' + row['Path'], flush=True)


if __name__ == '__main__':
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT.parent / 'model-source-inventory.json').write_text(json.dumps(FILES, indent=2))
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        list(pool.map(fetch, [row for row in FILES if row['Type'] == 'blob']))
    print('ALL MODEL FILES VERIFIED', flush=True)
