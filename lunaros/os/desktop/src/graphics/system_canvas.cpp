#include "desktop/ui/system_canvas.h"

namespace lunaredu::desktop::system_canvas {
void diagnostic(canvas::Surface s,Diagnostic pattern) {
  // Calibration signal colors are content, independent of the UI palette.
  if(pattern==Diagnostic::Bars){
    constexpr std::array<uint16_t,6> colors{0xf800,0xfd20,0x07e0,0x07ff,0x001f,0xf81f};
    for(int i=0;i<6;++i)s.rect({i*s.width/6,0,s.width/6,s.height},colors[i]);
  }else{
    s.rect({0,0,s.width/2,s.height/2},0xf800);s.rect({s.width/2,0,s.width/2,s.height/2},0x07e0);
    s.rect({0,s.height/2,s.width/2,s.height/2},0x001f);s.rect({s.width/2,s.height/2,s.width/2,s.height/2},0xfd20);
    s.label({0,s.height/2-ui::px(20),s.width,ui::px(40)},"方向",type::headline.size);
  }
}
}
