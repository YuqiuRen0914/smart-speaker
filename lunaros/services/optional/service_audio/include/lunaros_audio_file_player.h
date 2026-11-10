#pragma once
#include "lunaros_audio_file_decoder.h"
#include "lunaros_audio_effects.h"
#include <cstdio>
#include <cstdint>
#include <esp_err.h>
namespace lunaredu::lunaros { class KernelRuntime; }
namespace lunaredu::audio {
class AudioFocusBroker;
enum class AudioMute { Mute, Unmute };
enum class FilePlayerState { Idle, Playing, Paused };
enum class FilePlayerEvent { Playing, Paused, Idle, Failed };
struct FilePlayerEventContext { FilePlayerEvent event; void *context=nullptr; };
struct FilePlayerConfig {
  void *context=nullptr;
  esp_err_t (*write)(const void *,size_t,size_t *,uint32_t,void *)=nullptr;
  esp_err_t (*reconfigure)(uint32_t rate,unsigned channels)=nullptr;
  esp_err_t (*mute)(AudioMute)=nullptr;
  // Pause/resume must not reset the song PCM clock.
  esp_err_t (*pause_output)(bool paused)=nullptr;
  bool (*drain)(uint32_t timeout_ms)=nullptr;
  // False retains the previous output generation and prevents a new file.
  bool (*discard)(uint32_t timeout_ms)=nullptr;
  // Synchronous notifications may copy state or enqueue work; they must not
  // reenter the player or retain the borrowed event object.
  void (*event)(FilePlayerEventContext *)=nullptr;
  const char *task_id="audio.file_player",*service_id="audio";
  unsigned priority=8;
  int core=1;
  bool force_stereo=false;
  // Required and configured by composition for this physical device.
  AudioFocusBroker *focus=nullptr;
  const char *focus_owner="music.playback";
  PcmEffects effects{};
};
// One physical output binding per firmware. Managed resident worker sleeps when
// idle. Decoder and bounded PCM buffers exist only while a file is active.
bool start_file_player(lunaros::KernelRuntime &,const FilePlayerConfig &,std::string &);
// Failure retains bindings, task and buffers for a later retry.
bool stop_file_player(std::string &);
// Ownership of FILE transfers only on success. Commands never close an active
// worker's FILE; replacement/stop cancel it on that worker before opening next.
esp_err_t file_player_play(FILE *,AudioFileFormat=AudioFileFormat::Mp3);
esp_err_t file_player_pause();
esp_err_t file_player_resume();
esp_err_t file_player_stop();
FilePlayerState file_player_state();
}
