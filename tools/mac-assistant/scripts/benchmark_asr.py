"""Transcribe test WAVs with the same pinned INT8 assets as REM Harness."""
import argparse
import hashlib
import json
from pathlib import Path
import time

ROOT = Path(__file__).resolve().parents[1]


def load_recognizer():
    import sherpa_onnx
    model = ROOT / '.runtime/SenseVoice'
    assets = json.loads((ROOT / '.runtime/sensevoice-assets.json').read_text())
    for row in [assets['models']['int8'], assets['tokens']]:
        path = model / row['name']
        with path.open('rb') as stream:
            actual = hashlib.file_digest(stream, 'sha256').hexdigest()
        if path.stat().st_size != row['bytes'] or actual != row['sha256']:
            raise RuntimeError('SenseVoice integrity mismatch: ' + path.name)
    return sherpa_onnx.OfflineRecognizer.from_sense_voice(
        model=str(model / 'model.int8.onnx'), tokens=str(model / 'tokens.txt'),
        num_threads=2, use_itn=True, language='auto', provider='cpu')


def transcribe(recognizer, path):
    import numpy as np
    import soundfile as sf
    audio, rate = sf.read(path, dtype='float32', always_2d=True)
    audio = np.ascontiguousarray(audio.mean(axis=1))
    started = time.perf_counter()
    stream = recognizer.create_stream()
    stream.accept_waveform(rate, audio)
    recognizer.decode_stream(stream)
    return dict(file=str(path), text=stream.result.text, audio_seconds=len(audio)/rate,
                inference_seconds=time.perf_counter()-started)


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('files', type=Path, nargs='+')
    ap.add_argument('--output', required=True, type=Path)
    args = ap.parse_args()
    start = time.perf_counter()
    recognizer = load_recognizer()
    report = dict(load_seconds=time.perf_counter()-start, samples=[])
    for path in args.files:
        row = transcribe(recognizer, path)
        report['samples'].append(row)
        print(json.dumps(row, ensure_ascii=False), flush=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2))
