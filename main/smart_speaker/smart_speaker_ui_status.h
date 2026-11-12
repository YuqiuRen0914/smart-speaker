#pragma once
#include <string>

namespace smart_speaker {
inline const char *speaker_error_hint(const std::string &error) {
  if (error == "ambient_source_missing") return "请通过网页安装此场景音源";
  if (error == "ambient_save_failed") return "场景未能保存，重启后需重选";
  if (error == "ambient_not_playing") return "请先播放环境声";
  if (error == "audio_focus_busy") return "请等待当前对话结束";
  if (error == "noise_decode_failed" || error == "noise_play_failed") return "环境声播放失败，请重新安装音源";
  if (error == "track_read_failed") return "存储卡读取失败，请重试";
  if (error == "playback_mode_save_failed") return "播放模式未能保存";
  if (error == "playlist_unplayable") return "未找到可播放的歌曲";
  if (error == "upload_write_failed") return "歌曲未能保存";
  if (error == "track_delete_failed") return "歌曲未能删除";
  if (error == "playlist_empty") return "还没有歌曲";
  if (error == "sdcard_not_ready" || error == "sdcard_mount_failed") return "请检查存储卡";
  if (error == "track_not_found") return "找不到这首歌曲";
  if (error == "track_open_failed" || error == "track_play_failed" || error == "track_decode_timeout") return "这首歌曲无法播放";
  if (error == "audio_not_ready" || error == "audio_init_failed") return "播放暂不可用";
  return "操作未完成";
}
}  // namespace smart_speaker
