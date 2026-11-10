#include "lunaredu_music_desktop.h"
#include "desktop/ui/content_layout.h"
#include <cstring>
namespace lunaredu::music {
void Presentation::paint(desktop::UiPaintPort &port,bool)const {
  if(!model||!port.begin_widgets())return;
  namespace ui=lunaros::graphics::metrics;
  namespace theme=lunaros::graphics::theme;
  using lunaros::graphics::Align;
  auto &scene=port.graphics().scene();const auto &s=*model;
  const desktop::ContentLayout layout(port.shell().preferences.auto_hide_status);
  const int left=ui::px(8),width=ui::px(224),h=ui::min_touch,gap=ui::gap,pitch=h+gap;
  const int half=(width-gap)/2,third=(width-gap*2)/3;
  const size_t count=s.library?s.library->size():0;
  const bool bluetooth=s.speaker.receiving;
  const char *error=s.error[0]?s.error.data():s.speaker.supported?s.speaker.error.data():"";
  const bool extended=!s.playback_mode.empty();
  const int library_row=s.speaker.supported?7:extended?7:5;
  const int maximum=std::max(0,(library_row+int(std::max(size_t(1),count)))*pitch-(layout.bottom-layout.top));
  const int offset=std::min(int(std::min(scroll,unsigned(INT32_MAX))),maximum);
  scene.container(4000,{left,layout.top,width,layout.bottom-layout.top},offset,233);
  scene.flex_layout(4000,false,gap);
  scene.panel(10,{0,0,width,h},theme::appsurface,4000);
  scene.flow_item(10,0,0);scene.flex_layout(10,false,0,gap,Align::Center);
  const char *title=bluetooth?"音源 · 蓝牙音响":count?(*s.library)[std::min(size_t(s.selected),count-1)].title.c_str():"本地音乐";
  scene.label(11,{0,0,width-gap*2,h/2},title,ui::font(14),theme::text,Align::Left,10,true);
  char status[96];
  if(bluetooth)std::snprintf(status,sizeof(status),"%s",s.speaker.connected?(s.speaker.playing?"手机正在播放":"已连接 · 等待播放"):"等待手机连接");
  else if(!s.audio_ready)std::snprintf(status,sizeof(status),"扬声器不可用");
  else if(s.playing)std::snprintf(status,sizeof(status),"%s · %02u:%02u",s.paused?"已暂停":"正在播放",unsigned(s.position_seconds/60),unsigned(s.position_seconds%60));
  else std::snprintf(status,sizeof(status),"本地曲库 · %u 首",unsigned(count));
  scene.label(12,{0,0,width-gap*2,h/2},status,ui::font(12),theme::secondary,Align::Left,10);
  const auto group=[&](uint32_t id,unsigned order){
    scene.flow(id,{0,0,width,h},true,gap,4000);scene.flow_item(id,0,order);
  };
  const auto button=[&](uint32_t id,uint32_t parent,int w,const char *label,bool enabled=true) {
    scene.button(id,{0,0,w,h},label,ui::font(14),theme::raised,theme::text,false,enabled,parent);
    scene.flow_item(id,parent==4000?0:1);
  };
  const bool control=!s.stopping&&(bluetooth?s.speaker.connected&&s.speaker.controllable:count&&s.audio_ready);
  group(4101,1);
  button(223,4101,third,"上一首",control);
  button(218,4101,third,(bluetooth?s.speaker.playing:s.playing&&!s.paused)?"暂停":"播放",control);
  button(219,4101,third,"下一首",control);
  group(4102,2);button(224,4102,half,"减小音量",s.audio_ready);
  char volume[48];std::snprintf(volume,sizeof(volume),"音量 + · %u%%",unsigned(s.volume));
  button(220,4102,half,volume,s.audio_ready);
  group(4103,3);button(227,4103,half,"停止播放",s.playing||s.stopping);
  button(226,4103,half,"刷新曲库",!s.playing&&!s.stopping&&s.storage_ready&&!bluetooth);
  const char *hint=*error?"播放未完成，请重试":!s.storage_ready?"本地存储不可用":!count?"请将歌曲放入 performances/music":"点击曲目播放 · 支持 WAV / MP3";
  if(!std::strcmp(error,"music_stop_bluetooth_first"))hint="先停止蓝牙接收，再播放本地音乐";
  if(!std::strcmp(error,"music_stop_before_refresh"))hint="停止播放后可刷新曲库";
  if(!std::strcmp(error,"bluetooth_disabled"))hint="请在设置中开启蓝牙";
  if(!std::strcmp(error,"audio_wave_format_unsupported"))hint="WAV 需为 16 位单/双声道 PCM";
  if(!std::strcmp(error,"audio_file_truncated")||!std::strcmp(error,"audio_mp3_decode_failed"))hint="歌曲文件无法播放，请更换文件";
  if(!*error&&!s.library_hint.empty())hint=s.library_hint.c_str();
  scene.label(13,{0,0,width,h},hint,ui::font(12),*error?theme::warning:theme::secondary,Align::Center,4000,true);
  scene.flow_item(13,0,4);
  if(extended&&!s.speaker.supported){
    button(228,4000,width,s.playback_mode.c_str(),!s.stopping);scene.flow_item(228,0,5);
    scene.label(15,{0,0,width,h},s.lyric.empty()?"暂无当前歌词":s.lyric.c_str(),ui::font(14),theme::text,Align::Center,4000,true);scene.flow_item(15,0,6);
  }
  if(s.speaker.supported) {
    button(222,4000,width,bluetooth?"停止蓝牙接收":"用作蓝牙音响",s.speaker.enabled||bluetooth);scene.flow_item(222,0,5);
    button(225,4000,width,"蓝牙连接设置");scene.flow_item(225,0,6);
  }
  // Preserve complete scroll extent without retaining every song's widgets.
  const size_t first=std::min(count,size_t(std::max(0,offset/pitch-library_row-1)));
  const size_t visible=size_t((layout.height()+pitch-1)/pitch)+2;
  const size_t end=std::min(count,first+visible);
  unsigned order=unsigned(library_row);
  if(first){scene.label(4198,{0,0,1,int(first)*pitch-gap},"",ui::font(12),theme::secondary,Align::Left,4000);scene.flow_item(4198,0,order++);}
  for(size_t i=first;i<end;++i){
    const auto id=5000+uint32_t(i);button(id,4000,width,(*s.library)[i].title.c_str(),!bluetooth&&s.audio_ready);scene.flow_item(id,0,order++);
  }
  if(end<count){scene.label(4199,{0,0,1,int(count-end)*pitch-gap},"",ui::font(12),theme::secondary,Align::Left,4000);scene.flow_item(4199,0,order++);}
  if(!count){scene.label(14,{0,0,width,h},"暂无本地曲目",ui::font(14),theme::secondary,Align::Center,4000);scene.flow_item(14,0,order);}
  if(port.finish_widgets("音乐"))port.present_widgets();
}
}
