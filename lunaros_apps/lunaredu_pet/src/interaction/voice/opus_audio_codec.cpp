#include "interaction/voice/opus_audio_codec.h"

#include <esp_audio_enc.h>
#include <esp_audio_types.h>
#include <esp_opus_dec.h>
#include <esp_opus_enc.h>

namespace {

esp_opus_enc_frame_duration_t opus_frame_duration(int frame_duration_ms) {
  switch (frame_duration_ms) {
  case 5:
    return ESP_OPUS_ENC_FRAME_DURATION_5_MS;
  case 10:
    return ESP_OPUS_ENC_FRAME_DURATION_10_MS;
  case 20:
    return ESP_OPUS_ENC_FRAME_DURATION_20_MS;
  case 40:
    return ESP_OPUS_ENC_FRAME_DURATION_40_MS;
  case 60:
    return ESP_OPUS_ENC_FRAME_DURATION_60_MS;
  case 80:
    return ESP_OPUS_ENC_FRAME_DURATION_80_MS;
  case 100:
    return ESP_OPUS_ENC_FRAME_DURATION_100_MS;
  case 120:
    return ESP_OPUS_ENC_FRAME_DURATION_120_MS;
  default:
    return ESP_OPUS_ENC_FRAME_DURATION_60_MS;
  }
}

int opus_channels(int channels) {
  return channels == 2 ? ESP_AUDIO_DUAL : ESP_AUDIO_MONO;
}

} // namespace

bool opus_audio_codec_ensure_encoder(OpusAudioCodec &codec,
                                     int sample_rate,
                                     int frame_duration_ms,
                                     int channels,
                                     int complexity,
                                     std::string *error) {
  if (codec.encoder != nullptr) {
    return true;
  }

  esp_opus_enc_config_t encoder_config = {};
  encoder_config.sample_rate = sample_rate;
  encoder_config.channel = opus_channels(channels);
  encoder_config.bits_per_sample = ESP_AUDIO_BIT16;
  encoder_config.bitrate = ESP_OPUS_BITRATE_AUTO;
  encoder_config.frame_duration = opus_frame_duration(frame_duration_ms);
  encoder_config.application_mode = ESP_OPUS_ENC_APPLICATION_AUDIO;
  encoder_config.complexity = complexity;
  encoder_config.enable_dtx = true;
  encoder_config.enable_vbr = true;
  const int encoder_result =
      esp_opus_enc_open(&encoder_config, sizeof(encoder_config), &codec.encoder);
  if (encoder_result != ESP_AUDIO_ERR_OK || codec.encoder == nullptr) {
    if (error != nullptr) {
      *error = "failed_to_open_opus_encoder";
    }
    return false;
  }

  int frame_bytes = 0;
  int output_bytes = 0;
  esp_opus_enc_get_frame_size(codec.encoder, &frame_bytes, &output_bytes);
  codec.encoder_frame_samples =
      frame_bytes / static_cast<int>(sizeof(int16_t));
  codec.encoder_output_bytes = output_bytes;
  return true;
}

bool opus_audio_codec_ensure_decoder(OpusAudioCodec &codec,
                                     int sample_rate,
                                     int frame_duration_ms,
                                     int channels,
                                     std::string *error) {
  if (codec.decoder != nullptr &&
      codec.decoder_sample_rate == sample_rate &&
      codec.decoder_frame_duration_ms == frame_duration_ms) {
    return true;
  }
  if (codec.decoder != nullptr) {
    esp_opus_dec_close(codec.decoder);
    codec.decoder = nullptr;
    codec.decoder_sample_rate = 0;
    codec.decoder_frame_duration_ms = 0;
  }

  esp_opus_dec_cfg_t decoder_config = {};
  decoder_config.sample_rate = sample_rate;
  decoder_config.channel = opus_channels(channels);
  decoder_config.frame_duration = static_cast<esp_opus_dec_frame_duration_t>(
      opus_frame_duration(frame_duration_ms));
  decoder_config.self_delimited = false;
  const int decoder_result =
      esp_opus_dec_open(&decoder_config, sizeof(decoder_config), &codec.decoder);
  if (decoder_result != ESP_AUDIO_ERR_OK || codec.decoder == nullptr) {
    if (error != nullptr) {
      *error = "failed_to_open_opus_decoder";
    }
    return false;
  }
  codec.decoder_sample_rate = sample_rate;
  codec.decoder_frame_duration_ms = frame_duration_ms;
  return true;
}

