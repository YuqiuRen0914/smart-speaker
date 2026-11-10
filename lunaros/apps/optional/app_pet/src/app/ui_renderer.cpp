#include "diagnostics/function_probe.h"
#include "app/ui_renderer.h"
#include "app/caption.h"
#include "lunaros_text_raster.h"
#include "graphics/text_paint.h"
#include <algorithm>
#include <cstring>
#include <vector>
#include <cstdlib>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif
namespace lunaredu::pet {
namespace {
constexpr uint32_t text=0xe7e9ec,muted=0x939ba4,surface=0x15191e,accent=0xe6b35a;
uint16_t wire(uint32_t rgb){const uint16_t v=((rgb>>8)&0xf800)|((rgb>>5)&0x7e0)|((rgb>>3)&0x1f);return (v<<8)|(v>>8);}
void rect(PetFrameBuffer f,FrameRegion r,uint32_t color){
  if(r.width<=0||r.height<=0)return;
  const auto swapped=wire(color);const auto value=f.native_rgb565?uint16_t((swapped<<8)|(swapped>>8)):swapped;
  for(int y=std::max(0,r.y);y<std::min(f.height,r.y+r.height);++y)
    std::fill(f.pixels+y*f.stride+std::max(0,r.x),f.pixels+y*f.stride+std::min(f.width,r.x+r.width),value);
}
struct Ink{PetFrameBuffer f;FrameRegion clip;uint16_t color;};
void span(void *context,int x,int y,const uint8_t *alpha,size_t count){
  const auto &i=*static_cast<Ink *>(context);
  if(y<0||y>=i.f.height||y<i.clip.y||int64_t(y)>=int64_t(i.clip.y)+i.clip.height)return;
  const int64_t first=std::max({int64_t(0),int64_t(x),int64_t(i.clip.x)});
  const int64_t end=std::min({int64_t(i.f.width),int64_t(x)+int64_t(count),int64_t(i.clip.x)+i.clip.width});
  if(first>=end)return;
  alpha+=size_t(first-x);auto *pixels=i.f.pixels+y*i.f.stride+first;
  // Blend once in native 5/6/5 channels instead of expanding each glyph pixel
  // to RGB888 and quantizing it again. Preserve wire order at the boundary.
  for(int64_t n=0;n<end-first;++n){
    const uint16_t native=i.f.native_rgb565?pixels[n]:uint16_t((pixels[n]<<8)|(pixels[n]>>8));
    const auto blended=lunaros::text_raster::paint_rgb565(native,i.color,alpha[n],i.f.antialias!=0);
    pixels[n]=i.f.native_rgb565?blended:uint16_t((blended<<8)|(blended>>8));
  }
}
std::vector<std::string> lines(const std::string &value,int width,unsigned font){
  std::vector<std::string> result;std::string line;
  for(size_t i=0;i<value.size();){
    const auto c=static_cast<unsigned char>(value[i]);size_t count=c<128?1:c<224?2:c<240?3:4;count=std::min(count,value.size()-i);
    const auto part=value.substr(i,count);i+=count;
    if(part=="\n"){result.push_back(line);line.clear();continue;}
    if(!line.empty() && lunaros::text_raster::width((line+part).c_str(),font)>width){result.push_back(line);line.clear();}
    line+=part;
  }
  if(!line.empty())result.push_back(line);
  return result;
}
int scale(int n,int extent){return n*extent/240;}
FrameRegion intersection(FrameRegion a,FrameRegion b){
  const int x=std::max(a.x,b.x),y=std::max(a.y,b.y);
  const auto right=std::min(int64_t(a.x)+a.width,int64_t(b.x)+b.width);
  const auto bottom=std::min(int64_t(a.y)+a.height,int64_t(b.y)+b.height);
  return {x,y,int(std::max<int64_t>(0,right-x)),int(std::max<int64_t>(0,bottom-y))};
}
std::string appearance_name(const std::string &id){
  constexpr std::pair<const char *,const char *> names[]={
    {"soft-pet","软萌"},{"blob","团子"},{"pebble","卵石"},{"bean","豆子"},{"egg","蛋形"},{"squircle","圆角方形"},{"tablet","扁圆"},{"capsule","胶囊"},
    {"cylinder","圆柱"},{"hex","六边形"},{"gem","宝石"},{"crystal","水晶"},{"wedge","楔形"},{"shield","盾形"},{"dome","穹顶"},{"arch","拱形"},{"cloud","云朵"},{"teardrop","水滴"},{"leaf","叶片"},
    {"cyan","青色"},{"cream","奶油色"},{"gold","金色"},{"magenta","玫红"},{"black","黑色"},{"brown","棕色"},{"red","红色"},{"orange","橙色"},{"yellow","黄色"},{"green","绿色"},{"blue","蓝色"},{"violet","紫色"},{"gray","灰色"}};
  for(const auto &name:names){if(id==name.first)return name.second;}
  return "默认";
}
}
UiControls ui_controls(const UiSnapshot &v,int width,int height,const UiDecoration &d){
  UiControls result;
  const auto add=[&](UiAction a,int x,int y,int w,int h,const char *title){
    result.items[result.count++]={a,{scale(x,width),scale(y,height),scale(w,width),scale(h,height)},title};};
  if(d.connection_open){add(UiAction::CloseConnection,12,204,100,28,"返回");add(d.connection_next?UiAction::ConnectionNext:UiAction::RetryConnection,128,204,100,28,d.connection_next?(v.network_setup_link?"热点二维码":"已连接热点"):d.connection_busy?"等待连接":"重新连接");return result;}
  if(v.page==Page::Home){
    if(v.mode!=DisplayMode::Fullscreen)add(UiAction::ToggleMode,176,8,52,24,"全屏");
    if(v.connections.media.state==ConnectionState::Ready){
      if(v.connections.media.playing||v.connections.media.paused)add(UiAction::MediaStop,12,8,52,24,"停止");
      if(v.mode==DisplayMode::Fullscreen){
        add(UiAction::MediaToggle,12,204,68,28,v.connections.media.playing&&!v.connections.media.paused?"暂停":"播放");
        add(UiAction::MediaOpen,86,204,78,28,"音乐");
      }else{
        add(UiAction::MediaOpen,12,164,216,32,"");
        add(UiAction::MediaPrevious,12,204,46,28,"上首");
        add(UiAction::MediaToggle,62,204,46,28,v.connections.media.playing&&!v.connections.media.paused?"暂停":"播放");
        add(UiAction::MediaNext,112,204,46,28,"下首");
      }
      if(v.mode!=DisplayMode::Fullscreen)add(UiAction::Settings,174,204,54,28,"设置");
      return result;
    }
    if(v.mode==DisplayMode::Fullscreen)return result;
    if(v.connections.voice.state!=ConnectionState::Absent)add(UiAction::Voice,12,204,66,28,v.connections.voice.action==VoiceAction::StopReply?"停一下":v.connections.voice.action==VoiceAction::FinishInput?"说完了":"说话");
    if(v.connections.activities!=ConnectionState::Absent)add(UiAction::Activities,86,204,66,28,"活动");
    add(UiAction::Settings,160,204,68,28,"设置");return result;
  }
  add(UiAction::Home,8,8,48,28,"返回");
  if(v.page==Page::Activities){
    for(size_t i=0;i<v.connections.count;++i)add(static_cast<UiAction>(static_cast<unsigned>(UiAction::SelectFirst)+i),12,64+int(i)*44,216,38,"");
    if(v.connections.offset)add(UiAction::Previous,12,204,68,28,"上一页");
    if(v.connections.offset+v.connections.count<v.connections.total)add(UiAction::Next,160,204,68,28,"下一页");
  }else if(v.page==Page::Activity){
    result.items[0].action=UiAction::Activities;
    if(v.confirming){add(UiAction::DismissConfirm,12,174,100,28,"继续运行");add(UiAction::ConfirmCancel,128,174,100,28,"确认取消");}
    else if(v.connections.selected.cancellable)add(UiAction::CancelActivity,128,174,100,28,"取消活动");
    if(v.detail_page)add(UiAction::Previous,12,204,68,28,"上一段");
    const auto font=unsigned(std::max(14,scale(14,width)));
    if(lines(v.connections.selected.detail,scale(216,width),font).size()>(v.detail_page+1)*4)add(UiAction::Next,160,204,68,28,"下一段");
  }else if(v.page==Page::Settings){
    add(UiAction::ToggleMode,12,48,216,32,v.mode==DisplayMode::Fullscreen?"本次显示：全屏":"本次显示：窗口");
    add(UiAction::SaveDefaultMode,12,85,216,32,"设为默认显示模式");
    add(UiAction::Appearance,12,122,216,32,"外观与自主变化");
    add(UiAction::Features,12,159,216,32,"功能开关");
  }else if(v.page==Page::Features){
    result.items[0].action=UiAction::Settings;
    const UiAction actions[]={UiAction::ToggleFlexible,UiAction::ToggleImu,UiAction::ToggleVoice,UiAction::ToggleAgent};
    const char *names[]={"柔性动画","IMU 感应","语音助手","Agent"};
    int row=0;for(unsigned i=0;i<4;++i)if(v.feature_support&(1U<<i)){
      add(actions[i],12,48+row++*38,216,32,names[i]);
      result.items[result.count-1].toggle=true;result.items[result.count-1].checked=v.features.enabled(static_cast<Feature>(i));
    }
    add(UiAction::Connections,12,204,216,28,"连接与服务");
  }else if(v.page==Page::Appearance){
    add(UiAction::CycleShape,12,70,216,32,"切换形状");add(UiAction::CycleColor,12,108,216,32,"切换颜色");
    if(d.autonomous_appearance_enabled)add(UiAction::AutonomousAppearance,12,146,216,32,"恢复自主外观");
  }else if(v.page==Page::Connections){
    add(UiAction::PairAccount,12,48,100,32,"账号配对");add(UiAction::ConfigureNetwork,128,48,100,32,"网络配置");
  }
  return result;
}
UiAction ui_hit(const UiControls &controls,int x,int y){
  for(size_t i=0;i<controls.count;++i){const auto &c=controls.items[i];const auto r=c.bounds;if(x>=r.x&&y>=r.y&&x<r.x+r.width&&y<r.y+r.height)return c.action;}
  return UiAction::None;
}
FrameRegion UiRenderer::character_region(const UiSnapshot &v,int w,int h){
  if(v.page!=Page::Home)return {};
  if(v.mode==DisplayMode::Fullscreen)return {0,0,w,h};
  return {0,scale(34,h),w,scale(98,h)};
}
std::array<FrameRegion,2> UiRenderer::fullscreen_overlay_bands(int w,int h){
  const int top=scale(34,h),bottom=scale(166,h);
  return {{{0,0,w,top},{0,bottom,w,h-bottom}}};
}
void UiRenderer::reset(){text_cache_.reset();qr_.clear();std::fill(qr_payload_.begin(),qr_payload_.end(),'\0');qr_payload_.clear();caption_text_.clear();caption_lines_.clear();caption_line_ends_.clear();caption_width_=0;caption_font_=0;}
void UiRenderer::paint(PetFrameBuffer f,const UiSnapshot &v,const UiDecoration &d,const UiControls *prepared_controls,const FrameRegion *clip){
  lunaredu::system::probe::Scope diagnostic_scope(lunaredu::system::probe::Point::UiPaint);
  lunaros::text_raster::TextCoverageCache::Memory memory;
  if(cache_text_)memory={[](size_t bytes)->void *{
#ifdef ESP_PLATFORM
    return heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
    return std::malloc(bytes);
#endif
  },[](void *pixels){
#ifdef ESP_PLATFORM
    heap_caps_free(pixels);
#else
    std::free(pixels);
#endif
  }};
  text_cache_.begin(memory);
  const auto paint_clip=intersection({0,0,f.width,f.height},clip?*clip:FrameRegion{0,0,f.width,f.height});
  const auto fill=[&](FrameRegion r,uint32_t color){rect(f,intersection(r,paint_clip),color);};
  const auto label=[&](PetFrameBuffer buffer,FrameRegion r,const std::string &value,unsigned size,uint32_t color=text,bool centered=false){
    const auto covered=intersection(r,paint_clip);if(covered.width<=0||covered.height<=0)return;
    const auto swapped=wire(color);Ink ink{buffer,covered,uint16_t((swapped<<8)|(swapped>>8))};
    int x=r.x,y=r.y;
    text_cache_.draw_clipped_spans(x,y,r.width,value,size,span,&ink,centered?r.height:0);
  };
  const int w=f.width,h=f.height;const unsigned font=unsigned(std::max(14,scale(14,w)));
  const auto at=[&](int x,int y,int width,int height){return FrameRegion{scale(x,w),scale(y,h),scale(width,w),scale(height,h)};};
  const auto write=[&](int x,int y,int width,const std::string &s,uint32_t c=text){label(f,at(x,y,width,24),s,font,c);};
  if(d.connection_open){
    fill({0,0,w,h},0);write(12,12,216,d.connection_title);write(12,42,216,d.connection_hint,muted);
  }else if(v.page==Page::Home){
    const bool media=v.connections.media.state==ConnectionState::Ready;
    if(!media&&v.mode==DisplayMode::Window)write(12,10,158,v.connections.device_status.empty()?"宠物":v.connections.device_status,muted);
    if(v.mode==DisplayMode::Window){
      const auto &caption=home_caption(v);
      write(12,136,216,caption.empty()?(media?"和你一起听音乐":"在这里陪着你"):caption);
      if(!media){
      fill(at(12,164,216,32),surface);
      write(20,168,200,v.connections.count?v.connections.items[0].title+" · "+v.connections.items[0].status:
        v.connections.activities==ConnectionState::Ready?"暂时没有活动":v.connections.activity_status.empty()?"轻触、抚摸，和宠物互动":v.connections.activity_status,muted);
      }
    }else{
      const auto &caption=home_caption(v);const int width=scale(216,w);
      if(caption!=caption_text_||caption_width_!=width||caption_font_!=font){
        caption_text_=caption;caption_width_=width;caption_font_=font;caption_lines_=lines(caption,width,font);
        caption_line_ends_.clear();
        size_t byte_end = 0, characters = 0;
        for (const auto &line : caption_lines_) {
          for (unsigned char c : line) if ((c & 0xc0) != 0x80) ++characters;
          byte_end += line.size();
          if (byte_end < caption.size() && caption[byte_end] == '\n') {
            ++byte_end; ++characters;
          }
          caption_line_ends_.push_back(characters);
        }
      }
      if(!caption_lines_.empty()){
        // Keep the current line and its successor visible. The final two
        // lines stay put; a long caption never cycles back to old speech.
        size_t first = static_cast<size_t>(std::min<uint64_t>(v.caption_page, caption_lines_.size()));
        if (v.connections.voice.caption_audio_clock && !v.connections.voice.caption.empty()) {
          first = static_cast<size_t>(std::upper_bound(caption_line_ends_.begin(),
              caption_line_ends_.end(), v.connections.voice.caption_character) - caption_line_ends_.begin());
        }
        first = std::min(first, caption_lines_.size() > 2 ? caption_lines_.size() - 2 : size_t(0));
        const size_t count=std::min(size_t(2),caption_lines_.size()-first);
        const int top=(media?200:228)-int(count)*17;
        for(size_t row=first;row<first+count;++row){
          const int y=top+int(row-first)*17;
          label(f,at(13,y+1,216,17),caption_lines_[row],font,0,true);
          label(f,at(12,y,216,17),caption_lines_[row],font,text,true);
        }
      }
    }
  }else{
    const char *title=v.page==Page::Activities?"活动":v.page==Page::Activity?"活动详情":v.page==Page::Settings?"宠物设置":v.page==Page::Appearance?"外观":v.page==Page::Features?"功能开关":"连接与服务";
    write(68,10,160,title);
    if(v.page==Page::Activities){
      write(12,40,216,v.message.empty()?v.connections.activity_status:v.message,muted);
      if(!v.connections.count)write(12,88,216,v.connections.activities==ConnectionState::Absent?"未接入活动服务":v.connections.activities==ConnectionState::Ready?"暂时没有活动":"服务暂不可用，宠物继续陪伴",muted);
    }else if(v.page==Page::Activity){
      write(12,46,216,v.connections.selected.title);write(12,72,216,!v.message.empty()?v.message:v.confirming?"取消后，请等待最终结果":v.connections.selected.status,accent);
      const auto detail=lines(v.connections.selected.detail,scale(216,w),font);
      for(size_t i=v.detail_page*4;i<std::min(detail.size(),(v.detail_page+1)*4);++i)write(12,100+int(i-v.detail_page*4)*18,216,detail[i],muted);
    }else if(v.page==Page::Appearance){write(12,42,216,appearance_name(d.shape)+" · "+appearance_name(d.color),muted);
      if(!d.autonomous_appearance_enabled)write(12,154,216,"当前设备使用固定外观",muted);}
    else if(v.page==Page::Connections){write(12,86,216,"语音与活动的完整设置在工作台",muted);}
  }
  const auto payload=d.connection_open?d.connection_url:v.page==Page::Connections?v.connections.management_url:std::string();
  if(payload!=qr_payload_){qr_.clear();std::fill(qr_payload_.begin(),qr_payload_.end(),'\0');qr_payload_=payload;qr_.generate(payload);}
  if(qr_.ready()){
    const int size=scale(d.connection_open?124:88,std::min(w,h));const int x=(w-size)/2,y=scale(d.connection_open?72:112,h);
    fill({x,y,size,size},0xffffff);
    struct QrTarget {PetFrameBuffer frame;FrameRegion clip;} target{f,paint_clip};
    qr_.paint(x,y,size,&target,[](void *p,int x0,int y0,int rw,int rh,bool dark){auto &target=*static_cast<QrTarget *>(p);rect(target.frame,intersection({x0,y0,rw,rh},target.clip),dark?0:0xffffff);});
  }else if(!payload.empty())write(12,124,216,"二维码暂不可用，请重试",muted);
  const auto controls=prepared_controls?*prepared_controls:ui_controls(v,w,h,d);
  for(size_t i=0;i<controls.count;++i){const auto &c=controls.items[i];fill(c.bounds,surface);
    auto box=c.bounds;box.x+=scale(8,w);box.y+=scale(4,h);box.width-=scale(16,w);
    if(c.action>=UiAction::SelectFirst&&c.action<=UiAction::SelectThird){
      const auto index=static_cast<unsigned>(c.action)-static_cast<unsigned>(UiAction::SelectFirst);
      label(f,box,v.connections.items[index].title,font);
      box.y+=scale(18,h);label(f,box,v.connections.items[index].status,unsigned(std::max(12,scale(12,w))),muted);
    }else if(c.action==UiAction::MediaOpen&&v.mode==DisplayMode::Window){
      label(f,box,v.connections.media.title.empty()?"打开音乐曲库":v.connections.media.title,font);
    }else{
      const bool disabled=(c.action==UiAction::Voice && v.connections.voice.action==VoiceAction::None)||(c.action==UiAction::RetryConnection&&d.connection_busy);
      if(c.toggle){
        auto caption=box;caption.width-=scale(52,w);label(f,caption,c.label,font);
        auto status=box;status.x=box.x+box.width-scale(28,w);status.width=scale(28,w);
        label(f,status,c.checked?"开":"关",font,c.checked?accent:muted);
      }else label(f,box,c.label,font,disabled?muted:c.action==UiAction::ConfirmCancel?accent:text);
    }
  }
  if(!d.connection_open && !v.message.empty() && v.page!=Page::Home && v.page!=Page::Activity && v.page!=Page::Activities && v.page!=Page::Features)write(12,206,216,v.message,accent);
}
} // namespace lunaredu::pet
