#!/usr/bin/env python3
"""Build the complete on-device dsh-pet pack from the pinned local archive."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[2]
ARCHIVE = ROOT / "components/lunaredu_character_assets/third_party/dsh-pet"
CORE_CLIPS = (
    ("idle", "待机呼吸休闲"),
    ("tap", "点击回应-开心跃动"),
    ("drag", "被鼠标拖拽悬空反馈"),
    ("think", "工作状态-思考冒泡"),
    ("talk", "碎碎念-对屏碎碎念"),
    ("sleep-start", "睡觉-开始"),
    ("sleep-loop", "睡觉-循环"),
    ("sleep-end", "睡觉-起来"),
)
SIDE, FPS = 120, 12
# One crop keeps the character's feet and visual scale aligned across clips.
CORE_CROP = (320, 320, 160, 14)
FRAME_BYTES = SIDE * SIDE * 2
MAX_FRAMES = 240
FRAMES_PER_SHARD = 12


def clip_catalog() -> list[tuple[str, str]]:
    catalog = json.loads((ARCHIVE / "animation-catalog.json").read_text(encoding="utf-8"))
    names = {clip["name"] for clip in catalog["clips"]}
    core_names = {name for _, name in CORE_CLIPS}
    if len(names) != 114 or len(catalog["clips"]) != len(names) or not core_names <= names:
        raise ValueError("expected the 114 pinned clips and all core animations")
    return [*CORE_CLIPS, *((name, name) for name in sorted(names - core_names))]


def crop_for_clip(source: Path) -> tuple[int, int, int, int]:
    # Keep the first pack's anchor and expand only when a sampled pose or prop
    # needs more space. The final square is padded after crop when necessary.
    result = subprocess.run([
        "ffmpeg", "-v", "info", "-filter_threads", "1", "-c:v", "libvpx-vp9", "-threads", "2",
        "-i", str(source), "-an", "-vf", f"fps={FPS},alphaextract,bbox=min_val=12",
        "-f", "null", "-",
    ], check=True, capture_output=True, text=True)
    bounds = [tuple(map(int, match)) for match in
              re.findall(r"x1:(\d+) x2:(\d+) y1:(\d+) y2:(\d+)", result.stderr)]
    if not bounds:
        raise ValueError(f"{source.name}: no visible alpha in sampled frames")
    x0 = max(0, min(160, min(b[0] for b in bounds) - 4)) & ~1
    x1 = min(640, max(480, max(b[1] for b in bounds) + 5) + 1) & ~1
    y0 = max(0, min(14, min(b[2] for b in bounds) - 4)) & ~1
    y1 = min(360, max(334, max(b[3] for b in bounds) + 5) + 1) & ~1
    return x1 - x0, y1 - y0, x0, y0


def convert(source: Path, bounds: tuple[int, int, int, int]) -> tuple[bytes, int]:
    w, h, x, y = bounds
    square = max(w, h)
    graph = (
        f"[0:v]fps={FPS},format=rgba,crop={w}:{h}:{x}:{y},"
        f"pad={square}:{square}:(ow-iw)/2:(oh-ih)/2:color=black@0,"
        f"scale={SIDE}:{SIDE}:flags=lanczos[s];"
        f"color=c=black:s={SIDE}x{SIDE}:r={FPS}[b];"
        "[b][s]overlay=shortest=1:format=rgb,format=rgb565le"
    )
    raw = subprocess.check_output([
        "ffmpeg", "-v", "error", "-c:v", "libvpx-vp9", "-threads", "2",
        "-i", str(source), "-an", "-filter_complex_threads", "1",
        "-filter_complex", graph, "-c:v", "rawvideo", "-threads", "1",
        "-pix_fmt", "rgb565le", "-f", "rawvideo", "pipe:1",
    ])
    count, remainder = divmod(len(raw), FRAME_BYTES)
    if remainder or not 0 < count <= MAX_FRAMES:
        raise ValueError(f"{source.name}: unexpected output length {len(raw)}")
    return raw, count


def verify(staging: Path, ordered: list[tuple[str, str]]) -> None:
    folder = staging / "characters" / "dsh-pet"
    info = json.loads((folder / "pet-v2.json").read_text(encoding="utf-8"))
    index = (folder / "pet-v2.idx").read_bytes()
    prefix = struct.pack("<4sBBBBI", b"DSHP", 2, len(ordered), SIDE, FPS, FRAMES_PER_SHARD)
    if len(index) != len(prefix) + 2 * len(ordered) or not index.startswith(prefix):
        raise ValueError("invalid pack index")
    counts = struct.unpack_from(f"<{len(ordered)}H", index, len(prefix))
    if len(info["clips"]) != len(ordered):
        raise ValueError("manifest clip count mismatch")
    digest, size, shards = hashlib.sha256(index), len(index), 0
    record_size = FRAME_BYTES + 4
    for clip_id, (count, (_, name), clip) in enumerate(zip(counts, ordered, info["clips"])):
        if not 0 < count <= MAX_FRAMES or (clip["id"], clip["name"], clip["frames"]) != (clip_id, name, count):
            raise ValueError(f"index mismatch: {clip_id}")
        clip_digest = hashlib.sha256()
        for first in range(0, count, FRAMES_PER_SHARD):
            path = folder / "frames-v2" / f"{clip_id:03d}" / f"{first // FRAMES_PER_SHARD:02d}.bin"
            data = path.read_bytes()
            if len(data) != min(count - first, FRAMES_PER_SHARD) * record_size:
                raise ValueError(f"shard size mismatch: {path}")
            for start in range(0, len(data), record_size):
                frame = data[start + 4:start + record_size]
                if zlib.crc32(frame) != struct.unpack_from("<I", data, start)[0]:
                    raise ValueError(f"frame checksum mismatch: {path}, record {start // record_size}")
                clip_digest.update(frame)
            digest.update(data)
            size += len(data)
            shards += 1
        if clip_digest.hexdigest() != clip["frameSha256"]:
            raise ValueError(f"clip digest mismatch: {name}")
    if (digest.hexdigest(), size, shards) != (info["sha256"], info["bytes"], info["shardCount"]):
        raise ValueError("pack digest or size mismatch")
    print(f"Verified {len(counts)} clips / {sum(counts)} frames / {shards} shards, SHA-256 {digest.hexdigest()}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sd_staging", type=Path, nargs="?", help="local staging directory (not the device)")
    parser.add_argument("--write-catalog", action="store_true", help="regenerate the firmware name table")
    parser.add_argument("--verify", action="store_true", help="verify a staged or copied pack without writing files")
    args = parser.parse_args()
    ordered = clip_catalog()
    name_table = ROOT / "main/product/pet_action_names.inc"
    names = "// Generated by prepare_dsh_pet.py --write-catalog. IDs match the v2 pack.\n" + "".join(
        json.dumps(name, ensure_ascii=False) + ",\n" for _, name in ordered
    )
    if args.write_catalog:
        name_table.write_text(names, encoding="utf-8")
    if args.sd_staging is None:
        if not args.write_catalog or args.verify:
            parser.error("sd_staging is required to build the pack")
        return
    if name_table.read_text(encoding="utf-8") != names:
        raise ValueError("firmware names are out of sync; rerun --write-catalog")
    if args.verify:
        verify(args.sd_staging, ordered)
        return
    sources = json.loads((ARCHIVE / "source-manifest.json").read_text(encoding="utf-8"))
    hashes = {entry["path"]: entry["sha256"] for entry in sources["files"]}
    target = args.sd_staging / "characters" / "dsh-pet" / "pet-v2.idx"
    target.parent.mkdir(parents=True, exist_ok=True)
    clips = []
    with tempfile.TemporaryDirectory(dir=target.parent) as temporary:
        stage = Path(temporary)
        shard_paths = []
        for index, (role, name) in enumerate(ordered):
            relative = f"dsh-pet/assets/webm/{name}.webm"
            source = ARCHIVE / "upstream" / relative
            source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
            if source_hash != hashes[relative]:
                raise ValueError(f"source digest mismatch: {source}")
            bounds = CORE_CROP if index < 5 else crop_for_clip(source)
            raw, count = convert(source, bounds)
            folder = Path("frames-v2") / f"{index:03d}"
            (stage / folder).mkdir(parents=True)
            for first in range(0, count, FRAMES_PER_SHARD):
                shard_path = folder / f"{first // FRAMES_PER_SHARD:02d}.bin"
                with (stage / shard_path).open("wb") as out:
                    for i in range(first, min(count, first + FRAMES_PER_SHARD)):
                        frame = raw[i * FRAME_BYTES:(i + 1) * FRAME_BYTES]
                        out.write(struct.pack("<I", zlib.crc32(frame)))
                        out.write(frame)
                shard_paths.append(shard_path)
            w, h, x, y = bounds
            clips.append({"id": index, "name": name, "role": role if index < len(CORE_CLIPS) else None,
                          "source": relative, "sha256": source_hash, "frames": count,
                          "cropXYWH": [x, y, w, h], "frameSha256": hashlib.sha256(raw).hexdigest()})
            if (index + 1) % 20 == 0:
                print(f"Converted {index + 1}/{len(ordered)} clips", flush=True)
        index_bytes = (struct.pack("<4sBBBBI", b"DSHP", 2, len(ordered), SIDE, FPS, FRAMES_PER_SHARD)
                       + struct.pack(f"<{len(ordered)}H", *(clip["frames"] for clip in clips)))
        (stage / target.name).write_bytes(index_bytes)
        digest = hashlib.sha256(index_bytes)
        size = len(index_bytes)
        for shard_path in shard_paths:
            shard = stage / shard_path
            data = shard.read_bytes()
            digest.update(data)
            size += len(data)
            published = target.parent / shard_path
            published.parent.mkdir(parents=True, exist_ok=True)
            shard.replace(published)
        sha256 = digest.hexdigest()
        (stage / target.name).replace(target)
    manifest = {
        "format": 2, "sourceCommit": sources["commit"],
        "fps": FPS, "size": [SIDE, SIDE], "pixelFormat": "RGB565LE over black",
        "bytes": size, "sha256": sha256, "clips": clips,
        "shardFrames": FRAMES_PER_SHARD, "shardCount": len(shard_paths),
        "digestOrder": "index followed by shards sorted by their relative paths",
        "attribution": "PC2005-cloud/dsh-pet; character: @上善无形 / @ZipZipPipe; CC BY-NC-SA 4.0.",
        "sourceRepository": sources["repository"],
    }
    target.with_suffix(".json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (target.parent / "ATTRIBUTION.txt").write_text(
        "dsh-pet character animation, adapted to cropped 120×120 RGB565 at 12 fps.\n"
        f"Source: {sources['repository']}/tree/{sources['commit']}\n"
        "Original Mingyue: https://space.bilibili.com/4456176\n"
        "DeepSeek whale-tail maid adaptation: https://space.bilibili.com/4168597\n"
        "Character and this adapted animation pack: CC BY-NC-SA 4.0.\n"
        "https://creativecommons.org/licenses/by-nc-sa/4.0/\n", encoding="utf-8")
    print(f"{target}: {size} bytes, {sum(clip['frames'] for clip in clips)} frames, SHA-256 {sha256}")


if __name__ == "__main__":
    main()
