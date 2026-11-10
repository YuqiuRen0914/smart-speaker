#include "graphics/canvas_painter.h"
#include "fonts.h"
#include "graphics_pool.h"
#include <lvgl/lvgl.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <new>
#include <array>
#ifndef LUNAROS_CANVAS_MASK_CACHE
#define LUNAROS_CANVAS_MASK_CACHE 1
#endif

namespace lunaredu::lunaros::graphics {
namespace {
lv_color_t color(uint16_t value){
  lv_color16_t packed{};static_assert(sizeof(packed)==sizeof(value));
  std::memcpy(&packed,&value,sizeof(value));return lv_color16_to_color(packed);
}
}
namespace {
struct ImmediateWorkspace {
  lv_obj_t *canvas=nullptr,*label=nullptr,*transform=nullptr;
  uint32_t parked_pixel=0;
  struct Mask {
    const void *source=nullptr;assets::MaskBounds crop{};
    uint64_t revision=0,touched=0;uint16_t stride=0;
    int width=0,height=0;assets::MaskFormat format{};bool aa=false;
    lv_image_dsc_t image{};uint8_t *pixels=nullptr;size_t bytes=0;
    void reset(){lunaros_graphics_free_buffer(pixels);*this={};}
  };
  std::array<Mask,64> masks{};uint64_t clock=0;size_t bytes=0,maximum_bytes=0;
  ~ImmediateWorkspace(){for(auto &mask:masks)mask.reset();if(canvas)lv_obj_delete(canvas);}
};
}
void CanvasPainter::Workspace::reset(){if(native){auto *work=static_cast<ImmediateWorkspace *>(native);work->~ImmediateWorkspace();lunaros_graphics_free_buffer(work);native=nullptr;}}
CanvasPainter::Workspace::~Workspace(){reset();}
struct CanvasPainter::Impl {
  lv_obj_t *canvas=nullptr;
  SceneFonts *fonts=nullptr;
  Rect bounds{},clip{};
  const uint16_t *output=nullptr;
  size_t output_bytes=0;
  bool antialias=true,failed=false;
  ImmediateWorkspace *workspace=nullptr;
  bool layer(lv_layer_t &layer){
    if(failed||clip.width<=0||clip.height<=0)return false;
    lv_canvas_init_layer(canvas,&layer);
    layer.buf_area={bounds.x,bounds.y,bounds.x+bounds.width-1,bounds.y+bounds.height-1};
    layer.phy_clip_area=layer.buf_area;
    layer._clip_area={clip.x,clip.y,clip.x+clip.width-1,clip.y+clip.height-1};
    return true;
  }
  void finish(lv_layer_t &layer){lv_canvas_finish_layer(canvas,&layer);}
  const lv_font_t *font(unsigned size){
    const auto *font=fonts->get(size);if(!font)failed=true;return font;
  }
};
void CanvasPainter::clip(Rect r){
  const auto &b=impl_->bounds;
  const int x=std::clamp(r.x,b.x,b.x+b.width),y=std::clamp(r.y,b.y,b.y+b.height);
  const int right=int(std::clamp<int64_t>(int64_t(r.x)+std::max(0,r.width),b.x,int64_t(b.x)+b.width));
  const int bottom=int(std::clamp<int64_t>(int64_t(r.y)+std::max(0,r.height),b.y,int64_t(b.y)+b.height));
  impl_->clip={x,y,std::max(0,right-x),std::max(0,bottom-y)};
}
bool CanvasPainter::good() const{return !impl_->failed;}
bool CanvasPainter::rectangle(Rect r,uint16_t ink,int radius,int border,uint8_t opacity){
  if(r.width<=0||r.height<=0||!opacity)return good();
  lv_layer_t layer{};if(!impl_->layer(layer))return good();
  lv_draw_rect_dsc_t dsc;lv_draw_rect_dsc_init(&dsc);
  dsc.radius=std::max(0,radius);dsc.bg_color=color(ink);dsc.bg_opa=border?uint8_t(LV_OPA_TRANSP):opacity;
  dsc.border_color=color(ink);dsc.border_width=std::max(0,border);dsc.border_opa=opacity;
  const lv_area_t area{r.x,r.y,r.x+r.width-1,r.y+r.height-1};
  lv_draw_rect(&layer,&dsc,&area);impl_->finish(layer);return good();
}
bool CanvasPainter::line(float x1,float y1,float x2,float y2,uint16_t ink,uint8_t opacity,unsigned width,bool rounded){
  if(!std::isfinite(x1)||!std::isfinite(y1)||!std::isfinite(x2)||!std::isfinite(y2))return impl_->failed=true,false;
  if(std::max({std::abs(x1),std::abs(y1),std::abs(x2),std::abs(y2)})>1048576.f)return impl_->failed=true,false;
  if(!width||width>1024)return impl_->failed=true,false;
  if(!opacity)return good();
  lv_layer_t layer{};if(!impl_->layer(layer))return good();
  lv_draw_line_dsc_t dsc;lv_draw_line_dsc_init(&dsc);
  dsc.p1={int32_t(std::lround(x1)),int32_t(std::lround(y1))};
  dsc.p2={int32_t(std::lround(x2)),int32_t(std::lround(y2))};
  dsc.color=color(ink);dsc.width=int32_t(width);dsc.opa=opacity;dsc.round_start=dsc.round_end=rounded;
  lv_draw_line(&layer,&dsc);impl_->finish(layer);return good();
}
Rect CanvasPainter::text_bounds(const char *text,unsigned size){
  if(!text)return {};
  const auto *font=impl_->font(size);if(!font)return {};
  lv_point_t extent{};lv_text_get_size(&extent,text,font,0,0,INT32_MAX,LV_TEXT_FLAG_NONE);
  return {0,0,extent.x,extent.y};
}
bool CanvasPainter::text(int x,int y,const char *text,unsigned size,uint16_t ink){
  if(!text||!*text)return good();
  const auto extent=text_bounds(text,size);if(!good()||extent.width<=0||extent.height<=0)return good();
  lv_layer_t layer{};if(!impl_->layer(layer))return good();
  lv_draw_label_dsc_t dsc;lv_draw_label_dsc_init(&dsc);
  dsc.text=text;dsc.font=impl_->font(size);dsc.color=color(ink);dsc.flag=LV_TEXT_FLAG_EXPAND;
  const lv_area_t area{x,y,x+extent.width-1,y+extent.height-1};
  lv_draw_label(&layer,&dsc,&area);impl_->finish(layer);return good();
}
bool CanvasPainter::label(Rect r,const char *value,unsigned size,uint16_t ink,Align align,bool ellipsis){
  if(r.width<=0||r.height<=0)return good();
  const auto *font=impl_->font(size);if(!font)return false;
  // Let the official Label handle UTF-8 ellipsis. It owns its temporary text
  // until the synchronous draw completes, and never joins the retained Scene.
  auto *label=impl_->workspace->label;
  if(!label){label=lv_label_create(impl_->canvas);impl_->workspace->label=label;}
  if(!label)return impl_->failed=true,false;
  lv_obj_remove_style_all(label);lv_obj_set_style_text_font(label,font,0);
  lv_obj_set_size(label,r.width,lv_font_get_line_height(font));
  lv_label_set_long_mode(label,ellipsis?LV_LABEL_LONG_MODE_DOTS:LV_LABEL_LONG_MODE_CLIP);
  lv_obj_update_layout(label);lv_label_set_text(label,value?value:"");
  const auto *text=lv_label_get_text(label);const auto bounds=text_bounds(text,size);
  const auto previous=impl_->clip;
  const int left=std::max(previous.x,r.x),top=std::max(previous.y,r.y);
  clip({left,top,std::max(0,std::min(previous.x+previous.width,r.x+r.width)-left),
    std::max(0,std::min(previous.y+previous.height,r.y+r.height)-top)});
  const int x=r.x+(align==Align::Center?(r.width-bounds.width)/2:align==Align::Right?r.width-bounds.width:0);
  const bool result=this->text(x,r.y+(r.height-bounds.height)/2,text,size,ink);
  impl_->clip=previous;lv_label_set_text_static(label,"");return result;
}
bool CanvasPainter::mask(Rect r,assets::MaskView view,assets::MaskBounds crop,uint16_t ink,uint8_t opacity,uint64_t revision){
  if(r.width<=0||r.height<=0||!opacity)return good();
  if(r.width>impl_->bounds.width||r.height>impl_->bounds.height||!view.data||crop.x<0||crop.y<0||crop.width<=0||crop.height<=0||
      crop.width>view.width||crop.height>view.height||crop.x>view.width-crop.width||crop.y>view.height-crop.height||
      (view.format!=assets::MaskFormat::Mono1&&view.format!=assets::MaskFormat::Alpha8)||
      view.stride<(view.format==assets::MaskFormat::Mono1?(view.width+7)/8:view.width)||
      size_t(crop.width)*size_t(crop.height)>64*1024)return impl_->failed=true,false;
  if(!impl_->clip.width||!impl_->clip.height)return good();
  auto &work=*impl_->workspace;
  ImmediateWorkspace::Mask *cached=nullptr;
  // Cache only small immutable icons, in at most 64 KiB of owner-owned PSRAM.
  // LVGL remains responsible for transformation and composition.
  if(LUNAROS_CANVAS_MASK_CACHE&&revision&&r.width<=64&&r.height<=64&&size_t(r.width)*r.height<=work.maximum_bytes){
    for(auto &entry:work.masks)if(entry.pixels&&entry.source==view.data&&entry.revision==revision&&
        entry.crop.x==crop.x&&entry.crop.y==crop.y&&entry.crop.width==crop.width&&entry.crop.height==crop.height&&
        entry.stride==view.stride&&entry.format==view.format&&entry.width==r.width&&entry.height==r.height&&entry.aa==impl_->antialias){cached=&entry;break;}
    if(!cached){
      const size_t wanted=size_t(r.width)*r.height;
      while(work.bytes+wanted>work.maximum_bytes){
        auto oldest=std::min_element(work.masks.begin(),work.masks.end(),[](const auto &a,const auto &b){return (a.pixels?a.touched:UINT64_MAX)<(b.pixels?b.touched:UINT64_MAX);});
        if(!oldest->pixels)break;
        work.bytes-=oldest->bytes;oldest->reset();
      }
      auto slot=std::min_element(work.masks.begin(),work.masks.end(),[](const auto &a,const auto &b){return (a.pixels?a.touched:0)<(b.pixels?b.touched:0);});
      work.bytes-=slot->bytes;slot->reset();
      slot->pixels=static_cast<uint8_t *>(lunaros_graphics_allocate_buffer(wanted));
      if(slot->pixels){slot->bytes=wanted;work.bytes+=wanted;cached=&*slot;}
    }
    if(cached&&cached->revision){
      cached->touched=++work.clock;
      lv_draw_image_dsc_t blend;lv_draw_image_dsc_init(&blend);blend.src=&cached->image;blend.opa=opacity;
      blend.recolor=color(ink);blend.recolor_opa=LV_OPA_COVER;
      const lv_area_t output{r.x,r.y,r.x+r.width-1,r.y+r.height-1};
      lv_layer_t layer{};if(!impl_->layer(layer))return good();
      lv_draw_image(&layer,&blend,&output);impl_->finish(layer);return good();
    }
  }
  const auto bytes=size_t(crop.width)*size_t(crop.height);
  std::unique_ptr<uint8_t,decltype(&lunaros_graphics_free_buffer)> alpha(
    static_cast<uint8_t *>(lunaros_graphics_allocate_buffer(bytes)),lunaros_graphics_free_buffer);
  if(!alpha)return impl_->failed=true,false;
  // Only unpack/crop source coverage. LVGL scales, samples and composites it.
  for(int y=0;y<crop.height;++y)for(int x=0;x<crop.width;++x){
    const auto *row=view.data+size_t(y+crop.y)*view.stride;const int sx=x+crop.x;
    const uint8_t value=view.format==assets::MaskFormat::Mono1?uint8_t(row[sx/8]&(0x80u>>(sx%8))?255:0):row[sx];
    alpha.get()[size_t(y)*crop.width+x]=impl_->antialias?value:value>=128?255:0;
  }
  lv_image_dsc_t image{};image.header.magic=LV_IMAGE_HEADER_MAGIC;image.header.cf=LV_COLOR_FORMAT_A8;
  image.header.w=crop.width;image.header.h=crop.height;image.header.stride=crop.width;
  image.data=alpha.get();image.data_size=uint32_t(bytes);
  lv_draw_image_dsc_t dsc;lv_draw_image_dsc_init(&dsc);dsc.src=&image;
  const int scale=std::max(1,std::min(r.width*256/crop.width,r.height*256/crop.height));
  dsc.scale_x=dsc.scale_y=scale;dsc.pivot={0,0};dsc.recolor=color(ink);
  dsc.recolor_opa=LV_OPA_COVER;dsc.opa=opacity;dsc.antialias=impl_->antialias;
  const int width=std::max(1,crop.width*scale/256),height=std::max(1,crop.height*scale/256);
  const int x=r.x+(r.width-width)/2,y=r.y+(r.height-height)/2;
  const lv_area_t area{x,y,x+crop.width-1,y+crop.height-1};
  if(cached){
    if(!work.transform){work.transform=lv_canvas_create(impl_->canvas);if(work.transform)lv_obj_set_hidden(work.transform,true);}
    lv_draw_buf_t buffer{};
    if(work.transform&&lv_draw_buf_init(&buffer,r.width,r.height,LV_COLOR_FORMAT_A8,r.width,cached->pixels,uint32_t(cached->bytes))==LV_RESULT_OK){
      std::memset(cached->pixels,0,cached->bytes);lv_canvas_set_draw_buf(work.transform,&buffer);
      lv_layer_t layer{};lv_canvas_init_layer(work.transform,&layer);
      const lv_area_t local{x-r.x,y-r.y,x-r.x+crop.width-1,y-r.y+crop.height-1};
      auto transform=dsc;transform.opa=LV_OPA_COVER;
      lv_draw_image(&layer,&transform,&local);lv_canvas_finish_layer(work.transform,&layer);
      lv_canvas_set_buffer(work.transform,&work.parked_pixel,1,1,LV_COLOR_FORMAT_ARGB8888);
      cached->image={};cached->image.header=buffer.header;cached->image.data=cached->pixels;cached->image.data_size=uint32_t(cached->bytes);
      cached->source=view.data;cached->revision=revision;cached->crop=crop;cached->stride=view.stride;cached->format=view.format;
      cached->width=r.width;cached->height=r.height;cached->aa=impl_->antialias;cached->touched=++work.clock;
      return mask(r,view,crop,ink,opacity,revision);
    }
    work.bytes-=cached->bytes;cached->reset();
  }
  lv_layer_t layer{};if(!impl_->layer(layer))return good();
  lv_draw_image(&layer,&dsc,&area);impl_->finish(layer);
  // No decoder/header cache may retain the stack descriptor or owned alpha.
  static_assert(LV_CACHE_DEF_SIZE==0&&LV_IMAGE_HEADER_CACHE_DEF_CNT==0);
  return good();
}
bool CanvasPainter::image(Rect r,const uint16_t *pixels,CanvasSpec source,int radius,uint8_t opacity){
  if(r.width<=0||r.height<=0||!opacity)return good();
  if(!pixels||!source.valid()||source.stride>UINT16_MAX/2||radius<0||
      r.width>impl_->bounds.width||r.height>impl_->bounds.height)return impl_->failed=true,false;
  const auto from=reinterpret_cast<uintptr_t>(pixels),to=reinterpret_cast<uintptr_t>(impl_->output);
  const auto destination_bytes=impl_->output_bytes;
  if(from<to?to-from<source.bytes():from-to<destination_bytes)return impl_->failed=true,false;
  lv_image_dsc_t image{};image.header.magic=LV_IMAGE_HEADER_MAGIC;image.header.cf=LV_COLOR_FORMAT_RGB565;
  image.header.w=source.width;image.header.h=source.height;image.header.stride=source.stride*2;
  image.data=reinterpret_cast<const uint8_t *>(pixels);image.data_size=uint32_t(source.bytes());
  lv_draw_image_dsc_t dsc;lv_draw_image_dsc_init(&dsc);dsc.src=&image;dsc.pivot={0,0};
  dsc.scale_x=std::max(1,r.width*256/source.width);dsc.scale_y=std::max(1,r.height*256/source.height);
  dsc.opa=opacity;dsc.antialias=impl_->antialias;dsc.clip_radius=radius;
  dsc.image_area={r.x,r.y,r.x+r.width-1,r.y+r.height-1};
  const lv_area_t area{r.x,r.y,r.x+source.width-1,r.y+source.height-1};
  if(radius&&(dsc.scale_x!=256||dsc.scale_y!=256)){
    // In pinned 9.6 the software image transform does not apply clip_radius.
    // Use official ARGB draw + rounded mask + composite in four-row strips,
    // avoiding either a private radius sampler or a full ARGB frame/layer.
    const int first=std::max(0,impl_->clip.y-r.y),last=std::min(r.height,impl_->clip.y+impl_->clip.height-r.y);
    const int left=std::max(0,impl_->clip.x-r.x),right=std::min(r.width,impl_->clip.x+impl_->clip.width-r.x);
    if(first>=last||left>=right)return good();
    const int rows=std::min(4,r.height);
    const size_t bytes=size_t(r.width)*rows*4;
    std::unique_ptr<uint8_t,decltype(&lunaros_graphics_free_buffer)> scratch(
      static_cast<uint8_t *>(lunaros_graphics_allocate_buffer(bytes)),lunaros_graphics_free_buffer);
    if(!scratch)return impl_->failed=true,false;
    lv_draw_buf_t buffer{};
    if(lv_draw_buf_init(&buffer,r.width,rows,LV_COLOR_FORMAT_ARGB8888,r.width*4,scratch.get(),uint32_t(bytes))!=LV_RESULT_OK)
      return impl_->failed=true,false;
    auto *strip=lv_canvas_create(lv_obj_get_parent(impl_->canvas));
    if(!strip)return impl_->failed=true,false;
    lv_obj_set_hidden(strip,true);lv_canvas_set_draw_buf(strip,&buffer);
    for(int band=first;band<last;band+=rows){
      const int count=std::min(rows,last-band);
      std::memset(scratch.get(),0,bytes);
      lv_layer_t work{};lv_canvas_init_layer(strip,&work);work._clip_area={left,0,right-1,count-1};
      auto transform=dsc;transform.opa=LV_OPA_COVER;transform.clip_radius=0;
      transform.image_area={0,-band,r.width-1,r.height-band-1};
      const lv_area_t input{0,-band,source.width-1,int(source.height)-band-1};
      lv_draw_image(&work,&transform,&input);
      lv_draw_mask_rect_dsc_t mask;lv_draw_mask_rect_dsc_init(&mask);
      mask.area=transform.image_area;mask.radius=radius;
      lv_draw_mask_rect(&work,&mask);lv_canvas_finish_layer(strip,&work);
      lv_image_dsc_t result{};result.header=buffer.header;result.data=scratch.get();result.data_size=uint32_t(bytes);
      lv_draw_image_dsc_t blend;lv_draw_image_dsc_init(&blend);blend.src=&result;blend.opa=opacity;
      const lv_area_t output{r.x,r.y+band,r.x+r.width-1,r.y+band+rows-1};
      lv_layer_t target{};
      if(!impl_->layer(target)){lv_obj_delete(strip);return good();}
      target._clip_area.y1=std::max<int32_t>(target._clip_area.y1,r.y+band);
      target._clip_area.y2=std::min<int32_t>(target._clip_area.y2,r.y+band+count-1);
      lv_draw_image(&target,&blend,&output);impl_->finish(target);
    }
    lv_obj_delete(strip);return good();
  }
  lv_layer_t layer{};if(!impl_->layer(layer))return good();
  lv_draw_image(&layer,&dsc,&area);impl_->finish(layer);return good();
}
bool CanvasPainter::paint_on(Workspace &storage,void *native_display,void *native_fonts,CanvasSpec spec,RasterOutput output,
    bool antialias,void *context,bool (*paint)(void *,CanvasPainter &),int origin_x,int origin_y){
  auto *display=static_cast<lv_display_t *>(native_display);auto *fonts=static_cast<SceneFonts *>(native_fonts);
  // Unlike a display/DMA buffer this is a synchronous software-only target.
  // Pinned draw_buf_init preserves the supplied pointer, and the RGB565
  // software blender aligns its word loops after a leading 16-bit pixel.
  static_assert(LV_USE_OS==LV_OS_NONE&&LV_DRAW_SW_DRAW_UNIT_CNT==1&&LV_USE_DRAW_SW_ASM==LV_DRAW_SW_ASM_NONE);
  if(!paint||!display||!fonts||!output.pixels||output.count!=spec.pixels()||
      reinterpret_cast<uintptr_t>(output.pixels)%alignof(uint16_t))return false;
  // Reserve native task/transform/circle work without enlarging the LVGL pool.
  // A declined frame stays with the caller for the normal recovery path.
  if(Scene::memory().largest_free<16*1024)return false;
  lv_draw_buf_t buffer{};
  if(lv_draw_buf_init(&buffer,spec.width,spec.height,LV_COLOR_FORMAT_RGB565,
      spec.stride*2,output.pixels,uint32_t(output.count*2))!=LV_RESULT_OK)return false;
  struct DisplayScope {
    lv_display_t *display,*previous_display;bool previous_aa;
    DisplayScope(lv_display_t *d,bool aa):display(d),previous_display(lv_display_get_default()),previous_aa(lv_display_get_antialiasing(d)){
      // Canvas is immediate output, never part of the retained widget tree.
      // Pair the SDK's nesting counter, including an already-disabled caller.
      lv_display_enable_invalidation(display,false);lv_display_set_default(display);lv_display_set_antialiasing(display,aa);
    }
    ~DisplayScope(){lv_display_set_antialiasing(display,previous_aa);lv_display_set_default(previous_display);lv_display_enable_invalidation(display,true);}
  } scope(display,antialias);
  if(!storage.native){auto *memory=lunaros_graphics_allocate_buffer(sizeof(ImmediateWorkspace));if(!memory)return false;storage.native=new(memory) ImmediateWorkspace;}
  auto &workspace=*static_cast<ImmediateWorkspace *>(storage.native);
  workspace.maximum_bytes=storage.maximum_bytes;
  if(!workspace.canvas)workspace.canvas=lv_canvas_create(lv_display_get_layer_sys(display));
  auto *canvas=workspace.canvas;if(!canvas)return false;
  lv_obj_set_hidden(canvas,true);lv_canvas_set_draw_buf(canvas,&buffer);
  CanvasPainter::Impl state{canvas,fonts,{origin_x,origin_y,spec.width,spec.height},
    {origin_x,origin_y,spec.width,spec.height},output.pixels,output.count*2,antialias,false,&workspace};
  CanvasPainter painter(state);
  const bool result=paint(context,painter)&&painter.good();
  // End the borrow while the descriptor is alive. The native object survives;
  // no task, descriptor or framebuffer reference survives this synchronous call.
  lv_canvas_set_buffer(canvas,&workspace.parked_pixel,1,1,LV_COLOR_FORMAT_ARGB8888);
  if(output.coherence)output.coherence->invalidate();
  if(output.damage)output.damage->mark({0,0,spec.width,spec.height});
  return result;
}
} // namespace lunaredu::lunaros::graphics
