#include "graphics/mask_coverage.h"
#include <algorithm>
#include <limits>
#include <cmath>

namespace lunaredu::lunaros::graphics {
namespace {
bool valid_mask(assets::MaskView view) {
  if (!view.data || !view.width || !view.height) return false;
  if (view.format == assets::MaskFormat::Alpha8) return view.stride >= view.width;
  if (view.format == assets::MaskFormat::Mono1) return view.stride >= (view.width + 7) / 8;
  return false;
}
// Q32 source coordinates keep large output dimensions accurate without a
// division/floor or floating-point interpolation in the inner pixel loop.
// Only the coverage weights are rounded to Q12 (less than one alpha step).
struct CoverageAxis {
  int whole = 0;
  uint32_t fraction = 0, step_fraction = 0;
  unsigned step_whole = 0;
  CoverageAxis(unsigned source, unsigned destination) {
    constexpr int64_t unit = int64_t{1} << 32;
    const int64_t start = (int64_t(source)-destination)*(unit/2)/destination;
    whole = int(start/unit);
    if (start<0 && start%unit) --whole;
    fraction = uint32_t(start-int64_t(whole)*unit);
    step_whole = source/destination;
    step_fraction = uint32_t((uint64_t(source%destination)<<32)/destination);
  }
  void next() {
    const uint32_t previous = fraction;
    fraction += step_fraction;
    whole += int(step_whole)+(fraction<previous);
  }
  void advance(unsigned count) {
    const uint64_t sum=uint64_t(fraction)+uint64_t(step_fraction)*count;
    fraction=uint32_t(sum);whole+=int(step_whole*count+(sum>>32));
  }
  unsigned weight() const { return fraction>>20; }
};
unsigned coverage(assets::MaskView view, int x, int y) {
  if (x<0 || y<0 || x>=view.width || y>=view.height) return 0;
  const auto value = view.data[y*view.stride+(view.format==assets::MaskFormat::Alpha8?x:x/8)];
  return view.format==assets::MaskFormat::Alpha8?value:(value&(0x80u>>(x%8)))?255u:0u;
}
}
static bool draw_mask_region(assets::MaskView view, int x, int y, uint16_t width, uint16_t height,
    MaskPixel pixel, void *context, Sampling sampling,unsigned first_x,unsigned first_y,unsigned last_x,unsigned last_y) {
  if (!valid_mask(view) || !width || !height || !pixel) return false;
  if (x > std::numeric_limits<int>::max() - (width - 1) ||
      y > std::numeric_limits<int>::max() - (height - 1)) return false;
  CoverageAxis vertical(view.height,height);
  CoverageAxis horizontal(view.width,width);
  vertical.advance(first_y);horizontal.advance(first_x);
  const bool native_mono = view.format==assets::MaskFormat::Mono1 && width==view.width && height==view.height;
  for (unsigned dy = first_y; dy < last_y; ++dy,vertical.next()) {
    const unsigned sy = dy * view.height / height;
    const int iy=vertical.whole;const unsigned ty=vertical.weight();
    auto column=horizontal;
    for (unsigned dx = first_x; dx < last_x; ++dx,column.next()) {
      uint8_t alpha=0;
      if(sampling==Sampling::Antialias){
        const int ix=column.whole;const unsigned tx=column.weight();
        if(native_mono){
          alpha=uint8_t((coverage(view,ix,iy)*12+coverage(view,ix-1,iy)+coverage(view,ix+1,iy)+
              coverage(view,ix,iy-1)+coverage(view,ix,iy+1)+8)/16);
        }else{
          const unsigned upper=coverage(view,ix,iy)*(4096-tx)+coverage(view,ix+1,iy)*tx;
          const unsigned lower=coverage(view,ix,iy+1)*(4096-tx)+coverage(view,ix+1,iy+1)*tx;
          alpha=uint8_t((upper*(4096-ty)+lower*ty+(1u<<23))>>24);
        }
      }else{
        const unsigned sx=dx*view.width/width;
        alpha=view.format==assets::MaskFormat::Alpha8?view.data[sy*view.stride+sx]:
            (view.data[sy*view.stride+sx/8]&(0x80u>>(sx%8)))?255:0;
        if(sampling==Sampling::Threshold)alpha=alpha>=128?255:0;
      }
      if (alpha) pixel(context, x + int(dx), y + int(dy), alpha);
    }
  }
  return true;
}
bool draw_mask(assets::MaskView view, int x, int y, uint16_t width, uint16_t height, MaskPixel pixel, void *context, Sampling sampling) {
  return draw_mask_region(view,x,y,width,height,pixel,context,sampling,0,0,width,height);
}
bool draw_mask(assets::MaskView view, int x, int y, MaskPixel pixel, void *context, Sampling sampling) {
  return draw_mask(view, x, y, view.width, view.height, pixel, context, sampling);
}
bool draw_mask_fitted(assets::MaskView view, assets::MaskBounds box, int x, int y, uint16_t width, uint16_t height,
                 MaskPixel pixel, void *context, Sampling sampling) {
  if (!valid_mask(view) || box.x < 0 || box.y < 0 || box.width <= 0 || box.height <= 0 ||
      box.x > view.width - box.width || box.y > view.height - box.height ||
      !width || !height || !pixel) return false;
  if (x > std::numeric_limits<int>::max() - width || y > std::numeric_limits<int>::max() - height) return false;
  const double scale = std::min(double(width)/box.width,double(height)/box.height);
  const int w = std::max(1,int(std::lround(view.width*scale))),h = std::max(1,int(std::lround(view.height*scale)));
  if (w>65535 || h>65535) return false;
  const int64_t left = int64_t(x)+std::lround((width-box.width*scale)/2-box.x*scale);
  const int64_t top = int64_t(y)+std::lround((height-box.height*scale)/2-box.y*scale);
  if (left<std::numeric_limits<int>::min() || top<std::numeric_limits<int>::min() ||
      left>std::numeric_limits<int>::max() || top>std::numeric_limits<int>::max()) return false;
  // Fit can scale the entire source far beyond the destination (a narrow
  // glyph in a large padded atlas). Clip before sampling, rather than compute
  // coverage for pixels which the callback immediately discards.
  const auto begin_x=unsigned(std::clamp<int64_t>(int64_t(x)-left,0,w));
  const auto begin_y=unsigned(std::clamp<int64_t>(int64_t(y)-top,0,h));
  const auto end_x=unsigned(std::clamp<int64_t>(int64_t(x)+width-left,0,w));
  const auto end_y=unsigned(std::clamp<int64_t>(int64_t(y)+height-top,0,h));
  return draw_mask_region(view,int(left),int(top),w,h,pixel,context,sampling,begin_x,begin_y,end_x,end_y);
}
} // namespace lunaredu::lunaros::graphics
