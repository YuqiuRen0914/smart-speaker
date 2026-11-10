#pragma once
#include "lunaros_audio_file_decoder.h"
#include "lunaros_audio_pcm_output.h"
namespace lunaredu::audio {
// Shared file-playback core for App- and Service-owned I/O. Callers retain
// source leases, pause/cancel, focus, hardware drain and worker retirement.
class AudioFilePlayback {
public:
  bool open(AudioFileFormat,PcmOutputPorts,std::string &error);
  bool push(const uint8_t *,size_t,std::string &error);
  bool finish(std::string &error);
  bool close();
private:
  static bool output(void *,const int16_t *,size_t,unsigned,unsigned);
  AudioFileDecoder decoder_;
  PcmOutputAdapter output_;
  bool opened_=false;
};
}
