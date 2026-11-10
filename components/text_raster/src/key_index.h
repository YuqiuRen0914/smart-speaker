#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
namespace lunaredu::lunaros::text_raster::detail {
// Non-owning exact index over a fixed owner array. No bitmap/string storage,
// allocation or approximate/hash-only equality. Owners rebuild after tombstones.
template<size_t Capacity> class KeyIndex {
 public:
  template<class Match> int find(uint32_t code,unsigned size,Match match) const {
    size_t bucket=hash(code,size);
    for(size_t n=0;n<slots_.size();++n,bucket=(bucket+1)%slots_.size()){
      const auto slot=slots_[bucket];if(!slot)return -1;
      if(slot!=deleted&&match(unsigned(slot-1)))return int(slot-1);
    }
    return -1;
  }
  void insert(uint32_t code,unsigned size,unsigned index){
    size_t bucket=hash(code,size),vacant=slots_.size();
    for(size_t n=0;n<slots_.size();++n,bucket=(bucket+1)%slots_.size()){
      if(slots_[bucket]==deleted&&vacant==slots_.size())vacant=bucket;
      if(!slots_[bucket]){if(vacant==slots_.size())vacant=bucket;break;}
    }
    if(vacant<slots_.size()){if(slots_[vacant]==deleted)--tombstones_;slots_[vacant]=uint16_t(index+1);}
  }
  void erase(uint32_t code,unsigned size,unsigned index){
    size_t bucket=hash(code,size);
    for(size_t n=0;n<slots_.size();++n,bucket=(bucket+1)%slots_.size()){
      if(!slots_[bucket])return;
      if(slots_[bucket]==index+1){slots_[bucket]=deleted;++tombstones_;return;}
    }
  }
  bool needs_rebuild() const{return tombstones_>=Capacity;}
  void clear(){slots_.fill(0);tombstones_=0;}
 private:
  static_assert(Capacity>0&&Capacity<65534,"indices fit uint16_t");
  static constexpr uint16_t deleted=UINT16_MAX;
  static size_t hash(uint32_t code,unsigned size){return (uint32_t(code*2654435761U)^(size*2246822519U))%(Capacity*2);}
  std::array<uint16_t,Capacity*2> slots_{};
  size_t tombstones_=0;
};
}
