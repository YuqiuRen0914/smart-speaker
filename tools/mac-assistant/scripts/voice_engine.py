"""A serialized inference subprocess; cancel never replays a stale result."""
import logging
import time


def inference_worker(inbox, outbox, backend):
    import numpy as np
    import torch
    from benchmark_tts import load_voice
    from benchmark_asr import load_recognizer
    from cosyvoice.utils.common import set_all_random_seed

    voice, info = load_voice(backend)
    recognizer = load_recognizer()
    logging.getLogger().setLevel(logging.WARNING)
    # Warm GPU kernels without sending audio to the speaker.
    with torch.inference_mode():
        list(voice.inference_zero_shot('主人，我在呢。', '', '',
                                      zero_shot_spk_id='feiyu-cute', stream=False))
    outbox.put(('ready', None, info))
    while True:
        command = inbox.get()
        if command is None:
            return
        kind, identity, value = command
        start = time.perf_counter()
        try:
            if kind == 'asr':
                samples = np.frombuffer(value, dtype='<i2').astype(np.float32) / 32768
                if not 1600 <= len(samples) <= 16000 * 30:
                    raise ValueError('Invalid recording duration')
                stream = recognizer.create_stream()
                stream.accept_waveform(16000, samples)
                recognizer.decode_stream(stream)
                result = dict(text=stream.result.text)
            elif kind == 'tts':
                if not isinstance(value, str) or not 1 <= len(value) <= 200:
                    raise ValueError('Invalid speech length')
                set_all_random_seed(42)
                with torch.inference_mode():
                    chunks = [item['tts_speech'].cpu().numpy().reshape(-1)
                              for item in voice.inference_zero_shot(value, '', '',
                                  zero_shot_spk_id='feiyu-cute', stream=False)]
                audio = np.concatenate(chunks)
                if not np.isfinite(audio).all() or abs(audio).max() < 1e-4:
                    raise ValueError('Invalid synthesized audio')
                audio *= min(1, .98 / abs(audio).max())
                result = dict(pcm=(audio * 32767).astype('<i2').tobytes(), sample_rate=24000)
            else:
                raise ValueError('Unknown inference job')
            result['seconds'] = time.perf_counter() - start
            outbox.put((kind, identity, result))
        except Exception as error:
            # No transcript or provider request data in background logs.
            outbox.put(('error', identity, {'kind': type(error).__name__}))
