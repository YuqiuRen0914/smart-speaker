"""Fetch original WeText FST files and verify pinned Git LFS digests."""
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
REVISION = '887eec2274a4502b9252185d0e6ee4614687c475'
target = ROOT / '.runtime/wetext'
if not (target / '.git').exists():
    subprocess.run(['git', '-c', 'filter.lfs.process=', '-c', 'filter.lfs.required=false',
                    'clone', '--no-checkout', 'https://www.modelscope.cn/pengzhendong/wetext.git',
                    str(target)], check=True)
receipt = {'revision': REVISION, 'files': []}
for lang in ('zh', 'en'):
    for name in ('tagger.fst', 'verbalizer.fst'):
        relative = f'{lang}/tn/{name}'
        pointer = subprocess.check_output(['git', '-C', str(target), 'show',
                                            REVISION + ':' + relative], text=True)
        digest = pointer.split('oid sha256:')[1].splitlines()[0]
        size = int(pointer.split('size ')[1].splitlines()[0])
        path = target / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        url = f'https://modelscope.cn/models/pengzhendong/wetext/resolve/{REVISION}/{relative}'
        with path.open('rb') if path.exists() else open('/dev/null', 'rb') as stream:
            actual = hashlib.file_digest(stream, 'sha256').hexdigest()
        if actual != digest:
            part = path.with_suffix('.partial')
            subprocess.run(['curl', '-fLsS', '--retry', '3', '--max-time', '120',
                            url, '-o', str(part)], check=True)
            with part.open('rb') as stream:
                actual = hashlib.file_digest(stream, 'sha256').hexdigest()
            if actual != digest or part.stat().st_size != size:
                raise RuntimeError('WeText integrity mismatch: ' + relative)
            part.replace(path)
        receipt['files'].append({'path': relative, 'sha256': digest, 'bytes': size, 'url': url})
        print('VERIFIED', relative, flush=True)
(ROOT / '.runtime/wetext-receipt.json').write_text(json.dumps(receipt, indent=2))
