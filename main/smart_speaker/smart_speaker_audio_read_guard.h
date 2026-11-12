#pragma once

// Audio-player task owns the decoder and its callbacks. An atomic handoff keeps
// the original FILE error available after the dependency closes the stream.
void speaker_audio_read_error_reset();
bool speaker_audio_read_error_take();
