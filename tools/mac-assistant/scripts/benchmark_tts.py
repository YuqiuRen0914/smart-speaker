"""Feiyu Cute F6 benchmark on Mac; original model files stay untouched."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT / '.runtime/CosyVoice'
PACKAGE = ROOT / '.runtime/FeiyuVoice-CosyVoice3'
os.environ.update(HF_HUB_OFFLINE='1', TRANSFORMERS_OFFLINE='1', OMP_NUM_THREADS='4',
                  TOKENIZERS_PARALLELISM='false', TQDM_DISABLE='1')
sys.path[:0] = [str(REPO), str(REPO / 'third_party/Matcha-TTS')]


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def load_voice(backend='cpu'):
    import numpy as np
    import soundfile as sf
    import torch
    import torchaudio
    torch.set_num_threads(4)
    # torchaudio 2.11 removed the soundfile backend used by pinned CosyVoice.
    # Limit the compatibility shim to this inference process and local WAV input.
    def read_audio(path, **kwargs):
        data, rate = sf.read(path, dtype='float32', always_2d=True)
        return torch.from_numpy(np.ascontiguousarray(data.T)), rate
    torchaudio.load = read_audio
    import wetext.wetext as wetext_runtime
    receipt = json.loads((ROOT / '.runtime/wetext-receipt.json').read_text())
    for row in receipt['files']:
        if sha(ROOT / '.runtime/wetext' / row['path']) != row['sha256']:
            raise RuntimeError('WeText integrity mismatch')
    def local_wetext(model_id, **kwargs):
        if model_id != 'pengzhendong/wetext':
            raise RuntimeError('Unexpected text frontend model')
        return str(ROOT / '.runtime/wetext')
    wetext_runtime.snapshot_download = local_wetext
    from cosyvoice.cli.cosyvoice import AutoModel
    manifest = json.loads((PACKAGE / 'manifest.json').read_text())
    selected = manifest['styles']['cute']
    checkpoint, reference = PACKAGE / selected['llm_checkpoint'], PACKAGE / selected['reference_audio']
    if sha(checkpoint) != selected['llm_sha256'] or sha(reference) != selected['reference_sha256']:
        raise RuntimeError('Cute checkpoint or reference integrity mismatch')
    commit = subprocess.check_output(['git', '-C', str(REPO), 'rev-parse', 'HEAD'], text=True).strip()
    if commit != '074ca6dc9e80a2f424f1f74b48bdd7d3fea531cc':
        raise RuntimeError('Unexpected CosyVoice source revision')
    started = time.perf_counter()
    voice = AutoModel(model_dir=str(PACKAGE / manifest['base_model_dir']), fp16=False,
                      load_trt=False, load_vllm=False)
    if voice.frontend.text_frontend != 'wetext':
        raise RuntimeError('Offline WeText frontend failed to load')
    state = torch.load(checkpoint, map_location='cpu', weights_only=True)
    voice.model.llm.load_state_dict(state, strict=True)
    del state
    voice.model.llm.eval()
    # Upstream grows token_hop_len on the model object during a streaming call.
    # Reset for each serialized sentence, otherwise later turns lose low latency.
    original_tts = voice.model.tts
    def reset_stream_window(*args, **kwargs):
        voice.model.token_hop_len = 25
        yield from original_tts(*args, **kwargs)
    voice.model.tts = reset_stream_window
    if backend == 'mps-hybrid':
        if not torch.backends.mps.is_available():
            raise RuntimeError('Apple MPS unavailable')
        voice.model.device = torch.device('mps')
        voice.model.llm.to('mps')
        voice.model.flow.to('mps')
        # HiFT requires float64 F0 and complex FFT operations. Preserve original
        # arithmetic on CPU; move only the autoregressive and flow networks.
        original_inference = voice.model.hift.inference
        def cpu_hift(speech_feat, **kwargs):
            return original_inference(speech_feat.cpu(), **kwargs)
        voice.model.hift.inference = cpu_hift
    load_seconds = time.perf_counter() - started
    prompt = manifest['inference']['prompt_prefix'] + selected['reference_text']
    prep = time.perf_counter()
    voice.add_zero_shot_spk(prompt, str(reference), 'feiyu-cute')
    return voice, dict(backend=backend, torch=torch.__version__, source_commit=commit,
                      checkpoint_sha256=selected['llm_sha256'], reference_sha256=selected['reference_sha256'],
                      load_seconds=load_seconds, prompt_seconds=time.perf_counter()-prep,
                      text_frontend=voice.frontend.text_frontend)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--backend', choices=['cpu', 'mps-hybrid'], default='cpu')
    ap.add_argument('--output', required=True, type=Path)
    ap.add_argument('--stream', action='store_true')
    ap.add_argument('--text', action='append')
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    import numpy as np
    import soundfile as sf
    import torch
    voice, report = load_voice(args.backend)
    from cosyvoice.utils.common import set_all_random_seed
    print('MODEL_READY ' + json.dumps(report), flush=True)
    report['stream'] = args.stream
    report['samples'] = []
    texts = args.text or ['主人，我在呢。今天想听什么歌呀？',
                         '主人，忙了一天辛苦啦。要不要听会儿音乐，放松一下？',
                         '主人，你想听 Let Go，对吗？']
    for i, text in enumerate(texts):
        set_all_random_seed(42)
        started = time.perf_counter()
        chunks, arrivals = [], []
        with torch.inference_mode():
            for result in voice.inference_zero_shot(text, '', '', zero_shot_spk_id='feiyu-cute',
                                                     stream=args.stream, text_frontend=True):
                samples = result['tts_speech'].detach().cpu().numpy().reshape(-1)
                chunks.append(samples)
                arrivals.append(dict(at_seconds=time.perf_counter()-started, samples=len(samples)))
        elapsed = time.perf_counter()-started
        audio = np.concatenate(chunks)
        if not np.isfinite(audio).all() or np.max(np.abs(audio)) < 1e-4:
            raise RuntimeError('Nonfinite or silent audio')
        path = args.output / f'cute-{i+1}.wav'
        sf.write(path, audio * min(1, .98 / np.max(np.abs(audio))), voice.sample_rate, subtype='PCM_16')
        row = dict(text=text, file=str(path.resolve()), sample_rate=voice.sample_rate,
                   audio_seconds=len(audio)/voice.sample_rate, elapsed_seconds=elapsed,
                   first_chunk_seconds=arrivals[0]['at_seconds'], chunks=arrivals,
                   realtime_factor=elapsed/(len(audio)/voice.sample_rate), sha256=sha(path))
        report['samples'].append(row)
        report['process_peak_rss_bytes'] = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
        (args.output/'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2))
        print('SAMPLE_DONE ' + json.dumps(row, ensure_ascii=False), flush=True)


if __name__ == '__main__':
    main()
