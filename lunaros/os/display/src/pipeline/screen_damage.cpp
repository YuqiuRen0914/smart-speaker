#include "diagnostics/function_probe.h"
#include "display/pipeline/damage_set.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "screen_geometry_internal.h"

namespace lunaredu::display {

using namespace screen_detail;

bool ScreenDamageSet::configure(int width, int height, std::string &error) {
  if (width <= 0 || height <= 0) {
    error = "invalid_screen_size";
    return false;
  }
  width_ = width;
  height_ = height;
  clear();
  error.clear();
  return true;
}

void ScreenDamageSet::clear() {
  count_ = 0;
  full_ = false;
}

void ScreenDamageSet::fill() {
  if (width_ <= 0 || height_ <= 0) return;
  regions_[0] = {0, 0, width_, height_};
  count_ = 1;
  full_ = true;
}

bool ScreenDamageSet::add(ScreenRegion region) {
  if (!valid_region(region, width_, height_)) return false;
  if (region.width == 0 || region.height == 0 || full_) return true;
  for (size_t index = 0; index < count_;) {
    if (!worth_merging(region, regions_[index])) {
      ++index;
      continue;
    }
    region = united(region, regions_[index]);
    regions_[index] = regions_[--count_];
    index = 0;
  }
  if (count_ == regions_.size()) {
    fill();
    return true;
  }
  regions_[count_++] = region;
  return true;
}

bool ScreenDamageSet::intersects(ScreenRegion region) const {
  if (region.width <= 0 || region.height <= 0) return false;
  for (size_t index = 0; index < count_; ++index) {
    const auto other = regions_[index];
    if (region.x < other.x + other.width && other.x < region.x + region.width &&
        region.y < other.y + other.height && other.y < region.y + region.height)
      return true;
  }
  return false;
}
bool ScreenDamageSet::add_costed(ScreenRegion region,unsigned window_pixels) {
  if(!valid_region(region,width_,height_))return false;
  if(!region.width||!region.height||full_)return true;
  std::array<ScreenRegion,kScreenBatchMaxRegions+1> candidates{};
  std::copy_n(regions_.data(),count_,candidates.data());candidates[count_]=region;
  size_t count=count_+1;
  const auto area=[](ScreenRegion r){return int64_t(r.width)*r.height;};
  while(count>1){
    int64_t best=INT64_MAX;size_t first=0,second=1;
    for(size_t i=0;i<count;++i)for(size_t j=i+1;j<count;++j){
      const auto cost=area(united(candidates[i],candidates[j]))-area(candidates[i])-area(candidates[j])-window_pixels;
      if(cost<best){best=cost;first=i;second=j;}
    }
    if(count<=kScreenBatchMaxRegions&&best>0)break;
    candidates[first]=united(candidates[first],candidates[second]);candidates[second]=candidates[--count];
  }
  int64_t cost=int64_t(count)*window_pixels;
  for(size_t i=0;i<count;++i)cost+=area(candidates[i]);
  if(cost>=int64_t(width_)*height_+window_pixels){fill();return true;}
  count_=count;std::copy_n(candidates.data(),count_,regions_.data());return true;
}

} // namespace lunaredu::display

#include "display/pipeline/frame_damage.h"
#include "display/pipeline/frame_refresh.h"

namespace lunaredu::display {
#if defined(__GNUC__)
// GCC's alias-qualified word access is valid for RGB565 storage. Only read
// aligned pairs inside a row; other compilers use the scalar path below.
using DamageWord = uint32_t __attribute__((__may_alias__));
inline bool aligned_damage_pair(const uint16_t *a,const uint16_t *b) {
  return ((reinterpret_cast<uintptr_t>(a)|reinterpret_cast<uintptr_t>(b))&3U)==0;
}
inline bool equal_damage_pair(const uint16_t *a,const uint16_t *b) {
  return *reinterpret_cast<const DamageWord *>(a)==*reinterpret_cast<const DamageWord *>(b);
}
inline bool equal_damage_octet(const uint16_t *a,const uint16_t *b) {
  const auto *wa=reinterpret_cast<const DamageWord *>(a);
  const auto *wb=reinterpret_cast<const DamageWord *>(b);
  return ((wa[0]^wb[0])|(wa[1]^wb[1])|(wa[2]^wb[2])|(wa[3]^wb[3]))==0;
}
#endif
namespace {
int first_changed(const uint16_t *a,const uint16_t *b,int begin,int end){
    // Locate only the two changed boundaries. A whole-row memcmp followed by
    // another prefix/suffix scan rereads unchanged PSRAM for every frame.
    // Native reads avoid tiny memcpy calls on Xtensa. Scalar boundaries also
    // keep odd strides and differently aligned row pairs exact.
    int first=begin;
#if defined(__GNUC__)
    if(first+1<end){
      // A 16-bit offset or odd stride may need one scalar pixel before pairs.
      if(!aligned_damage_pair(a+first,b+first)&&a[first]==b[first])++first;
      if(first+1<end&&aligned_damage_pair(a+first,b+first)){
        // Skip eight identical pixels per branch, then locate an exact boundary
        // with pairs/scalars. Reads stay inside this row and require alignment.
        while(first+7<end&&equal_damage_octet(a+first,b+first))first+=8;
        while(first+1<end&&equal_damage_pair(a+first,b+first))first+=2;
      }
    }
#endif
    while(first<end&&a[first]==b[first])++first;
    return first;
}
int last_changed(const uint16_t *a,const uint16_t *b,int begin,int end){
    int last=end-1;
#if defined(__GNUC__)
    if(last>begin){
      if(!aligned_damage_pair(a+last-1,b+last-1)&&a[last]==b[last])--last;
      if(last>begin&&aligned_damage_pair(a+last-1,b+last-1)){
        while(last-7>=begin&&equal_damage_octet(a+last-7,b+last-7))last-=8;
        while(last>begin&&equal_damage_pair(a+last-1,b+last-1))last-=2;
      }
    }
#endif
    while(last>=begin&&a[last]==b[last])--last;
    return last;
}
}
// Find the exact enclosing patch without examining pixels already enclosed.
// Establish its top/bottom first; each remaining row can only expand the two
// horizontal boundaries. This applies equally to loading, motion and pages.
Damage changed_region(const uint16_t *before,const uint16_t *after,int width,int height,int stride){
  int top=0,left=width,right=-1;
  for(;top<height;++top){
    left=first_changed(before+top*stride,after+top*stride,0,width);
    if(left<width)break;
  }
  if(top==height)return {};
  right=last_changed(before+top*stride,after+top*stride,left,width);
  int bottom=height-1;
  for(;bottom>top;--bottom){
    const auto *a=before+bottom*stride,*b=after+bottom*stride;
    const int first=first_changed(a,b,0,width);
    if(first==width)continue;
    left=std::min(left,first);
    right=std::max(right,last_changed(a,b,first,width));
    break;
  }
  for(int y=top+1;y<bottom&&(left>0||right<width-1);++y){
    const auto *a=before+y*stride,*b=after+y*stride;
    left=first_changed(a,b,0,left);
    right=std::max(right,last_changed(a,b,right+1,width));
  }
  return {left,top,right-left+1,bottom-top+1};
}
Damage changed_region(const uint16_t *before,const uint16_t *after,int width,int height,int stride,const FrameDamageMap *map){
  if(!map||!map->valid(width,height))return changed_region(before,after,width,height,stride);
  int left=width,right=0,top=height,bottom=0;
  for(int y=0;y<height;){
    const auto mask=map->row(unsigned(y)/16);
    int rows=std::min(16,height-y);
    if(y+rows<height&&map->row(unsigned(y+rows)/16)==mask)rows+=std::min(16,height-y-rows);
    for(unsigned tile=0;tile<unsigned((width+15)/16);){
      if(!(mask&(uint32_t(1)<<tile))){++tile;continue;}
      const int x=int(tile)*16;
      while(tile<unsigned((width+15)/16)&&(mask&(uint32_t(1)<<tile)))++tile;
      const int end=std::min(width,int(tile)*16);
      const auto patch=changed_region(before+y*stride+x,after+y*stride+x,end-x,rows,stride);
      if(patch.empty())continue;
      left=std::min(left,x+patch.x);right=std::max(right,x+patch.x+patch.width);
      top=std::min(top,y+patch.y);bottom=std::max(bottom,y+patch.y+patch.height);
    }
    y+=rows;
  }
  return right<=left?Damage{}:Damage{left,top,right-left,bottom-top};
}
ScreenDamageSet plan_frame_refresh_batch(const uint16_t *before,const uint16_t *after,int width,int height,int stride,
    bool baseline_valid,const FrameDamageMap *map,unsigned window_pixels){
  lunaredu::system::probe::Scope diagnostic_scope(lunaredu::system::probe::Point::Damage);
  ScreenDamageSet result;std::string error;if(!result.configure(width,height,error))return result;
  if(!baseline_valid||!before||!after||stride<width||width>512){result.fill();return result;}
  const bool mapped=map&&map->valid(width,height);
  for(int y=0;y<height;){
    const auto mask=mapped?map->row(unsigned(y)/16):UINT32_MAX;
    int rows=std::min(16,height-y);
    // Identical adjacent candidate bands have the same coverage. Compare one
    // bounded 32-row rectangle and cost its patch once; no hashes or omitted
    // pixels, and alternating/sparse bands retain their original granularity.
    if(mapped&&y+rows<height&&map->row(unsigned(y+rows)/16)==mask)
      rows+=std::min(16,height-y-rows);
    for(unsigned tile=0;tile<unsigned((width+15)/16);){
      if(!(mask&(uint32_t(1)<<tile))){++tile;continue;}
      const int x=int(tile)*16;
      while(tile<unsigned((width+15)/16)&&(mask&(uint32_t(1)<<tile)))++tile;
      const int end=std::min(width,int(tile)*16);
      const auto patch=mapped&&!map->refinement_required()?Damage{0,0,end-x,rows}:
        changed_region(before+y*stride+x,after+y*stride+x,end-x,rows,stride);
      if(!patch.empty())result.add_costed({x+patch.x,y+patch.y,patch.width,patch.height},window_pixels);
    }
    y+=rows;
  }
  return result;
}
}
