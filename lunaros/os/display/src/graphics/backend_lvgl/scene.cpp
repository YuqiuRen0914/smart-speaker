#include "graphics/scene.h"
#include "graphics/canvas_painter.h"
#include "lunaros_ui_metrics.h"
#include "lunaros_ui_theme.h"
#include "graphics_pool.h"
#include "output_copy.h"
#include "fonts.h"
#include "styles.h"
#include <lvgl/lvgl.h>
#include <array>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <limits>
#include <vector>
#include <cmath>
#include <atomic>
#include <initializer_list>
#include <memory>
#include <new>
namespace lunaredu::lunaros::graphics {
namespace {
bool initialized=false;
lv_color_t color(uint16_t v){lv_color16_t packed{};static_assert(sizeof(packed)==sizeof(v));
  std::memcpy(&packed,&v,sizeof(v));return lv_color16_to_color(packed);}
}
struct Scene::Impl {
  enum class Kind { Label, Button, Panel, Progress, Container, Toggle, Slider, Textbox, Keypad, Image, Spinner };
  struct GridTracks {
    std::array<int32_t,9> columns{},rows{};
    unsigned column_count=0,row_count=0;
  };
  struct Slot {
    Impl *owner=nullptr;lv_obj_t *object=nullptr,*text=nullptr;
    lv_obj_t *image_clip=nullptr,*image_object=nullptr;
    int image_width=0,image_height=0;
    std::unique_ptr<GridTracks> grid;
    uint32_t id=0,version=0,parent=0,action=0;Kind kind=Kind::Label;
    int scroll=std::numeric_limits<int>::min();bool horizontal=false,scroll_pending=false;
    std::vector<uint32_t> image_pixels;
    lv_image_dsc_t image{};
    uint64_t attributes=0,image_revision=0,image_style=0;
    bool image_antialias=false,selected=false,touch_feedback=false,cached=false;
    uint16_t button_fill=0;
  };
  // The bounded 33-row startup catalog has a card, two labels and an icon
  // per row. Slots live in the heap-owned Impl, never on the render stack.
  std::array<Slot,160> slots{};
  SceneFonts fonts;
  CanvasPainter::Workspace canvas_workspace;
  SceneStyles styles;
  lv_group_t *focus_group=nullptr;
  std::array<uint32_t,16> focus_ids{};
  size_t focus_count=0;uint32_t focus_version=0;
  bool native_focused(lv_obj_t *object) const {return focus_group&&lv_group_get_focused(focus_group)==object;}
  void retire_focus(){
    if(focus_group)lv_group_delete(focus_group);
    focus_group=nullptr;focus_ids={};focus_count=0;focus_version=0;
  }
  lv_anim_timeline_t *timeline=nullptr;
  uint64_t animation_key=0;
  uint32_t animation_duration=0;
  AnimationCurve animation_curve=AnimationCurve::EaseOut;
  int32_t animation_sample=0;
  void retire_animation(){
    if(timeline)lv_anim_timeline_delete(timeline);
    timeline=nullptr;animation_key=0;animation_duration=0;animation_sample=0;
  }
  void apply_styles(Slot &s){
    switch(s.kind){
      case Kind::Button:
        lv_obj_add_style(s.object,&styles.card,0);
        lv_obj_add_style(s.object,&styles.button_border,LV_STATE_PRESSED);
        lv_obj_add_style(s.object,&styles.button_border,LV_STATE_FOCUSED);break;
      case Kind::Panel:lv_obj_add_style(s.object,&styles.card,0);break;
      case Kind::Progress:case Kind::Toggle:case Kind::Slider:
        lv_obj_add_style(s.object,&styles.track,0);
        lv_obj_add_style(s.object,&styles.indicator,LV_PART_INDICATOR);
        if(s.kind!=Kind::Progress)lv_obj_add_style(s.object,&styles.knob,LV_PART_KNOB);
        break;
      case Kind::Textbox:lv_obj_add_style(s.object,&styles.textbox,0);break;
      case Kind::Container:lv_obj_add_style(s.object,&styles.scrollbar,LV_PART_SCROLLBAR);break;
      case Kind::Keypad:lv_obj_add_style(s.object,&styles.keys,LV_PART_ITEMS);break;
      default:break;
    }
  }
  const lv_font_t *font(unsigned size){const auto *value=fonts.get(size);
    if(!value){failed=true;return &lv_font_montserrat_14;}return value;}
  lv_display_t *display=nullptr;
  uint16_t *retained=nullptr;
  inline static std::atomic<uint64_t> next_raster_owner{1};
  uint64_t raster_owner=0,raster_epoch=0;
  display::FrameDamageMap raster_damage;
  std::array<display::FrameDamageMap,4> raster_history;
  std::atomic<uint32_t> retained_bytes{0},frames{0},image_rebuilds{0},image_cache_hits{0};
  std::atomic<uint32_t> flushes{0},object_creates{0},object_reuses{0},cached_objects{0};std::atomic<uint64_t> raster_pixels{0};
  std::atomic<uint64_t> output_copy_bytes{0},output_compared_bytes{0};
  std::atomic<uint64_t> texture_bytes{0},texture_peak_bytes{0};
  void observe_textures(){
    uint64_t bytes=0;for(const auto &slot:slots)bytes+=slot.image_pixels.capacity()*sizeof(uint32_t);
    texture_bytes.store(bytes);if(bytes>texture_peak_bytes.load())texture_peak_bytes.store(bytes);
  }
  bool texture(Slot &slot,size_t bytes){
    const auto words=bytes/sizeof(uint32_t)+(bytes%sizeof(uint32_t)!=0);
    if(words>slot.image_pixels.capacity()){
      const auto live=size_t(texture_bytes.load());
      if(live>budget.maximum_texture_bytes||words>(budget.maximum_texture_bytes-live)/sizeof(uint32_t))return false;
      std::vector<uint32_t> replacement(words);
      const auto peak=live+replacement.capacity()*sizeof(uint32_t);
      if(peak>texture_peak_bytes.load())texture_peak_bytes.store(peak);
      slot.image_pixels.swap(replacement);
    }else slot.image_pixels.resize(words);
    observe_textures();return true;
  }
  std::atomic<uint64_t> build_time_us{0},raster_time_us{0};
  std::atomic<uint32_t> build_max_us{0},raster_max_us{0};uint64_t build_started=0;
  static uint64_t now_us(){return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
  static void timing(std::atomic<uint64_t> &total,std::atomic<uint32_t> &maximum,uint64_t elapsed){total.fetch_add(elapsed);if(elapsed>maximum.load())maximum.store(uint32_t(std::min<uint64_t>(elapsed,UINT32_MAX)));}
  bool background_set=false,feedback_set=false;
  uint16_t background=0;
  CanvasSpec canvas;
  RasterBudget budget;
  lv_indev_t *pointer=nullptr;
  uint64_t epoch=0;
  uint32_t version=0;
  uint32_t active_slider=0;
  unsigned text_preset=1;
  bool antialias=true,failed=false,rendered=false,building=false,input_pressed=false,invalidated=false,motion=false;
  lv_style_transition_dsc_t feedback{};
  inline static const lv_style_prop_t feedback_properties[]={LV_STYLE_BG_COLOR,LV_STYLE_BORDER_COLOR,LV_STYLE_BORDER_WIDTH,LV_STYLE_PROP_INV};
  Emit emit=nullptr;void *context=nullptr;
  int x=0,y=0;
  void synchronize_button_focus(){
    for(auto &s:slots){
      if(!s.object||s.cached||s.kind!=Kind::Button)continue;
      const bool pressed=lv_obj_has_state(s.object,LV_STATE_PRESSED);
      if(!s.selected&&!native_focused(s.object)&&!s.touch_feedback&&!pressed&&!lv_obj_has_state(s.object,LV_STATE_FOCUSED))continue;
      s.touch_feedback=pressed;
      // The page owns selection unless it explicitly opted into Group focus.
      // Touch temporarily owns feedback; ordinary buttons create no second
      // persistent selection authority.
      if((s.selected||native_focused(s.object))&&!input_pressed)lv_obj_add_state(s.object,LV_STATE_FOCUSED);
      else lv_obj_remove_state(s.object,LV_STATE_FOCUSED);
      // Reapplying these properties cancels their running transitions through
      // LVGL's public style API. A previous control must not fade alongside
      // the newly pressed control, or after a canceled contact.
      lv_obj_set_style_bg_color(s.object,color(s.button_fill),0);
      lv_obj_set_style_border_width(s.object,0,0);
      lv_obj_set_style_border_color(s.object,color(theme::appaccent),0);
    }
  }
  uint64_t signature(std::initializer_list<uint64_t> values) const {
    uint64_t result=1469598103934665603ull;
    for(auto value:values){result^=value;result*=1099511628211ull;}
    result^=uint64_t(antialias)|(uint64_t(motion)<<1)|(uint64_t(fonts.generation())<<32);return result;
  }
  std::array<std::array<char,12>,16> key_labels{};
  std::array<const char *,24> key_map{};
  static void deleted(lv_event_t *event){
    auto &slot=*static_cast<Slot *>(lv_event_get_user_data(event));
    auto *owner=slot.owner;if(owner&&slot.cached)--owner->cached_objects;slot={};
    if(owner)owner->observe_textures();
  }
  static void clicked(lv_event_t *event){
    auto &s=*static_cast<Slot *>(lv_event_get_user_data(event));
    if(!s.cached&&s.owner->emit&&!s.owner->building){
      if(s.owner->focus_group&&lv_obj_get_group(s.object)==s.owner->focus_group)lv_group_focus_obj(s.object);
      s.owner->emit(s.owner->context,{s.owner->epoch,s.id});
    }
  }
  static void changed(lv_event_t *event){
    auto &s=*static_cast<Slot *>(lv_event_get_user_data(event));
    if(!s.owner||!s.owner->emit||s.owner->building)return;
    int value=0;uint32_t id=s.id;
    if(s.kind==Kind::Slider){value=lv_slider_get_value(s.object);if(s.owner->input_pressed)s.owner->active_slider=s.id;}
    else if(s.kind==Kind::Toggle)value=lv_obj_has_state(s.object,LV_STATE_CHECKED);
    else if(s.kind==Kind::Keypad)value=lv_buttonmatrix_get_selected_button(s.object);
    else if(s.kind==Kind::Container){id=s.action;value=s.horizontal?lv_obj_get_scroll_x(s.object):lv_obj_get_scroll_y(s.object);}
    if(id)s.owner->emit(s.owner->context,{s.owner->epoch,id,value});
  }
  static void read(lv_indev_t *indev,lv_indev_data_t *data){
    auto &s=*static_cast<Impl *>(lv_indev_get_user_data(indev));
    data->point={s.x,s.y};data->state=s.input_pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
  }
  void retain_raster(){
    if(retained||canvas.bytes()>budget.maximum_retained_bytes)return;
    retained=static_cast<uint16_t *>(lunaros_graphics_allocate_buffer(canvas.bytes()));
    if(!retained)return;
    raster_owner=next_raster_owner.fetch_add(1);raster_epoch=0;
    retained_bytes.store(uint32_t(canvas.bytes()));
    std::fill_n(retained,canvas.pixels(),0);
    lv_display_set_buffers_with_stride(display,retained,nullptr,uint32_t(canvas.bytes()),uint32_t(canvas.stride*2),LV_DISPLAY_RENDER_MODE_DIRECT);
  }
  bool setup(bool keep_raster=true){
    if(display){if(keep_raster)retain_raster();return true;}
    if(!initialized){
      if(!lunaros_graphics_allocate_pool(LV_MEM_SIZE))return false;
      lv_init();
      lv_tick_set_cb([]()->uint32_t{
        return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
      });
      initialized=true;
    }
    styles.init(color(theme::appaccent),color(theme::raised),color(theme::text),color(theme::secondary));
    display=lv_display_create(canvas.width,canvas.height);if(!display)return false;
    lv_display_set_user_data(display,this);
    // Style/animation invalidation can resume this timer from INSIDE another
    // timer callback. Replace automatic drawing with an OS redraw request;
    // render() alone calls lv_refr_now with a freshly borrowed frame buffer.
    lv_timer_set_cb(lv_display_get_refr_timer(display),[](lv_timer_t *timer){
      auto *display=static_cast<lv_display_t *>(lv_timer_get_user_data(timer));
      static_cast<Impl *>(lv_display_get_user_data(display))->invalidated=true;
      lv_timer_pause(timer);
    });
    lv_display_add_event_cb(display,[](lv_event_t *event){
      static_cast<Impl *>(lv_event_get_user_data(event))->invalidated=true;
    },LV_EVENT_INVALIDATE_AREA,this);
    // Full refresh emits REFR_REQUEST without INVALIDATE_AREA. Observe it
    // immediately; the automatic refresh timer remains paused between frames.
    lv_display_add_event_cb(display,[](lv_event_t *event){
      static_cast<Impl *>(lv_event_get_user_data(event))->invalidated=true;
    },LV_EVENT_REFR_REQUEST,this);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_antialiasing(display,antialias);
    lv_display_set_flush_cb(display,[](lv_display_t *d,const lv_area_t *area,uint8_t *){
      auto &self=*static_cast<Impl *>(lv_display_get_user_data(d));++self.flushes;
      self.raster_damage.mark({area->x1,area->y1,area->x2-area->x1+1,area->y2-area->y1+1});
      self.raster_pixels.fetch_add(uint64_t(area->x2-area->x1+1)*uint64_t(area->y2-area->y1+1));lv_display_flush_ready(d);
    });
    lv_timer_pause(lv_display_get_refr_timer(display));
    auto *root=lv_display_get_screen_active(display);
    lv_obj_remove_style_all(root);lv_obj_set_scrollable(root,false);
    lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);
    if(keep_raster)retain_raster();
    pointer=lv_indev_create();
    if(!pointer){lv_display_delete(display);display=nullptr;lunaros_graphics_free_buffer(retained);retained=nullptr;retained_bytes.store(0);return false;}
    lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);lv_indev_set_display(pointer,display);
    lv_indev_set_user_data(pointer,this);lv_indev_set_read_cb(pointer,read);
    lv_indev_set_mode(pointer,LV_INDEV_MODE_EVENT);
    return true;
  }
  // Retain only leaf structures whose complete state belongs to this scene.
  // Textareas, key maps, image textures and container subtrees retire normally.
  void retire_slot(Slot &s){
    if(!s.object||s.cached)return;
    const bool simple=s.kind==Kind::Label||s.kind==Kind::Button||s.kind==Kind::Panel;
    const unsigned own_children=s.kind==Kind::Button?1:0;
    if(!simple||lv_obj_get_child_count(s.object)!=own_children||cached_objects.load()>=32){lv_obj_delete(s.object);return;}
    lv_obj_remove_style_all(s.object);lv_anim_delete(s.object,nullptr);
    lv_obj_remove_state(s.object,LV_STATE_ANY);
    if(s.text){
      lv_obj_remove_style_all(s.text);lv_anim_delete(s.text,nullptr);
      lv_label_set_text_static(s.text,"");
    }
    lv_obj_set_hidden(s.object,true);
    lv_obj_set_parent(s.object,lv_display_get_screen_active(display));
    s.cached=true;s.id=s.version=s.parent=s.action=0;s.attributes=0;
    s.selected=s.touch_feedback=false;
    ++cached_objects;
  }
  void retire_stale(bool all){
    // Children must detach before their old parent is deleted. Iterate leaves
    // first; DELETE callbacks clear remaining subtree slots automatically.
    for(auto &s:slots)if(s.object&&!s.cached&&(all||s.version!=version)&&
      lv_obj_get_child_count(s.object)==(s.kind==Kind::Button?1u:0u))retire_slot(s);
    for(auto &s:slots)if(s.object&&!s.cached&&(all||s.version!=version))retire_slot(s);
  }
  Slot *get(uint32_t id,Kind kind,uint32_t parent=0){
    if(!display||!version){failed=true;return nullptr;}
    if(!id){failed=true;return nullptr;}
    Slot *free=nullptr,*reuse=nullptr,*evict=nullptr;
    for(auto &s:slots){
      if(!s.object){if(!free)free=&s;continue;}
      if(s.cached){if(!evict)evict=&s;if(!reuse&&s.kind==kind)reuse=&s;continue;}
      if(s.id==id){
        if(s.parent==parent&&((s.kind==Kind::Panel&&kind==Kind::Button)||(s.kind==Kind::Button&&kind==Kind::Panel))){lv_obj_delete(s.object);if(!free)free=&s;continue;}
        if(s.kind!=kind||s.parent!=parent){failed=true;return nullptr;}s.version=version;return &s;
      }
    }
    if(!free&&!reuse&&evict){lv_obj_delete(evict->object);free=evict;}
    if(!free&&!reuse){failed=true;return nullptr;}
    auto *root=lv_display_get_screen_active(display);
    if(parent){bool found=false;for(auto &s:slots)if(s.object&&!s.cached&&s.id==parent&&(s.kind==Kind::Container||s.kind==Kind::Button||s.kind==Kind::Panel)){root=s.object;found=true;break;}if(!found){failed=true;return nullptr;}}
    if(reuse){
      reuse->cached=false;--cached_objects;++object_reuses;
      reuse->id=id;reuse->version=version;reuse->parent=parent;
      lv_obj_set_parent(reuse->object,root);lv_obj_move_to_index(reuse->object,-1);
      lv_obj_set_hidden(reuse->object,false);
      lv_obj_set_floating(reuse->object,false);
      apply_styles(*reuse);
      return reuse;
    }
    lv_obj_t *o=nullptr;
    switch(kind){
      case Kind::Label:o=lv_label_create(root);break;
      case Kind::Button:o=lv_button_create(root);break;
      case Kind::Panel:o=lv_obj_create(root);break;
      case Kind::Progress:o=lv_bar_create(root);break;
      case Kind::Container:o=lv_obj_create(root);break;
      case Kind::Toggle:o=lv_switch_create(root);break;
      case Kind::Slider:o=lv_slider_create(root);break;
      case Kind::Textbox:o=lv_textarea_create(root);break;
      case Kind::Keypad:o=lv_buttonmatrix_create(root);break;
      case Kind::Image:o=lv_obj_create(root);break;
      case Kind::Spinner:o=lv_spinner_create(root);break;
    }
    if(!o){failed=true;return nullptr;}
    ++object_creates;
    free->owner=this;free->object=o;free->id=id;free->version=version;free->kind=kind;free->parent=parent;
    lv_obj_add_event_cb(o,deleted,LV_EVENT_DELETE,free);
    lv_obj_remove_style_all(o);lv_obj_set_scrollable(o,false);
    apply_styles(*free);
    if(kind==Kind::Button){
      lv_obj_set_click_focusable(o,false);
      free->text=lv_label_create(o);if(!free->text){lv_obj_delete(o);*free={};failed=true;return nullptr;}
      lv_obj_remove_style_all(free->text);
      lv_obj_add_event_cb(o,clicked,LV_EVENT_CLICKED,free);
      lv_obj_set_ignore_layout(free->text,true);
    }else if(kind==Kind::Image){
      free->image_clip=lv_obj_create(o);
      if(!free->image_clip){lv_obj_delete(o);failed=true;return nullptr;}
      lv_obj_remove_style_all(free->image_clip);lv_obj_set_scrollable(free->image_clip,false);
      lv_obj_set_clickable(o,false);lv_obj_set_clickable(free->image_clip,false);
      free->image_object=lv_image_create(free->image_clip);
      if(!free->image_object){lv_obj_delete(o);failed=true;return nullptr;}
      lv_obj_remove_style_all(free->image_object);lv_obj_set_clickable(free->image_object,false);
      lv_image_set_inner_align(free->image_object,LV_IMAGE_ALIGN_STRETCH);
    }else if(kind==Kind::Label)free->text=o;
    else if(kind==Kind::Container)lv_obj_add_event_cb(o,changed,LV_EVENT_SCROLL_END,free);
    else if(kind==Kind::Slider||kind==Kind::Toggle||kind==Kind::Keypad)lv_obj_add_event_cb(o,changed,LV_EVENT_VALUE_CHANGED,free);
    return free;
  }
  void position(lv_obj_t *o,Rect r){lv_obj_set_pos(o,r.x,r.y);lv_obj_set_size(o,r.width,r.height);}
  void image_geometry(Slot &slot,Rect r,bool rounded){
    position(slot.object,r);slot.image_width=r.width;slot.image_height=r.height;
    const int width=rounded?std::min(r.width,r.height):r.width;
    const int height=rounded?width:r.height;
    const int x=(r.width-width)/2,y=(r.height-height)/2;
    position(slot.image_clip,{x,y,width,height});
    lv_obj_set_style_radius(slot.image_clip,rounded?LV_RADIUS_CIRCLE:0,0);
    lv_obj_set_style_clip_corner(slot.image_clip,rounded,0);
    position(slot.image_object,{-x,-y,r.width,r.height});
    lv_image_set_antialias(slot.image_object,antialias);
  }
  bool text(Slot &s,const char *v,unsigned size,uint16_t ink,Align align,size_t limit=255){
    if(!v)v="";
    if(std::strlen(v)>limit){failed=true;return false;}
    if(std::strcmp(lv_label_get_text(s.text),v))lv_label_set_text(s.text,v);
    lv_label_set_long_mode(s.text,LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_font(s.text,font(size),0);
    lv_obj_set_style_text_color(s.text,color(ink),0);
    lv_obj_set_style_text_align(s.text,align==Align::Left?LV_TEXT_ALIGN_LEFT:align==Align::Right?LV_TEXT_ALIGN_RIGHT:LV_TEXT_ALIGN_CENTER,0);
    return true;
  }
};
bool Scene::animation_value(uint64_t key,uint32_t elapsed,uint32_t duration,float &value,AnimationCurve curve){
  if(!duration||duration>INT32_MAX||!impl_->setup(false))return false;
  if(!impl_->timeline||key!=impl_->animation_key||duration!=impl_->animation_duration||curve!=impl_->animation_curve){
    impl_->retire_animation();
    // The fixed upstream add() has a void allocation API. Admit a single
    // descriptor only with contiguous headroom on this sole LVGL owner.
    if(Scene::memory().largest_free<sizeof(lv_anim_timeline_dsc_t)+1024)return false;
    impl_->timeline=lv_anim_timeline_create();if(!impl_->timeline)return false;
    lv_anim_t track;lv_anim_init(&track);lv_anim_set_var(&track,impl_.get());
    lv_anim_set_values(&track,0,32768);lv_anim_set_duration(&track,duration);
    lv_anim_set_path_cb(&track,curve==AnimationCurve::Linear?lv_anim_path_linear:
      curve==AnimationCurve::EaseInOut?lv_anim_path_ease_in_out:lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&track,[](void *p,int32_t value){static_cast<Impl *>(p)->animation_sample=value;});
    lv_anim_timeline_add(impl_->timeline,0,&track);
    if(lv_anim_timeline_get_playtime(impl_->timeline)!=duration){impl_->retire_animation();return false;}
    impl_->animation_key=key;impl_->animation_duration=duration;impl_->animation_curve=curve;
  }
  // Round up to retain the requested integer millisecond after the official
  // 16-bit progress-to-time mapping. The OS still gates physical presentation.
  const uint16_t progress=elapsed>=duration?UINT16_MAX:
    uint16_t((uint64_t(elapsed)*UINT16_MAX+duration-1)/duration);
  lv_anim_timeline_set_progress(impl_->timeline,progress);
  value=std::clamp(impl_->animation_sample/32768.f,0.f,1.f);
  if(elapsed>=duration)impl_->retire_animation();
  return true;
}
void Scene::cancel_animation(){impl_->retire_animation();}
bool Scene::paint_canvas(RasterOutput output,bool antialias,void *context,bool (*paint)(void *,CanvasPainter &)){
  return paint_canvas(impl_->canvas,output,antialias,context,paint);
}
bool Scene::paint_canvas(CanvasSpec target,RasterOutput output,bool antialias,void *context,bool (*paint)(void *,CanvasPainter &)){
  if(!target.valid()||target.width>impl_->canvas.width||target.height>impl_->canvas.height||target.stride>UINT16_MAX/2)return false;
  if(!impl_->setup(false))return false;
  const bool building=impl_->building;impl_->building=true;
  const bool result=CanvasPainter::paint_on(impl_->canvas_workspace,impl_->display,&impl_->fonts,target,output,antialias,context,paint);
  impl_->building=building;return result;
}
Scene::Scene():impl_(new Impl){}
Scene::~Scene(){suspend();}
bool Scene::configure(CanvasSpec canvas,RasterBudget budget){
  // LVGL's public image/draw-buffer header stores byte stride in 16 bits.
  if(!canvas.valid()||canvas.stride>UINT16_MAX/sizeof(uint16_t))return false;
  if(impl_->display)return canvas==impl_->canvas &&
      budget.maximum_retained_bytes==impl_->budget.maximum_retained_bytes &&
      budget.maximum_texture_bytes==impl_->budget.maximum_texture_bytes&&
      budget.maximum_canvas_cache_bytes==impl_->budget.maximum_canvas_cache_bytes;
  impl_->canvas=canvas;impl_->budget=budget;impl_->canvas_workspace.maximum_bytes=std::min<size_t>(64*1024,budget.maximum_canvas_cache_bytes);return true;
}
CanvasSpec Scene::canvas() const{return impl_->canvas;}
bool Scene::text_scale(unsigned percent){
  unsigned preset=0;
  while(preset<metrics::text_percentages.size()&&metrics::text_percentages[preset]!=percent)++preset;
  if(preset==metrics::text_percentages.size())return false;
  if(impl_->text_preset!=preset){impl_->text_preset=preset;impl_->invalidated=true;}
  return true;
}
void Scene::motion(bool enabled){
  if(impl_->feedback_set&&impl_->motion==enabled)return;
  impl_->feedback_set=true;
  impl_->motion=enabled;
  lv_style_transition_dsc_init(&impl_->feedback,Impl::feedback_properties,lv_anim_path_ease_out,
      enabled?design::controlFeedbackMs:0,0,nullptr);
}
void Scene::reset(bool retain_objects){
  impl_->retire_animation();
  if(!impl_->display)return;
  impl_->building=true;
  impl_->retire_focus();
  impl_->input_pressed=false;impl_->active_slider=0;
  lv_indev_reset(impl_->pointer,nullptr);
  lv_indev_read(impl_->pointer); // Complete reset with an actual released sample.
  if(retain_objects)impl_->retire_stale(true);
  else lv_obj_clean(lv_display_get_screen_active(impl_->display));
  impl_->fonts.clear();
  impl_->rendered=false;
  lv_timer_pause(lv_display_get_refr_timer(impl_->display));
}
void Scene::clear(){reset(false);}
void Scene::suspend(){
  impl_->canvas_workspace.reset();
  clear();
  if(impl_->pointer)lv_indev_delete(impl_->pointer);
  impl_->pointer=nullptr;
  if(impl_->display)lv_display_delete(impl_->display);
  impl_->display=nullptr;
  impl_->styles.reset();
  impl_->fonts.clear();
  lunaros_graphics_free_buffer(impl_->retained);impl_->retained=nullptr;
  impl_->retained_bytes.store(0);
  impl_->emit=nullptr;impl_->context=nullptr;impl_->epoch=0;impl_->version=0;
  impl_->background_set=false;impl_->rendered=false;impl_->invalidated=false;
}
bool Scene::begin(uint64_t epoch,uint16_t background,bool antialias,Emit emit,void *context){
  impl_->build_started=Impl::now_us();
  impl_->rendered=false;
  if(!epoch||!impl_->setup())return false;
  if(epoch!=impl_->epoch)reset(true);
  if(impl_->antialias!=antialias){lv_display_set_antialiasing(impl_->display,antialias);lv_obj_invalidate(lv_display_get_screen_active(impl_->display));}
  impl_->epoch=epoch;impl_->antialias=antialias;impl_->emit=emit;impl_->context=context;
  impl_->failed=false;impl_->rendered=false;
  impl_->building=true;
  if(++impl_->version==0){reset(true);impl_->version=1;}
  if(!impl_->background_set||impl_->background!=background){impl_->background=background;impl_->background_set=true;lv_obj_set_style_bg_color(lv_display_get_screen_active(impl_->display),color(background),0);}
  return true;
}
bool Scene::label(uint32_t id,Rect r,const char *v,unsigned size,uint16_t ink,Align align,uint32_t parent,bool ellipsis){
  size=metrics::scaled_text_size(size,impl_->text_preset);
  auto *s=impl_->get(id,Impl::Kind::Label,parent);if(!s)return false;
  const auto signature=impl_->signature({uint64_t(r.x),uint64_t(r.y),uint64_t(r.width),uint64_t(r.height),size,ink,unsigned(align),ellipsis});
  if(s->attributes==signature&&!std::strcmp(lv_label_get_text(s->text),v?v:""))return true;
  impl_->position(s->object,r);
  if(!impl_->text(*s,v,size,ink,align,ellipsis?4096:255))return false;
  if(ellipsis)lv_label_set_long_mode(s->text,LV_LABEL_LONG_MODE_DOTS);
  // Center the official font line box, including shorter declared boxes.
  lv_obj_set_style_pad_top(s->text,(r.height-lv_font_get_line_height(impl_->font(size)))/2,0);
  s->attributes=signature;return true;
}
bool Scene::image(uint32_t id,Rect r,ImageView image,uint32_t parent){
  if(!image.width||!image.height||image.width>UINT16_MAX/2||image.height>UINT16_MAX||image.stride<image.width||
      size_t(image.height)>SIZE_MAX/size_t(image.stride)/(image.pixels?sizeof(uint16_t):1)||
      (!image.pixels&&!image.alpha)||image.sampling< -1||image.sampling>1||unsigned(image.fit)>unsigned(ImageFit::Cover)||
      r.width<=0||r.height<=0||r.width>impl_->canvas.width||r.height>impl_->canvas.height){impl_->failed=true;return false;}
  auto *s=impl_->get(id,Impl::Kind::Image,parent);if(!s)return false;
  const bool sampling=image.sampling<0?impl_->antialias:image.sampling!=0;
  const auto format=image.pixels?(image.alpha?LV_COLOR_FORMAT_RGB565A8:LV_COLOR_FORMAT_RGB565):LV_COLOR_FORMAT_A8;
  const auto signature=impl_->signature({unsigned(r.width),unsigned(r.height),image.width,image.height,image.stride,image.color,image.rounded,unsigned(image.fit),sampling,unsigned(format)});
  if(s->image_style!=signature){
    impl_->image_geometry(*s,r,image.rounded);
    lv_image_set_inner_align(s->image_object,image.fit==ImageFit::Contain?LV_IMAGE_ALIGN_CONTAIN:
      image.fit==ImageFit::Cover?LV_IMAGE_ALIGN_COVER:LV_IMAGE_ALIGN_STRETCH);
    lv_image_set_antialias(s->image_object,sampling);
    lv_obj_set_style_image_recolor(s->image_object,color(image.color),0);
    lv_obj_set_style_image_recolor_opa(s->image_object,image.pixels?LV_OPA_TRANSP:LV_OPA_COVER,0);
    s->image_style=signature;
  }else impl_->position(s->object,r);
  if(image.revision&&s->image_revision==image.revision&&s->attributes==signature){++impl_->image_cache_hits;return true;}
  bool changed=s->image.header.w!=image.width||s->image.header.h!=image.height||s->attributes!=signature;
  // Keep an immutable source snapshot in its native format. For RGB565A8,
  // pinned LVGL expects RGB rows followed by A8 rows at half the RGB stride.
  // texture() bounds the live old + replacement capacities, including padding.
  const size_t texels=size_t(image.width)*image.height;
  const unsigned bytes_per_texel=(image.pixels?2:0)+(image.alpha?1:0);
  if(texels>UINT32_MAX/bytes_per_texel){impl_->failed=true;return false;}
  const size_t color_bytes=image.pixels?texels*sizeof(uint16_t):0;
  const size_t bytes=color_bytes+(image.alpha?texels:0);
  if(bytes>UINT32_MAX||!impl_->texture(*s,bytes)){impl_->failed=true;return false;}
  auto *target=reinterpret_cast<uint8_t *>(s->image_pixels.data());
  const auto copy=[&](uint8_t *to,const void *from,size_t count){
    if(std::memcmp(to,from,count)){lv_memcpy(to,from,count);changed=true;}
  };
  for(unsigned y=0;y<image.height;++y){
    if(image.pixels)copy(target+size_t(y)*image.width*2,image.pixels+size_t(y)*image.stride,size_t(image.width)*2);
    if(image.alpha){
      auto *alpha=target+color_bytes+size_t(y)*image.width;
      const auto *source=image.alpha+size_t(y)*image.stride;
      if(impl_->antialias)copy(alpha,source,image.width);
      else for(unsigned x=0;x<image.width;++x){
        const uint8_t value=source[x]>=128?255:0;
        changed|=alpha[x]!=value;alpha[x]=value;
      }
    }
  }
  s->attributes=signature;s->image_revision=image.revision;s->image_antialias=sampling;
  if(!changed){++impl_->image_cache_hits;return true;}
  ++impl_->image_rebuilds;
  s->image={};s->image.header.magic=LV_IMAGE_HEADER_MAGIC;s->image.header.cf=format;
  s->image.header.w=image.width;s->image.header.h=image.height;s->image.header.stride=image.width*(image.pixels?2:1);
  s->image.data_size=uint32_t(bytes);s->image.data=target;
  lv_image_set_src(s->image_object,&s->image);lv_obj_invalidate(s->image_object);return true;
}
bool Scene::canvas(uint32_t id,Rect r,uint16_t background,void *context,bool (*paint)(void *,CanvasPainter &),uint32_t parent){
  if(!paint||r.x<0||r.y<0||r.width<=0||r.height<=0||r.width>impl_->canvas.width||r.height>impl_->canvas.height||
      r.x>impl_->canvas.width-r.width||r.y>impl_->canvas.height-r.height||r.width>UINT16_MAX/2){impl_->failed=true;return false;}
  auto *s=impl_->get(id,Impl::Kind::Image,parent);if(!s)return false;
  const CanvasSpec spec{uint16_t(r.width),uint16_t(r.height),uint16_t(r.width)};
  if(!impl_->texture(*s,spec.bytes())){impl_->failed=true;return false;}
  struct Context {Rect bounds;uint16_t background;void *context;bool (*paint)(void *,CanvasPainter &);} work{r,background,context,paint};
  const bool building=impl_->building;impl_->building=true;
  const bool result=CanvasPainter::paint_on(impl_->canvas_workspace,impl_->display,&impl_->fonts,spec,
    {reinterpret_cast<uint16_t *>(s->image_pixels.data()),spec.pixels()},impl_->antialias,&work,
    [](void *p,CanvasPainter &canvas){const auto &c=*static_cast<Context *>(p);
      return canvas.rectangle(c.bounds,c.background)&&c.paint(c.context,canvas);
    },r.x,r.y);
  impl_->building=building;
  if(!result){impl_->failed=true;return false;}
  impl_->image_geometry(*s,r,false);
  s->image_style=0;
  lv_image_set_inner_align(s->image_object,LV_IMAGE_ALIGN_STRETCH);
  lv_obj_set_style_image_recolor_opa(s->image_object,LV_OPA_TRANSP,0);
  s->attributes=0;s->image_revision=0;s->image_antialias=impl_->antialias;
  s->image={};s->image.header.magic=LV_IMAGE_HEADER_MAGIC;s->image.header.cf=LV_COLOR_FORMAT_RGB565;
  s->image.header.w=r.width;s->image.header.h=r.height;s->image.header.stride=r.width*2;
  s->image.data_size=uint32_t(spec.bytes());s->image.data=reinterpret_cast<const uint8_t *>(s->image_pixels.data());
  lv_image_set_src(s->image_object,&s->image);lv_obj_invalidate(s->image_object);++impl_->image_rebuilds;
  return true;
}
bool Scene::flow(uint32_t id,Rect r,bool horizontal,int gap,uint32_t parent){
  if(r.width<=0||r.height<=0||gap<0){impl_->failed=true;return false;}
  auto *s=impl_->get(id,Impl::Kind::Container,parent);if(!s)return false;
  // Switch off grid layout before releasing a former grid's owned tracks.
  lv_obj_set_layout(s->object,LV_LAYOUT_FLEX);
  lv_obj_set_style_grid_column_dsc_array(s->object,nullptr,0);
  lv_obj_set_style_grid_row_dsc_array(s->object,nullptr,0);
  s->grid.reset();s->action=0;s->scroll_pending=false;
  impl_->position(s->object,r);lv_obj_set_scrollable(s->object,false);
  lv_obj_set_flex_flow(s->object,horizontal?LV_FLEX_FLOW_ROW:LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s->object,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_all(s->object,0,0);lv_obj_set_style_pad_column(s->object,gap,0);lv_obj_set_style_pad_row(s->object,gap,0);
  return true;
}
bool Scene::flow_item(uint32_t id,unsigned grow,unsigned order){
  if(grow>255||(order!=UINT32_MAX&&order>=impl_->slots.size())){impl_->failed=true;return false;}
  for(auto &s:impl_->slots)if(s.object&&!s.cached&&s.id==id){
    lv_obj_set_floating(s.object,false);
    lv_obj_set_flex_grow(s.object,uint8_t(grow));
    if(order!=UINT32_MAX){const auto count=lv_obj_get_child_count(lv_obj_get_parent(s.object));
      if(count)lv_obj_move_to_index(s.object,int32_t(std::min<uint32_t>(order,count-1)));}
    if(s.kind==Impl::Kind::Button){
      if(lv_obj_get_style_layout(s.object,LV_PART_MAIN)==LV_LAYOUT_NONE)lv_obj_set_style_pad_hor(s.object,4,0);
      lv_obj_set_width(s.text,lv_pct(100));
    }
    return true;
  }
  impl_->failed=true;return false;
}
bool Scene::overlay(uint32_t id,Rect r){
  for(auto &s:impl_->slots)if(s.object&&!s.cached&&s.id==id){
    auto *parent=lv_obj_get_parent(s.object);
    lv_obj_set_floating(s.object,true);
    lv_area_t border{},content;lv_obj_get_coords(parent,&border);lv_obj_get_content_coords(parent,&content);
    r.x-=content.x1-border.x1;r.y-=content.y1-border.y1;
    impl_->position(s.object,r);return true;
  }
  impl_->failed=true;return false;
}
bool Scene::flex_layout(uint32_t id,bool horizontal,int gap,int inset,Align main,int trailing_inset){
  if(gap<0||inset<0||trailing_inset< -1){impl_->failed=true;return false;}
  for(auto &s:impl_->slots)if(s.object&&!s.cached&&s.id==id){
    lv_obj_set_flex_flow(s.object,horizontal?LV_FLEX_FLOW_ROW:LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s.object,main==Align::Left?LV_FLEX_ALIGN_START:
      main==Align::Right?LV_FLEX_ALIGN_END:LV_FLEX_ALIGN_CENTER,
      horizontal?LV_FLEX_ALIGN_CENTER:LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(s.object,0,0);lv_obj_set_style_pad_hor(s.object,inset,0);
    if(trailing_inset>=0)lv_obj_set_style_pad_right(s.object,trailing_inset,0);
    lv_obj_set_style_pad_column(s.object,gap,0);lv_obj_set_style_pad_row(s.object,gap,0);
    // The native Button label is still useful for the one-slot simple row.
    // Rich cards supply their own children and exclude that empty label.
    if(s.kind==Impl::Kind::Button)lv_obj_set_ignore_layout(s.text,true);
    return true;
  }
  impl_->failed=true;return false;
}
bool Scene::grid(uint32_t id,Rect r,unsigned columns,unsigned rows,int column_gap,int row_gap,uint32_t parent){
  if(!columns||columns>8||!rows||rows>8||column_gap<0||row_gap<0||r.width<=0||r.height<=0){impl_->failed=true;return false;}
  auto *s=impl_->get(id,Impl::Kind::Container,parent);if(!s)return false;
  if(!s->grid){
    unsigned grids=0;for(const auto &slot:impl_->slots)grids+=bool(slot.grid);
    if(grids>=8){impl_->failed=true;return false;}
    s->grid.reset(new(std::nothrow) Impl::GridTracks);
    if(!s->grid){impl_->failed=true;return false;}
  }
  auto &tracks=*s->grid;
  s->action=0;s->scroll_pending=false;
  impl_->position(s->object,r);lv_obj_set_scrollable(s->object,false);
  tracks.columns.fill(LV_GRID_TEMPLATE_LAST);tracks.rows.fill(LV_GRID_TEMPLATE_LAST);
  for(unsigned n=0;n<columns;++n)tracks.columns[n]=LV_GRID_FR(1);
  for(unsigned n=0;n<rows;++n)tracks.rows[n]=LV_GRID_FR(1);
  tracks.column_count=columns;tracks.row_count=rows;
  lv_obj_set_style_pad_all(s->object,0,0);lv_obj_set_style_pad_column(s->object,column_gap,0);lv_obj_set_style_pad_row(s->object,row_gap,0);
  lv_obj_set_grid_dsc_array(s->object,tracks.columns.data(),tracks.rows.data());lv_obj_mark_layout_as_dirty(s->object);return true;
}
bool Scene::grid_item(uint32_t id,unsigned column,unsigned row,unsigned column_span,unsigned row_span){
  for(auto &s:impl_->slots)if(s.object&&!s.cached&&s.id==id){
    Impl::Slot *parent=nullptr;for(auto &p:impl_->slots)if(p.object&&!p.cached&&p.id==s.parent){parent=&p;break;}
    if(!parent||!parent->grid||!column_span||!row_span)break;
    const auto &tracks=*parent->grid;
    if(column>=tracks.column_count||row>=tracks.row_count||
        column_span>tracks.column_count-column||row_span>tracks.row_count-row)break;
    lv_obj_set_floating(s.object,false);
    if(s.kind==Impl::Kind::Label){
      // Let the official font line box center inside the cell. Do not retain
      // padding calculated for the caller's provisional rectangle height.
      lv_obj_set_style_pad_top(s.object,0,0);lv_obj_set_height(s.object,LV_SIZE_CONTENT);
    }
    lv_obj_set_grid_cell(s.object,LV_GRID_ALIGN_STRETCH,column,column_span,
      s.kind==Impl::Kind::Label?LV_GRID_ALIGN_CENTER:LV_GRID_ALIGN_STRETCH,row,row_span);return true;
  }
  impl_->failed=true;return false;
}
bool Scene::focus_order(const uint32_t *ids,size_t count,uint32_t initial){
  if(!ids||!count||count>impl_->focus_ids.size()||!impl_->display||!impl_->version){impl_->failed=true;return false;}
  std::array<lv_obj_t *,16> objects{};
  for(size_t i=0;i<count;++i){
    for(size_t j=0;j<i;++j)if(ids[i]==ids[j]){impl_->failed=true;return false;}
    for(auto &s:impl_->slots)if(s.object&&!s.cached&&s.id==ids[i]&&s.version==impl_->version&&s.kind==Impl::Kind::Button){objects[i]=s.object;break;}
    if(!objects[i]){impl_->failed=true;return false;}
  }
  bool changed=!impl_->focus_group||impl_->focus_count!=count||!std::equal(ids,ids+count,impl_->focus_ids.begin());
  if(!changed)for(size_t i=0;i<count;++i)changed|=lv_obj_get_group(objects[i])!=impl_->focus_group;
  if(changed){
    if(memory().largest_free<16*1024){impl_->failed=true;return false;}
    auto *old=impl_->focus_group?lv_group_get_focused(impl_->focus_group):nullptr;
    uint32_t previous=0;for(const auto &s:impl_->slots)if(s.object==old&&!s.cached){previous=s.id;break;}
    if(!impl_->focus_group)impl_->focus_group=lv_group_create();
    if(!impl_->focus_group){impl_->failed=true;return false;}
    lv_group_remove_all_objs(impl_->focus_group);lv_group_set_wrap(impl_->focus_group,true);
    for(size_t i=0;i<count;++i)lv_group_add_obj(impl_->focus_group,objects[i]);
    const auto restore=[&](uint32_t id){
      for(size_t i=0;i<count;++i)if(ids[i]==id&&!lv_obj_has_state(objects[i],LV_STATE_DISABLED)){lv_group_focus_obj(objects[i]);return true;}
      return false;
    };
    if(!restore(previous))(void)restore(initial);
    std::copy_n(ids,count,impl_->focus_ids.begin());impl_->focus_count=count;
  }
  auto *focused=lv_group_get_focused(impl_->focus_group);
  if(focused&&lv_obj_has_state(focused,LV_STATE_DISABLED))lv_group_focus_next(impl_->focus_group);
  impl_->focus_version=impl_->version;return true;
}
bool Scene::focus(FocusCommand command){
  if(!impl_->rendered||impl_->failed||impl_->building||impl_->input_pressed||!impl_->focus_group)return false;
  switch(command){
    case FocusCommand::Next:lv_group_focus_next(impl_->focus_group);break;
    case FocusCommand::Previous:lv_group_focus_prev(impl_->focus_group);break;
    case FocusCommand::Activate:{
      auto *object=lv_group_get_focused(impl_->focus_group);
      if(!object||lv_obj_has_state(object,LV_STATE_DISABLED)||lv_obj_is_hidden(object)||!lv_obj_is_clickable(object))return false;
      lv_obj_send_event(object,LV_EVENT_CLICKED,nullptr);break;
    }
    default:return false;
  }
  impl_->synchronize_button_focus();return true;
}
bool Scene::mask(uint32_t id,Rect r,assets::MaskView view,assets::MaskBounds crop,
    uint16_t ink,uint64_t revision,uint32_t parent,bool aa){
  if(!view.data||!view.width||!view.height||crop.x<0||crop.y<0||crop.width<=0||crop.height<=0||
      crop.width>view.width||crop.height>view.height||crop.x>view.width-crop.width||crop.y>view.height-crop.height||
      (view.format!=assets::MaskFormat::Mono1&&view.format!=assets::MaskFormat::Alpha8)||
      view.stride<(view.format==assets::MaskFormat::Mono1?(view.width+7)/8:view.width)||
      r.width<=0||r.height<=0||r.width>impl_->canvas.width||r.height>impl_->canvas.height){impl_->failed=true;return false;}
  auto *s=impl_->get(id,Impl::Kind::Image,parent);if(!s)return false;
  const bool sampling=aa&&impl_->antialias;
  // Geometry and tint belong to the native Image object. Only source coverage
  // changes require another unpack; resizing an icon keeps its owned texels.
  const auto signature=impl_->signature({uint64_t(reinterpret_cast<uintptr_t>(view.data)),uint64_t(crop.x),uint64_t(crop.y),
    uint64_t(crop.width),uint64_t(crop.height),uint64_t(view.format),view.stride,sampling,0x4138});
  const auto style=impl_->signature({uint64_t(r.width),uint64_t(r.height),ink,sampling,0x4d41534b});
  if(s->image_style!=style){
    impl_->image_geometry(*s,r,false);
    lv_image_set_inner_align(s->image_object,LV_IMAGE_ALIGN_CONTAIN);
    lv_image_set_antialias(s->image_object,sampling);
    lv_obj_set_style_image_recolor(s->image_object,color(ink),0);
    lv_obj_set_style_image_recolor_opa(s->image_object,LV_OPA_COVER,0);
    s->image_style=style;
  }else impl_->position(s->object,r);
  if(revision&&s->image_revision==revision&&s->attributes==signature){++impl_->image_cache_hits;return true;}
  const auto bytes=size_t(crop.width)*size_t(crop.height);
  if(!impl_->texture(*s,bytes)){impl_->failed=true;return false;}
  auto *pixels=reinterpret_cast<uint8_t *>(s->image_pixels.data());
  for(int y=0;y<crop.height;++y){
    const auto *row=view.data+size_t(crop.y+y)*view.stride;
    if(view.format==assets::MaskFormat::Alpha8&&sampling)std::memcpy(pixels+size_t(y)*crop.width,row+crop.x,crop.width);
    else for(int x=0;x<crop.width;++x){
      const int sx=crop.x+x;
      const auto alpha=view.format==assets::MaskFormat::Mono1?(row[sx/8]&(0x80u>>(sx%8))?255:0):row[sx];
      pixels[size_t(y)*crop.width+x]=sampling?uint8_t(alpha):alpha>=128?255:0;
    }
  }
  s->image={};s->image.header.magic=LV_IMAGE_HEADER_MAGIC;s->image.header.cf=LV_COLOR_FORMAT_A8;
  s->image.header.w=crop.width;s->image.header.h=crop.height;s->image.header.stride=crop.width;
  s->image.data=pixels;s->image.data_size=bytes;s->attributes=signature;s->image_revision=revision;s->image_antialias=sampling;
  lv_image_set_src(s->image_object,&s->image);lv_obj_invalidate(s->image_object);++impl_->image_rebuilds;return true;
}
bool Scene::reuse_image(uint32_t id,Rect r,uint64_t revision,uint32_t parent){
  if(!revision||r.width<=0||r.height<=0||r.width>impl_->canvas.width||r.height>impl_->canvas.height)return false;
  auto *s=impl_->get(id,Impl::Kind::Image,parent);if(!s)return false;
  if(s->image_revision!=revision||s->image_antialias!=impl_->antialias||s->image_width!=r.width||s->image_height!=r.height)return false;
  impl_->position(s->object,r);++impl_->image_cache_hits;return true;
}
bool Scene::button(uint32_t id,Rect r,const char *v,unsigned size,uint16_t fill,uint16_t ink,bool selected,bool enabled,uint32_t parent,Align align){
  size=metrics::scaled_text_size(size,impl_->text_preset);
  auto *s=impl_->get(id,Impl::Kind::Button,parent);if(!s)return false;
  s->selected=selected;s->button_fill=fill;
  const bool focused=selected||impl_->native_focused(s->object);
  const auto signature=impl_->signature({uint64_t(r.x),uint64_t(r.y),uint64_t(r.width),uint64_t(r.height),size,fill,ink,selected,enabled,unsigned(align),focused});
  if(s->attributes==signature&&!std::strcmp(lv_label_get_text(s->text),v?v:""))return true;
  lv_obj_set_clickable(s->object,true);
  impl_->position(s->object,r);
  lv_obj_set_style_bg_color(s->object,color(fill),0);
  lv_obj_set_style_bg_color(s->object,lv_color_mix(color(theme::appaccent),color(fill),20),LV_STATE_FOCUSED);
  lv_obj_set_style_bg_color(s->object,lv_color_lighten(color(fill),32),LV_STATE_PRESSED);
  if(impl_->motion){
    lv_obj_set_style_transition(s->object,&impl_->feedback,0);
    lv_obj_set_style_transition(s->object,&impl_->feedback,LV_STATE_PRESSED);
    lv_obj_set_style_transition(s->object,&impl_->feedback,LV_STATE_FOCUSED);
  }else{
    lv_obj_remove_local_style_prop(s->object,LV_STYLE_TRANSITION,0);
    lv_obj_remove_local_style_prop(s->object,LV_STYLE_TRANSITION,LV_STATE_PRESSED);
    lv_obj_remove_local_style_prop(s->object,LV_STYLE_TRANSITION,LV_STATE_FOCUSED);
  }
  if(focused&&!impl_->input_pressed)lv_obj_add_state(s->object,LV_STATE_FOCUSED);else lv_obj_remove_state(s->object,LV_STATE_FOCUSED);
  if(enabled)lv_obj_remove_state(s->object,LV_STATE_DISABLED);else lv_obj_add_state(s->object,LV_STATE_DISABLED);
  lv_obj_set_style_opa(s->object,enabled?LV_OPA_COVER:LV_OPA_50,0);
  lv_obj_set_width(s->text,std::max(0,r.width-8));
  if(!impl_->text(*s,v,size,ink,align))return false;
  lv_obj_center(s->text);
  s->attributes=signature;return true;
}
bool Scene::panel(uint32_t id,Rect r,uint16_t fill,uint32_t parent,int radius){
  if(radius<0){impl_->failed=true;return false;}
  auto *s=impl_->get(id,Impl::Kind::Panel,parent);if(!s)return false;
  const auto signature=impl_->signature({uint64_t(r.x),uint64_t(r.y),uint64_t(r.width),uint64_t(r.height),fill,uint64_t(radius)});
  if(s->attributes==signature)return true;
  impl_->position(s->object,r);lv_obj_set_style_bg_color(s->object,color(fill),0);
  lv_obj_set_style_radius(s->object,radius,0);
  lv_obj_set_clickable(s->object,false);s->attributes=signature;return true;
}
bool Scene::progress(uint32_t id,Rect r,unsigned percent,uint16_t ink,uint32_t parent){
  auto *s=impl_->get(id,Impl::Kind::Progress,parent);if(!s)return false;
  const auto signature=impl_->signature({uint64_t(r.x),uint64_t(r.y),uint64_t(r.width),uint64_t(r.height),percent,ink});
  if(s->attributes==signature)return true;
  impl_->position(s->object,r);lv_bar_set_range(s->object,0,100);lv_bar_set_value(s->object,std::min(100U,percent),LV_ANIM_OFF);
  lv_obj_set_style_radius(s->object,4,0);
  lv_obj_set_style_bg_color(s->object,color(ink),LV_PART_INDICATOR);lv_obj_set_style_radius(s->object,4,LV_PART_INDICATOR);s->attributes=signature;
  return true;
}
bool Scene::spinner(uint32_t id,Rect r,uint16_t ink,uint32_t duration){
  if(r.width<=0||r.height<=0||duration<100||duration>60000){impl_->failed=true;return false;}
  auto *s=impl_->get(id,Impl::Kind::Spinner);if(!s)return false;
  const auto signature=impl_->signature({uint64_t(r.x),uint64_t(r.y),uint64_t(r.width),uint64_t(r.height),ink,duration});
  if(s->attributes==signature)return true;
  impl_->position(s->object,r);lv_obj_set_clickable(s->object,false);
  lv_obj_set_style_arc_color(s->object,color(theme::raised),0);
  lv_obj_set_style_arc_color(s->object,color(ink),LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(s->object,metrics::px(3),0);
  lv_obj_set_style_arc_width(s->object,metrics::px(3),LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(s->object,true,LV_PART_INDICATOR);
  lv_spinner_set_anim_params(s->object,duration,200);
  s->attributes=signature;return true;
}
bool Scene::container(uint32_t id,Rect r,int scroll,uint32_t action,bool horizontal,uint32_t parent){
  auto *s=impl_->get(id,Impl::Kind::Container,parent);if(!s)return false;
  const auto signature=impl_->signature({uint64_t(r.x),uint64_t(r.y),uint64_t(r.width),uint64_t(r.height),uint64_t(scroll),action,horizontal});
  if(s->attributes==signature)return true;
  impl_->position(s->object,r);s->action=action;s->horizontal=horizontal;
  lv_obj_set_scrollable(s->object,true);
  lv_obj_set_scroll_dir(s->object,horizontal?LV_DIR_HOR:LV_DIR_VER);
  lv_obj_set_scroll_momentum(s->object,false);lv_obj_set_scroll_elastic(s->object,false);
  lv_obj_set_style_pad_all(s->object,0,0);
  // Scroll only after children have updated their content size (render below).
  if(s->scroll!=scroll){s->scroll=scroll;s->scroll_pending=true;}
  s->attributes=signature;return true;
}
bool Scene::paragraph(uint32_t id,Rect r,const char *v,unsigned size,uint16_t ink,uint32_t parent){
  size=metrics::scaled_text_size(size,impl_->text_preset);
  auto *s=impl_->get(id,Impl::Kind::Label,parent);if(!s)return false;
  const auto signature=impl_->signature({uint64_t(r.x),uint64_t(r.y),uint64_t(r.width),size,ink,0x70617261});
  if(s->attributes==signature&&!std::strcmp(lv_label_get_text(s->text),v?v:""))return true;
  impl_->position(s->object,{r.x,r.y,r.width,LV_SIZE_CONTENT});
  if(!impl_->text(*s,v,size,ink,Align::Left,4096))return false;
  lv_label_set_long_mode(s->object,LV_LABEL_LONG_MODE_WRAP);s->attributes=signature;return true;
}
bool Scene::toggle(uint32_t id,Rect r,bool checked,uint16_t ink,uint32_t parent){
  auto *s=impl_->get(id,Impl::Kind::Toggle,parent);if(!s)return false;
  const auto signature=impl_->signature({uint64_t(r.x),uint64_t(r.y),uint64_t(r.width),uint64_t(r.height),checked,ink});
  if(s->attributes==signature&&bool(lv_obj_has_state(s->object,LV_STATE_CHECKED))==checked)return true;
  impl_->position(s->object,r);lv_obj_set_style_radius(s->object,12,0);
  lv_obj_set_style_bg_opa(s->object,LV_OPA_COVER,lv_style_selector_t(LV_PART_INDICATOR)|lv_style_selector_t(LV_STATE_CHECKED));
  lv_obj_set_style_bg_color(s->object,color(ink),lv_style_selector_t(LV_PART_INDICATOR)|lv_style_selector_t(LV_STATE_CHECKED));
  lv_obj_set_style_radius(s->object,12,LV_PART_INDICATOR);
  lv_obj_set_style_anim_duration(s->object,impl_->motion?design::controlFeedbackMs:0,0);
  if(checked)lv_obj_add_state(s->object,LV_STATE_CHECKED);else lv_obj_remove_state(s->object,LV_STATE_CHECKED);
  s->attributes=signature;return true;
}
bool Scene::slider(uint32_t id,Rect r,unsigned value,uint16_t ink,bool enabled){
  auto *s=impl_->get(id,Impl::Kind::Slider);if(!s)return false;
  const auto signature=impl_->signature({uint64_t(r.x),uint64_t(r.y),uint64_t(r.width),uint64_t(r.height),value,ink,enabled});
  if(s->attributes==signature&&((impl_->input_pressed&&impl_->active_slider==id)||lv_slider_get_value(s->object)==int(std::min(100u,value))))return true;
  impl_->position(s->object,r);lv_slider_set_range(s->object,0,100);
  if(!impl_->input_pressed||impl_->active_slider!=id)lv_slider_set_value(s->object,std::min(100u,value),LV_ANIM_OFF);
  lv_obj_set_style_bg_color(s->object,color(ink),LV_PART_INDICATOR);
  lv_obj_set_style_radius(s->object,8,LV_PART_KNOB);
  if(enabled)lv_obj_remove_state(s->object,LV_STATE_DISABLED);else lv_obj_add_state(s->object,LV_STATE_DISABLED);
  s->attributes=signature;return true;
}
bool Scene::textbox(uint32_t id,Rect r,const char *value,unsigned size,bool password){
  size=metrics::scaled_text_size(size,impl_->text_preset);
  auto *s=impl_->get(id,Impl::Kind::Textbox);if(!s)return false;
  if(!value||std::strlen(value)>64){impl_->failed=true;return false;}
  const auto signature=impl_->signature({uint64_t(r.x),uint64_t(r.y),uint64_t(r.width),uint64_t(r.height),size,password});
  if(s->attributes==signature&&!std::strcmp(lv_textarea_get_text(s->object),value))return true;
  impl_->position(s->object,r);lv_textarea_set_one_line(s->object,true);
  lv_textarea_set_max_length(s->object,64);lv_textarea_set_password_mode(s->object,password);
  lv_textarea_set_password_show_time(s->object,0);lv_obj_set_clickable(s->object,false);
  if(std::strcmp(lv_textarea_get_text(s->object),value))lv_textarea_set_text(s->object,value);
  lv_obj_set_style_bg_color(s->object,color(theme::surface),0);lv_obj_set_style_text_font(s->object,impl_->font(size),0);
  lv_obj_set_style_text_color(s->object,color(theme::text),0);lv_obj_set_style_opa(s->object,LV_OPA_TRANSP,LV_PART_CURSOR);
  s->attributes=signature;return true;
}
bool Scene::keypad(uint32_t id,Rect r,const char *const *labels,size_t count,unsigned columns,unsigned selected,int disabled){
  if(!labels||!count||count>16||columns<2||columns>4||disabled>=int(count)){impl_->failed=true;return false;}
  for(const auto &slot:impl_->slots)if(slot.object&&slot.kind==Impl::Kind::Keypad&&slot.id!=id){impl_->failed=true;return false;}
  auto *s=impl_->get(id,Impl::Kind::Keypad);if(!s)return false;
  const auto signature=impl_->signature({uint64_t(r.x),uint64_t(r.y),uint64_t(r.width),uint64_t(r.height),count,columns,selected,uint64_t(disabled),impl_->text_preset});
  bool identical=s->attributes==signature;
  for(size_t i=0;i<count;++i){if(!labels[i]||std::strlen(labels[i])>=12){impl_->failed=true;return false;}identical&=!std::strcmp(impl_->key_labels[i].data(),labels[i]);}
  if(identical&&lv_buttonmatrix_get_selected_button(s->object)==std::min<unsigned>(selected,count-1))return true;
  impl_->position(s->object,r);size_t at=0;
  for(size_t i=0;i<count;++i){
    if(!labels[i]||std::strlen(labels[i])>=12){impl_->failed=true;return false;}
    std::strcpy(impl_->key_labels[i].data(),labels[i]);impl_->key_map[at++]=impl_->key_labels[i].data();
    if((i+1)%columns==0&&i+1<count)impl_->key_map[at++]="\n";
  }
  impl_->key_map[at]="";lv_buttonmatrix_set_map(s->object,impl_->key_map.data());
  lv_buttonmatrix_set_selected_button(s->object,std::min<unsigned>(selected,count-1));
  lv_buttonmatrix_set_button_ctrl_all(s->object,LV_BUTTONMATRIX_CTRL_CLICK_TRIG);
  lv_buttonmatrix_clear_button_ctrl_all(s->object,LV_BUTTONMATRIX_CTRL_DISABLED);
  if(disabled>=0)lv_buttonmatrix_set_button_ctrl(s->object,disabled,LV_BUTTONMATRIX_CTRL_DISABLED);
  lv_obj_set_style_pad_all(s->object,3,0);lv_obj_set_style_pad_row(s->object,3,0);lv_obj_set_style_pad_column(s->object,3,0);
  lv_obj_set_style_bg_color(s->object,color(theme::surface),LV_PART_ITEMS);
  lv_obj_set_style_bg_color(s->object,color(0x3186),lv_style_selector_t(LV_PART_ITEMS)|lv_style_selector_t(LV_STATE_PRESSED));
  lv_obj_set_style_text_font(s->object,impl_->font(metrics::scaled_text_size(desktop::typography::label.size,impl_->text_preset)),LV_PART_ITEMS);
  s->attributes=signature;return true;
}
bool Scene::render(RasterOutput output){
  auto *pixels=output.pixels;const auto count=output.count;
  if(!impl_->display||impl_->failed||!pixels||count!=impl_->canvas.pixels()||reinterpret_cast<uintptr_t>(pixels)%LV_DRAW_BUF_ALIGN){impl_->rendered=false;return false;}
  const auto started=Impl::now_us();Impl::timing(impl_->build_time_us,impl_->build_max_us,started-impl_->build_started);
  if(impl_->focus_group&&impl_->focus_version!=impl_->version)impl_->retire_focus();
  impl_->retire_stale(false);
  for(auto &s:impl_->slots)if(s.object&&s.kind==Impl::Kind::Container&&s.scroll_pending){
    lv_obj_update_layout(s.object);
    if(s.horizontal)lv_obj_scroll_to_x(s.object,std::max(0,s.scroll),LV_ANIM_OFF);
    else lv_obj_scroll_to_y(s.object,std::max(0,s.scroll),LV_ANIM_OFF);
    s.scroll_pending=false;
  }
  if(!impl_->retained){
    lv_display_set_buffers_with_stride(impl_->display,pixels,nullptr,uint32_t(count*2),uint32_t(impl_->canvas.stride*2),LV_DISPLAY_RENDER_MODE_FULL);
    lv_obj_invalidate(lv_display_get_screen_active(impl_->display));
  }
  impl_->raster_damage=display::FrameDamageMap(impl_->canvas.width,impl_->canvas.height);
  lv_refr_now(impl_->display);
  if(impl_->retained){
    const auto epoch=++impl_->raster_epoch;
    impl_->raster_history[epoch%impl_->raster_history.size()]=impl_->raster_damage;
    auto *coherence=output.coherence;
    const bool coherent=output.initialized&&coherence&&coherence->owner==impl_->raster_owner&&coherence->epoch<epoch&&
      epoch-coherence->epoch<=impl_->raster_history.size()&&coherence->overwritten.valid(impl_->canvas.width,impl_->canvas.height);
    auto candidates=coherent?coherence->overwritten:display::FrameDamageMap{};
    if(coherent)for(auto n=coherence->epoch+1;n<=epoch;++n)candidates.merge(impl_->raster_history[n%impl_->raster_history.size()]);
    if(output.damage){
      if(coherent)*output.damage=candidates;
      else {*output.damage=display::FrameDamageMap(impl_->canvas.width,impl_->canvas.height);output.damage->mark({0,0,impl_->canvas.width,impl_->canvas.height});}
      // Complete LVGL invalidation coverage can proceed to transmission;
      // later pixel overlays retain their own exact refinement.
      output.damage->use_declared_region();
    }
    const auto copied=coherent?detail::copy_raster_output(impl_->retained,pixels,impl_->canvas.width,impl_->canvas.height,impl_->canvas.stride,candidates):
      detail::copy_raster_output(impl_->retained,pixels,impl_->canvas.height,impl_->canvas.stride,output.initialized);
    if(!copied.valid){impl_->rendered=false;impl_->failed=true;return false;}
    if(coherence){coherence->owner=impl_->raster_owner;coherence->epoch=epoch;coherence->overwritten=display::FrameDamageMap(impl_->canvas.width,impl_->canvas.height);}
    impl_->output_copy_bytes.fetch_add(copied.copied);
    impl_->output_compared_bytes.fetch_add(copied.compared);
  }
  else if(output.damage){*output.damage=display::FrameDamageMap(impl_->canvas.width,impl_->canvas.height);output.damage->mark({0,0,impl_->canvas.width,impl_->canvas.height});output.damage->use_declared_region();}
  Impl::timing(impl_->raster_time_us,impl_->raster_max_us,Impl::now_us()-started);
  lv_timer_pause(lv_display_get_refr_timer(impl_->display));
  impl_->building=false;impl_->rendered=true;impl_->invalidated=false;++impl_->frames;return true;
}
RasterStats Scene::stats() const {return {impl_->retained_bytes.load(),impl_->frames.load(),impl_->image_rebuilds.load(),impl_->image_cache_hits.load(),impl_->flushes.load(),impl_->raster_pixels.load(),impl_->build_time_us.load(),impl_->raster_time_us.load(),impl_->build_max_us.load(),impl_->raster_max_us.load(),impl_->output_copy_bytes.load(),impl_->output_compared_bytes.load(),impl_->texture_bytes.load(),impl_->texture_peak_bytes.load(),impl_->object_creates.load(),impl_->object_reuses.load(),impl_->cached_objects.load()};}
bool Scene::tap(uint64_t epoch,int x,int y){
  return pointer(epoch,true,x,y)&&pointer(epoch,false,x,y);
}
bool Scene::pointer(uint64_t epoch,bool pressed,int x,int y,bool valid){
  if(!impl_->rendered||impl_->failed||epoch!=impl_->epoch)return false;
  if(!valid||x<0||x>=impl_->canvas.width||y<0||y>=impl_->canvas.height){
    const bool building=impl_->building;impl_->building=true;
    impl_->input_pressed=false;impl_->active_slider=0;
    lv_indev_reset(impl_->pointer,nullptr);lv_indev_read(impl_->pointer);
    impl_->synchronize_button_focus();
    impl_->building=building;return false;
  }
  const bool contact_changed=impl_->input_pressed!=pressed;
  impl_->x=x;impl_->y=y;impl_->input_pressed=pressed;
  lv_indev_read(impl_->pointer);
  if(contact_changed)impl_->synchronize_button_focus();
  if(!pressed)impl_->active_slider=0;
  lv_timer_pause(lv_display_get_refr_timer(impl_->display));
  return true;
}
Memory Scene::memory(){
  if(!initialized)return {};
  lv_mem_monitor_t m{};lv_mem_monitor(&m);return {LV_MEM_SIZE,m.total_size-m.free_size,m.max_used,m.free_biggest_size};
}
Update Scene::service(){
  if(!impl_->display||!impl_->rendered||impl_->failed)return {};
  // Timers may update styles/animations, but physical refresh must wait for an
  // OS-owned frame buffer and its existing presentation path.
  lv_timer_pause(lv_display_get_refr_timer(impl_->display));
  const uint32_t delay=lv_timer_handler();
  lv_timer_pause(lv_display_get_refr_timer(impl_->display));
  return {impl_->invalidated,delay};
}
} // namespace lunaredu::lunaros::graphics
