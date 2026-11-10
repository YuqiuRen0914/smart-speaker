"""Embed deterministic gzip bytes while retaining the existing asset symbols."""
import gzip
from pathlib import Path
import sys

source, target = map(Path, sys.argv[1:])
content = gzip.compress(source.read_bytes(), compresslevel=9, mtime=0)
if not target.exists() or target.read_bytes() != content:
    target.write_bytes(content)
