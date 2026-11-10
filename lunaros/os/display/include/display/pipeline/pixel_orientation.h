#pragma once
#include <algorithm>
#include <cstdint>
#include <cstddef>
namespace lunaredu::display {
// Maps an in-bounds logical pixel in a square frame to its clockwise rotation.
inline constexpr size_t rotated_pixel_index(int x,int y,unsigned rotation,int side=240){
  for(unsigned r=0;r<rotation%4;++r){const int previous=x;x=side-1-y;y=previous;}
  return static_cast<size_t>(y)*side+x;
}
// Rotate the borrowed square frame in place, without another frame allocation.
// Half turns and counterclockwise turns use one pass rather than repeated quarter turns.
inline void rotate_rgb565(uint16_t *pixels,unsigned rotation,int side=240){
  rotation%=4;if(!pixels||!rotation||side<=0)return;
  if(rotation==2){for(int i=0,end=side*side-1;i<end;++i,--end)std::swap(pixels[i],pixels[end]);return;}
  for(int layer=0;layer<side/2;++layer)for(int x=layer,last=side-1-layer;x<last;++x){
    const int offset=x-layer,a=layer*side+x,b=(last-offset)*side+layer,
      c=last*side+last-offset,d=x*side+last;
    const auto top=pixels[a];
    if(rotation==1){pixels[a]=pixels[b];pixels[b]=pixels[c];pixels[c]=pixels[d];pixels[d]=top;}
    else{pixels[a]=pixels[d];pixels[d]=pixels[c];pixels[c]=pixels[b];pixels[b]=top;}
  }
}
}
