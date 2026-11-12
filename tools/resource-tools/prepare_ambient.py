#!/usr/bin/env python3
"""Build canonical, seam-crossfaded ambient WAVs for the existing WAV decoder.
Requires ffmpeg and Python standard library. Inputs are licensed source recordings,
never fetched implicitly. Sources/derivative checksums are recorded in the pack.
"""
import argparse
import array
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import wave

SOURCES = {
    'rain': ('Soft rain.WAV', 'barkenov', 'https://freesound.org/people/barkenov/sounds/640655/'),
    'shore': ('Gentle small waves lapping on shore.wav', 'Alex_hears_things', 'https://freesound.org/people/Alex_hears_things/sounds/352356/'),
    'forest': ('Ambiance_Natural_Forest_Birds_Wind_Loop_Stereo.wav', 'Nox_Sound', 'https://freesound.org/people/Nox_Sound/sounds/570492/'),
    'cafe': ('Coffee Shop at the Capucins', 'Joseph SARDIN', 'https://bigsoundbank.com/coffee-shop-at-the-capucins-s2561.html'),
}

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def prepare(source_dir, output):
    folder = output / 'ambient'
    folder.mkdir(parents=True, exist_ok=True)
    manifest = {'schema': 1, 'sample_rate': 22050, 'channels': 2, 'bits': 16,
                'processing': 'high-pass 50 Hz, low-pass 8 kHz, loudnorm -26 LUFS / -8 dBTP / LRA 7, 1.5 s linear seam crossfade',
                'sources': []}
    with tempfile.TemporaryDirectory() as temp:
        for scene, (title, author, url) in SOURCES.items():
            src = source_dir / (scene + '.mp3')
            pcm_path = Path(temp) / (scene + '.pcm')
            subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(src), '-map', '0:a:0',
                '-af', 'highpass=f=50,lowpass=f=8000,loudnorm=I=-26:TP=-8:LRA=7',
                '-ar', '22050', '-ac', '2', '-f', 's16le', str(pcm_path)], check=True)
            samples = array.array('h', pcm_path.read_bytes())
            if sys.byteorder != 'little': samples.byteswap()
            overlap = 33075 * 2
            if len(samples) < overlap * 4: raise ValueError('Source too short')
            blend = array.array('h')
            for index in range(overlap):
                weight = (index // 2) / (overlap // 2 - 1)
                blend.append(round(samples[-overlap + index] * (1 - weight) + samples[index] * weight))
            loop = samples[overlap:-overlap] + blend
            peak = max(abs(x) for x in loop)
            seam = max(abs(loop[channel]-loop[-2+channel]) for channel in (0, 1))
            path = folder / (scene + '.wav')
            if sys.byteorder != 'little': loop.byteswap()
            with wave.open(str(path), 'wb') as wav:
                wav.setnchannels(2); wav.setsampwidth(2); wav.setframerate(22050); wav.writeframes(loop.tobytes())
            manifest['sources'].append({'scene':scene, 'title':title, 'author':author, 'url':url,
                'license':'CC0-1.0', 'license_url':'https://creativecommons.org/publicdomain/zero/1.0/',
                'input_variant': 'public high-quality MP3 preview' if scene != 'cafe' else 'public MP3 download',
                'source_sha256':digest(src), 'file':f'ambient/{scene}.wav', 'sha256':digest(path),
                'bytes':path.stat().st_size, 'seconds':len(loop)/44100, 'peak_pcm':peak, 'seam_delta_pcm':seam})
            print(scene, path.stat().st_size, 'peak', peak, 'seam delta', seam, flush=True)
    (output / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n')
    return manifest

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source_dir', type=Path, help='Directory containing rain/shore/forest/cafe.mp3')
    parser.add_argument('output', type=Path, help='Output SD-root directory')
    args=parser.parse_args(); prepare(args.source_dir, args.output)
