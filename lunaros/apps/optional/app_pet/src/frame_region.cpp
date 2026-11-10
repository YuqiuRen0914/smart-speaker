#include "lunaredu_pet_frame_region.h"
namespace lunaredu::pet {
FrameRegion FrameDamage::update(FrameRegion current,int frame_width,int frame_height) {
        if(frame_width<=0||frame_height<=0)return {};
        if(width!=frame_width||height!=frame_height){
            width=frame_width;height=frame_height;invalidate();
        }
        FrameRegion old=previous;previous=current;
        if(full){full=false;return {0,0,width,height};}
        if(current.width<=0||current.height<=0)current=old;
        else if(old.width>0&&old.height>0){
            int right=std::max(current.x+current.width,old.x+old.width);
            int bottom=std::max(current.y+current.height,old.y+old.height);
            current.x=std::min(current.x,old.x);current.y=std::min(current.y,old.y);
            current.width=right-current.x;current.height=bottom-current.y;
        }
        if(current.width<=0||current.height<=0)return {};
        // Even width and x keep every RGB565 DMA row word aligned.
        int right=std::min(width,(current.x+current.width+1)&~1);
        current.x&=~1;current.width=right-current.x;
        return current;
    }
}
