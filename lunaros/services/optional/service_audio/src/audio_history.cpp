#include "lunaros_audio_history.h"
#include "lunaros_audio_dual_i2s.h"
#include "kernel/memory_status.h"
#include <algorithm>
namespace lunaredu::audio {
AudioHistory::AudioHistory(uint32_t interval):interval_ms_(interval){}
bool AudioHistory::due(uint32_t now) const{return now-last_capture_ms_>=interval_ms_;}
void AudioHistory::capture(uint32_t now,int volume){
  if(!due(now))return;
  last_capture_ms_=now;
  const auto dsp=dual_i2s_dsp_diagnostics();const auto i2s=dual_i2s_diagnostics();
  const auto dma=system::read_memory_pool_free_snapshot(system::MemoryPool::Dma);
  AudioHistoryFrame frame;
  frame.timestamp_ms=now;frame.volume=volume;
  frame.peak_in=int(dsp.input_peak*32767.0f);frame.peak_out=int(dsp.output_peak*32767.0f);
  frame.avg_abs_out=int(dsp.rms*32767.0f);frame.limiter_gain_percent=int(dsp.limiter_gain*100.0f);
  frame.limiter_events=uint32_t(dsp.limiter_events);frame.soft_clip_events=uint32_t(dsp.soft_clip_samples);
  frame.i2s_write_errors=i2s.timeouts_left+i2s.timeouts_right;
  frame.i2s_short_writes=i2s.short_writes_left+i2s.short_writes_right;
  frame.max_write_gap_ms=i2s.maximum_write_gap_ms;frame.max_write_time_ms=i2s.maximum_write_time_ms;
  frame.dma_free_bytes=dma.free_bytes;frame.dma_largest_block=dma.largest_free_bytes;
  frame.dma_minimum_free_bytes=dma.minimum_free_bytes;
  std::lock_guard<std::mutex> lock(mutex_);
  if(++sequence_==0)++sequence_;
  frame.sequence=sequence_;frames_[sequence_%kCapacity]=frame;
  count_=std::min(count_+1,kCapacity);
}
size_t AudioHistory::read_recent(AudioHistoryFrame *output,size_t capacity,uint32_t &latest){
  std::lock_guard<std::mutex> lock(mutex_);latest=sequence_;
  if(!output)return 0;
  const auto count=std::min(count_,capacity);
  for(size_t i=0;i<count;++i){
    const uint32_t seq=sequence_-uint32_t(count-1-i);
    output[i]=frames_[seq%kCapacity];
  }
  return count;
}
}
