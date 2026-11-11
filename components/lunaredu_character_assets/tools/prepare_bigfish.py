#!/usr/bin/env python3
"""Prepare pinned legacy BigFish PNG frames for the device's 144px pet view.

This tool only stages transparent PNGs for SD/resource packaging. It does not
embed them in firmware or select runtime animation policy.
Requires Pillow from tools/resource-tools/requirements.txt.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image


SOURCE = Path(__file__).resolve().parents[1] / "third_party/dsh-dafeiyu/legacy/dafeiyu"
PINNED = SOURCE.parent / "source-manifest.json"
SIZE = (160, 160)
CLIPS = {
    "idle": ["idle_front/idle_front_238.png"],
    "happy": ["happy/happy_238.png"],
    "angry": ["angry/angry_238.png"],
    "think": ["idle_think/idle_think_238.png"],
    "talk": ["talk/talk_238.png"],
    "head_pat": [f"head_pat/head_pat_238_{i:02}.png" for i in range(6)],
    "poke_react": [f"poke_react/poke_react_238_{i:02}.png" for i in range(4)],
    "tail_react": [f"tail_react/tail_react_238_{i:02}.png" for i in range(4)],
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def prepare(output: Path) -> dict:
    pinned = json.loads(PINNED.read_text(encoding="utf-8"))
    checksums = {entry["path"]: entry for entry in pinned["verifiedFiles"]}
    output.mkdir(parents=True, exist_ok=True)
    frames = []
    for clip, paths in CLIPS.items():
        for index, relative in enumerate(paths):
            source = SOURCE / relative
            raw = source.read_bytes()
            if relative not in checksums or len(raw) != checksums[relative]["size"] or sha256(raw) != checksums[relative]["sha256"]:
                raise ValueError(f"source asset does not match pinned manifest: {source}")
            with Image.open(source) as opened:
                image = opened.convert("RGBA")
                image.thumbnail(SIZE, Image.Resampling.LANCZOS)
                canvas = Image.new("RGBA", SIZE, (0, 0, 0, 0))
                canvas.alpha_composite(image, ((SIZE[0] - image.width) // 2,
                                               (SIZE[1] - image.height) // 2))
                target = output / clip / f"{index:02}.png"
                target.parent.mkdir(parents=True, exist_ok=True)
                canvas.save(target, format="PNG", optimize=True)
                # The display task's existing RGB565 tile writer paints onto
                # a black character region. Bake alpha against black once so
                # it never needs to decode PNG or allocate a full RGB surface
                # on each animation tick. Bytes are little-endian RGB565,
                # matching the ESP32's uint16_t tile representation.
                rgb565 = bytearray(SIZE[0] * SIZE[1] * 2)
                for pixel_index, (red, green, blue, _alpha) in enumerate(canvas.getdata()):
                    value = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
                    rgb565[pixel_index * 2] = value & 0xFF
                    rgb565[pixel_index * 2 + 1] = value >> 8
                raw_target = target.with_suffix(".rgb565")
                raw_target.write_bytes(rgb565)
            frames.append({
                "clip": clip,
                "index": index,
                "source": relative,
                "sourceSha256": sha256(raw),
                "output": target.relative_to(output).as_posix(),
                "outputSha256": sha256(target.read_bytes()),
                "rgb565": raw_target.relative_to(output).as_posix(),
                "rgb565Sha256": sha256(rgb565),
                "rgb565Bytes": len(rgb565),
                "width": SIZE[0],
                "height": SIZE[1],
                "format": "RGBA PNG and little-endian RGB565 over black",
            })
    manifest = {
        "formatVersion": 1,
        "characterId": "dafeiyu",
        "sourceRepository": "https://github.com/QCYTSN/dsh-dafeiyu",
        "sourceCommit": "084323845ab82b68d5388d5c638bec09d8544a8a",
        "licenseNotice": "See third_party/dsh-dafeiyu/ASSET_LICENSE.md; local redistribution authorization supplied by project owner.",
        "frameCanvas": list(SIZE),
        "fit": "contain, centered, transparent background, Pillow LANCZOS; RGB565 alpha composited against black",
        "frames": frames,
    }
    (output / "bigfish-manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="resource staging directory (normally SD package input)")
    args = parser.parse_args()
    manifest = prepare(args.output)
    print(f"prepared {len(manifest['frames'])} verified-source frames in {args.output}")


if __name__ == "__main__":
    main()
