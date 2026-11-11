#!/usr/bin/env python3
"""Verify the pinned dsh-pet archive; --fetch restores missing files only.

Uses Python's standard library and curl with normal TLS verification. Existing
files are never overwritten. No device access, conversion, or firmware build.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import tarfile
import tempfile


ROOT = Path(__file__).resolve().parents[2]
ARCHIVE = ROOT / "components/lunaredu_character_assets/third_party/dsh-pet"
REPOSITORY = "https://github.com/PC2005-cloud/dsh-pet"


def validate(data: bytes, entry: dict) -> None:
    if len(data) != entry["size"]:
        raise ValueError("byte length mismatch")
    blob = b"blob " + str(len(data)).encode("ascii") + b"\0" + data
    if hashlib.sha1(blob).hexdigest() != entry["gitBlobSha1"]:
        raise ValueError("Git blob SHA-1 mismatch")
    if hashlib.sha256(data).hexdigest() != entry["sha256"]:
        raise ValueError("SHA-256 mismatch")


def verify_entry(entry: dict) -> None:
    target = ARCHIVE / "upstream" / entry["path"]
    if target.exists():
        validate(target.read_bytes(), entry)
        return
    raise ValueError("missing file (use --fetch to restore)")


def restore_missing(entries: list[dict], commit: str) -> None:
    missing = [entry for entry in entries if not (ARCHIVE / "upstream" / entry["path"]).exists()]
    if not missing:
        return
    # One public source snapshot avoids hundreds of HTTPS connections. Only
    # allowlisted regular files are read; no tar extraction or Git checkout.
    url = "https://codeload.github.com/PC2005-cloud/dsh-pet/tar.gz/" + commit
    with tempfile.TemporaryDirectory(prefix="speaker-dsh-pet-") as temporary_dir:
        snapshot = Path(temporary_dir) / "source.tar.gz"
        subprocess.run([
            "curl", "--fail", "--location", "--silent", "--show-error",
            "--proto", "=https", "--proto-redir", "=https",
            "--connect-timeout", "15", "--max-time", "300", "--retry", "2",
            "--max-filesize", str(256 * 1024 * 1024), "--output", str(snapshot), url,
        ], check=True, capture_output=True)
        with tarfile.open(snapshot, "r:gz") as archive:
            for entry in missing:
                member = archive.getmember("dsh-pet-" + commit + "/" + entry["path"])
                if not member.isfile() or member.size != entry["size"]:
                    raise ValueError("invalid snapshot member: " + entry["path"])
                with archive.extractfile(member) as source:
                    data = source.read(entry["size"] + 1)
                validate(data, entry)
                target = ARCHIVE / "upstream" / entry["path"]
                target.parent.mkdir(parents=True, exist_ok=True)
                with tempfile.NamedTemporaryFile(dir=target.parent, suffix=".download") as temporary:
                    temporary.write(data)
                    temporary.flush()
                    # Atomic, exclusive publication preserves existing files.
                    os.link(temporary.name, target)


def write_catalog(manifest: dict) -> dict:
    def probe(entry: dict) -> dict:
        path = ARCHIVE / "upstream" / entry["path"]
        result = json.loads(subprocess.check_output([
            "ffprobe", "-v", "error", "-count_frames", "-select_streams", "v:0",
            "-show_entries", "stream=codec_name,width,height,avg_frame_rate,nb_read_frames:stream_tags:format=duration",
            "-of", "json", str(path),
        ], stderr=subprocess.PIPE))
        stream = result["streams"][0]
        tags = {key.lower(): value for key, value in stream.get("tags", {}).items()}
        return {
            "name": path.stem, "path": "upstream/" + entry["path"],
            "sha256": entry["sha256"], "bytes": entry["size"],
            "durationSeconds": float(result["format"]["duration"]),
            "frameCount": int(stream["nb_read_frames"]), "codec": stream["codec_name"],
            "width": stream["width"], "height": stream["height"],
            "frameRate": stream["avg_frame_rate"], "alphaModeTag": tags.get("alpha_mode"),
        }
    entries = [entry for entry in manifest["files"] if entry["kind"] == "animation"]
    with ThreadPoolExecutor(max_workers=2) as pool:
        clips = list(pool.map(probe, entries))
    catalog = {
        "formatVersion": 1, "sourceCommit": manifest["commit"],
        "scope": "All WebM files in the pinned tree, unmodified. ffprobe frame counts and alpha metadata are not ESP32 playback or visual alpha verification.",
        "animationCount": len(clips), "totalFrames": sum(clip["frameCount"] for clip in clips),
        "totalDurationSeconds": round(sum(clip["durationSeconds"] for clip in clips), 3),
        "clips": clips,
    }
    (ARCHIVE / "animation-catalog.json").write_text(json.dumps(catalog, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return {key: catalog[key] for key in ("animationCount", "totalFrames", "totalDurationSeconds")}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fetch", action="store_true", help="download missing pinned files; never overwrite local files")
    parser.add_argument("--catalog", action="store_true", help="rebuild video metadata catalog with ffprobe (optional host tool)")
    args = parser.parse_args()
    manifest = json.loads((ARCHIVE / "source-manifest.json").read_text(encoding="utf-8"))
    if manifest["repository"] != REPOSITORY or not re.fullmatch(r"[0-9a-f]{40}", manifest["commit"]):
        raise ValueError("unexpected repository or invalid pinned commit")
    seen = set()
    for entry in manifest["files"]:
        path = PurePosixPath(entry["path"])
        if path.is_absolute() or ".." in path.parts or "\\" in entry["path"] or entry["path"] in seen:
            raise ValueError("invalid or duplicate archive path")
        seen.add(entry["path"])
        if not re.fullmatch(r"[0-9a-f]{64}", entry["sha256"]) or not re.fullmatch(r"[0-9a-f]{40}", entry["gitBlobSha1"]):
            raise ValueError("invalid checksum")
    errors = []
    if args.fetch:
        restore_missing(manifest["files"], manifest["commit"])
    with ThreadPoolExecutor(max_workers=4) as pool:
        pending = {pool.submit(verify_entry, entry): entry for entry in manifest["files"]}
        for future in as_completed(pending):
            entry = pending[future]
            try:
                future.result()
            except Exception as error:
                errors.append({"path": entry["path"], "error": str(error)})
    actual = {p.relative_to(ARCHIVE / "upstream").as_posix() for p in (ARCHIVE / "upstream").rglob("*") if p.is_file()}
    for unexpected in sorted(actual - seen):
        errors.append({"path": unexpected, "error": "unlisted file"})
    catalog = write_catalog(manifest) if args.catalog and not errors else None
    print(json.dumps({
        "commit": manifest["commit"], "files": len(seen),
        "bytes": sum(entry["size"] for entry in manifest["files"]),
        "status": "failed" if errors else "verified", "errors": errors, "catalog": catalog,
    }, ensure_ascii=False, indent=2))
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
