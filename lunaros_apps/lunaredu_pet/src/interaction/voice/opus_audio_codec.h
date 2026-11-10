#pragma once

#include <string>

struct OpusAudioCodec {
  void *encoder = nullptr;
  void *decoder = nullptr;
  int encoder_frame_samples = 0;
  int encoder_output_bytes = 0;
  int decoder_sample_rate = 0;
  int decoder_frame_duration_ms = 0;
};

bool opus_audio_codec_ensure_encoder(OpusAudioCodec &codec,
                                     int sample_rate,
                                     int frame_duration_ms,
                                     int channels,
                                     int complexity,
                                     std::string *error);
bool opus_audio_codec_ensure_decoder(OpusAudioCodec &codec,
                                     int sample_rate,
                                     int frame_duration_ms,
                                     int channels,
                                     std::string *error);

