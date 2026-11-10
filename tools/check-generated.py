"""Fail closed if checked-in generated code or its authored inputs changed."""
import hashlib
import json
from pathlib import Path
import sys

manifest, root = map(Path, sys.argv[1:])
document = json.loads(manifest.read_text())
for group, base in (('inputs', root), ('outputs', manifest.parent / 'generated')):
    for name, expected in document[group].items():
        source = base / name
        if hashlib.sha256(source.read_bytes()).hexdigest() != expected:
            raise SystemExit(f'Generated port differs: {name}; regenerate and review generated.json')
