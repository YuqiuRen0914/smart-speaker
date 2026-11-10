#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
namespace lunaredu::audio {
struct AudioHistoryFrame {
  uint32_t sequence=0,timestamp_ms=0;
  int volume=50,peak_in=0,peak_out=0,avg_abs_out=0,limiter_gain_percent=100;
  uint32_t limiter_events=0,soft_clip_events=0,i2s_write_errors=0,i2s_short_writes=0;
  uint32_t max_write_gap_ms=0,max_write_time_ms=0;
  uint32_t dma_free_bytes=0,dma_largest_block=0,dma_minimum_free_bytes=0;
};
// Optional diagnostic history owned by the audio service's existing producer.
// No thread/timer/PCM copy. Readers copy only the requested newest frames and
// serialize JSON after releasing the lock. One producer calls due/capture.
class AudioHistory {
public:
  static constexpr size_t kCapacity=96;
  explicit AudioHistory(uint32_t interval_ms=500);
  bool due(uint32_t now_ms) const;
  void capture(uint32_t now_ms,int volume);
  size_t read_recent(AudioHistoryFrame *,size_t capacity,uint32_t &latest_sequence);
private:
  uint32_t interval_ms_,last_capture_ms_=0,sequence_=0;
  size_t count_=0;
  std::mutex mutex_;
  std::array<AudioHistoryFrame,kCapacity> frames_{};
};
}
