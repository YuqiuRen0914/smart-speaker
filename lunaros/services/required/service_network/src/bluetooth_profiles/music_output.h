#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace lunaredu::lunaros::bluetooth_service_internal {
bool enable_music_output(bool enabled,std::string &error);
void music_audio_format(unsigned rate,unsigned channels);
void music_receive_pcm(const uint8_t *data,uint32_t size);
}
