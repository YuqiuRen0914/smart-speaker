#pragma once
#include <stdint.h>
#include <string.h>
// Coordinates and dimensions in a framebuffer; zero area means no I/O.
typedef struct { int x,y,width,height; } PetRegion;
typedef struct PetFrameBuffer {
    uint16_t *pixels;
    int width;
    int height;
    int stride;
    // Nominal resting blob diameter in screen pixels; zero keeps the source scale.
    int body_diameter;
    // Opt-in edge coverage for the body, eyes, and animated effects.
    int antialias;
    // Wire order is the default for direct panels. Native RGB565 lets an OS
    // compositor copy the immutable frame without a byte-swap pass.
#ifdef __cplusplus
    int native_rgb565 = 0;
#else
    int native_rgb565;
#endif
} PetFrameBuffer;

// Pack narrow rows for DMA without sending the framebuffer's untouched margins.
static inline void pet_copy_region_rows(uint16_t *out,PetFrameBuffer frame,
                                            PetRegion region,int row,int count){
    for(int i=0;i<count;i++)
        memcpy(out+i*region.width,frame.pixels+(region.y+row+i)*frame.stride+region.x,
               (unsigned)region.width*sizeof(uint16_t));
}

#ifdef __cplusplus
#include <algorithm>
namespace lunaredu::pet {
using FrameRegion=PetRegion;
using FrameBuffer=PetFrameBuffer;
class FrameDamage {
    FrameRegion previous{};
    int width=0,height=0;
    bool full=true;
public:
    void invalidate(){previous={};full=true;}
    FrameRegion update(FrameRegion current,int frame_width,int frame_height);
};
}
#endif
