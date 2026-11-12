"""Use Homebrew's system Opus on Apple Silicon without global loader changes."""
import ctypes.util
import ctypes
from pathlib import Path

_find = ctypes.util.find_library
if _find('opus') is None:
    _opus = Path('/opt/homebrew/lib/libopus.dylib')
    if not _opus.is_file():
        raise RuntimeError('Install the Homebrew opus library first')
    ctypes.util.find_library = lambda name: str(_opus) if name == 'opus' else _find(name)
try:
    import opuslib
finally:
    ctypes.util.find_library = _find

# Apple ARM64 passes variadic arguments differently. Declare the two fixed
# arguments so opuslib's bitrate/getter controls use the correct native ABI.
opuslib.api.encoder.libopus_ctl.argtypes = (opuslib.api.encoder.EncoderPointer, ctypes.c_int)
opuslib.api.decoder.libopus_ctl.argtypes = (opuslib.api.decoder.DecoderPointer, ctypes.c_int)


def decoder():
    return opuslib.Decoder(16000, 1)


def encoder():
    result = opuslib.Encoder(24000, 1, opuslib.APPLICATION_AUDIO)
    result.bitrate = 32000
    return result
