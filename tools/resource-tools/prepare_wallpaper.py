#!/usr/bin/env python3
"""Create the canonical 240x240 24-bit BMP accepted by this device."""
from __future__ import annotations

import argparse
import hashlib
import io
import json
from pathlib import Path
import tempfile

SIDE = 240


def prepare(source: Path) -> bytes:
    try:
        from PIL import Image, ImageOps
    except ImportError as error:
        raise RuntimeError("install tools/resource-tools/requirements.txt first") from error
    with Image.open(source) as image:
        image = ImageOps.exif_transpose(image).convert("RGB")
        width, height = image.size
        scale = max(SIDE / width, SIDE / height)
        image = image.resize((round(width * scale), round(height * scale)), Image.Resampling.LANCZOS)
        left = (image.width - SIDE) // 2
        top = (image.height - SIDE) // 2
        image = image.crop((left, top, left + SIDE, top + SIDE))
        stream = io.BytesIO()
        image.save(stream, format="BMP", bits=24)
        data = stream.getvalue()
    if len(data) != 54 + SIDE * SIDE * 3 or data[:2] != b"BM":
        raise RuntimeError("image backend emitted a non-canonical BMP")
    return data


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
        temp_path = Path(temporary.name)
    temp_path.replace(args.output)
    print(json.dumps({"output": str(args.output), "width": SIDE, "height": SIDE,
                      "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
