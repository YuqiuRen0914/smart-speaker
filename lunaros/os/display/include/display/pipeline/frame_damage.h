#pragma once
#include <cstdint>
#include <array>
#include <algorithm>
namespace lunaredu::display {
struct Damage { int x=0,y=0,width=0,height=0; bool empty()const{return !width||!height;} };
// Complete candidate coverage supplied by a painter, not a hash heuristic.
// A 16-pixel tile union preserves sparse App/system damage without allocating
// a screen-sized mask. Unsupported dimensions fall back to full comparison.
class FrameDamageMap {
 public:
  FrameDamageMap()=default;
  FrameDamageMap(int width,int height):width_(width),height_(height){
    valid_=width>0&&height>0&&width<=512&&height<=512;
  }
  bool valid(int width,int height)const{return valid_&&width_==width&&height_==height;}
  // A complete painter may send conservative coverage directly. Extra pixels
  // are safe; omitted changes are not. Pixel producers keep exact refinement.
  void use_declared_region(){refine_=false;refined_={};}
  bool refinement_required()const{return refine_;}
  Damage bounds()const{return masked_bounds(false);}
  Damage declared_bounds()const{return masked_bounds(true);}
  FrameDamageMap refined_candidates()const{
    FrameDamageMap result(width_,height_);result.tiles_=refined_;return result;
  }
  void mark_refined(Damage region){
    if(refine_){mark(region);return;}
    FrameDamageMap extra(width_,height_);extra.mark(region);
    for(unsigned y=0;y<tiles_.size();++y){refined_[y]|=extra.tiles_[y]&~tiles_[y];tiles_[y]|=extra.tiles_[y];}
  }
  Damage masked_bounds(bool declared_only)const{
    if(!valid_)return {};
    int left=width_,right=0,top=height_,bottom=0;
    for(unsigned y=0;y<unsigned((height_+15)/16);++y){
      auto mask=tiles_[y]&(declared_only?~refined_[y]:UINT32_MAX);if(!mask)continue;
      unsigned first=0,last=31;
      while(!(mask&(uint32_t(1)<<first)))++first;
      while(!(mask&(uint32_t(1)<<last)))--last;
      left=std::min(left,int(first)*16);right=std::max(right,std::min(width_,int(last+1)*16));
      top=std::min(top,int(y)*16);bottom=std::max(bottom,std::min(height_,int(y+1)*16));
    }
    return right<=left?Damage{}:Damage{left,top,right-left,bottom-top};
  }
  uint32_t row(unsigned tile_y)const{return tiles_[tile_y];}
  bool merge(const FrameDamageMap &other){
    if(!valid_||!other.valid(width_,height_))return false;
    for(unsigned y=0;y<tiles_.size();++y)tiles_[y]|=other.tiles_[y];
    refine_=true;refined_.fill(0);return true;
  }
  void mark(Damage region){
    if(!valid_||region.width<=0||region.height<=0)return;
    const int left=std::clamp(region.x,0,width_),top=std::clamp(region.y,0,height_);
    const int right=int(std::clamp<int64_t>(int64_t(region.x)+region.width,0,width_));
    const int bottom=int(std::clamp<int64_t>(int64_t(region.y)+region.height,0,height_));
    if(right<=left||bottom<=top)return;
    const unsigned first=unsigned(left)/16,last=unsigned(right-1)/16;
    const uint32_t mask=(UINT32_MAX<<first)&(UINT32_MAX>>(31-last));
    for(unsigned y=unsigned(top)/16;y<=unsigned(bottom-1)/16;++y){tiles_[y]|=mask;if(!refine_)refined_[y]&=~mask;}
  }
 private:
  std::array<uint32_t,32> tiles_{};
  std::array<uint32_t,32> refined_{};
  int width_=0,height_=0;
  bool valid_=false,refine_=true;
};
// Exact pixel comparison belongs to the pipeline's optimized implementation,
// rather than being compiled repeatedly with each product's size flags.
Damage changed_region(const uint16_t *,const uint16_t *,int width,int height,int stride);
Damage changed_region(const uint16_t *,const uint16_t *,int width,int height,int stride,const FrameDamageMap *);
}
