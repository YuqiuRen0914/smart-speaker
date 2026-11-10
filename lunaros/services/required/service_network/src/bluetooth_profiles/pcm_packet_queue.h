#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <algorithm>
namespace lunaredu::lunaros::bluetooth_service_internal {
inline constexpr size_t kPcmBlocks=16,kPcmBlockBytes=2048;
struct PcmBlock {uint32_t size;unsigned sample_rate=44100,channels=2;uint8_t data[kPcmBlockBytes];};
// One producer publishes the whole packet once all blocks have been copied.
// Variable SDK PCM packet sizes must not be confused with our pool block size.
inline bool queue_pcm_packet(PcmBlock *blocks,const uint8_t *data,uint32_t bytes,
                             uint32_t read,uint32_t &write,
                             unsigned sample_rate=44100,unsigned channels=2) {
  if(!blocks||!data||!bytes||(channels!=1&&channels!=2)||sample_rate<8000||sample_rate>48000||
      bytes%(channels*2)||bytes>kPcmBlocks*kPcmBlockBytes)return false;
  const uint32_t required=(bytes+kPcmBlockBytes-1)/kPcmBlockBytes,used=write-read;
  if(used>kPcmBlocks||required>kPcmBlocks-used)return false;
  for(uint32_t i=0,offset=0;i<required;++i) {
    auto &block=blocks[(write+i)%kPcmBlocks];block.size=std::min<uint32_t>(bytes-offset,kPcmBlockBytes);
    block.sample_rate=sample_rate;block.channels=channels;
    std::memcpy(block.data,data+offset,block.size);offset+=block.size;
  }
  write+=required;return true;
}
}
