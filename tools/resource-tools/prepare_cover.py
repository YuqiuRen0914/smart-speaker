#!/usr/bin/env python3
"""Prepare a canonical 240x240 RGB BMP for a song cover sidecar."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import tempfile

from prepare_wallpaper import prepare


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    data = prepare(args.source)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=args.output.parent, prefix=args.output.name + ".",
                                     suffix=".tmp", delete=False) as temporary:
        temporary.write(data)
        temporary_path = Path(temporary.name)
    temporary_path.replace(args.output)
    print(json.dumps({"output": str(args.output), "width": 240, "height": 240,
                      "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
