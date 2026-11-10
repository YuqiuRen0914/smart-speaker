#include "lunaros_ui_metrics.h"
#include "lunaros_files_browser.h"
#include "desktop/shell/shell.h"
#include "desktop/graphics/page.h"
#include "desktop/ui/content_layout.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
namespace lunaredu::lunaros::graphics {
namespace ui=::lunaredu::lunaros::graphics::metrics;
namespace {
using namespace desktop;
constexpr uint16_t white=theme::text,muted=theme::appsecondary,panel=theme::appsurface,accent=theme::appaccent;
constexpr uint32_t file_id=4000,list_id=5000,scroll_id=5001,info_id=5003,seek_id=5004,volume_id=5005,play_id=5006,retry_id=5007,wallpaper_id=5008;
void label(Scene &scene,uint32_t id,int y,const char *text,unsigned size=desktop::typography::label.size,uint16_t ink=white){scene.label(id,{ui::px(8),y,ui::px(224),ui::px(28)},text,size,ink);}
const char *kind(FileKind k){switch(k){case FileKind::Directory:return "目录";case FileKind::Text:return "文本";case FileKind::Jpeg:return "图片";case FileKind::Audio:return "音频";case FileKind::Video:return "视频";default:return "二进制";}}
std::string bytes(size_t n){char text[32];if(n>=1048576)std::snprintf(text,sizeof(text),"%.1f MB",n/1048576.);else if(n>=1024)std::snprintf(text,sizeof(text),"%.1f KB",n/1024.);else std::snprintf(text,sizeof(text),"%u B",unsigned(n));return text;}
std::string name(const std::string &p){return p=="/scripts"?"脚本":p=="/performances"?"表演":file_name(p);}
void files(Scene &scene,const DesktopShell &shell){
  const ContentLayout layout(shell.preferences.auto_hide_status);
  if(!file_browser(shell)){page::content_state(scene,layout,page::State::Loading,"正在读取存储状态");return;}
  const auto &s=*file_browser(shell);const auto &location=s.location;
  const bool root=location.view==FileView::Directory&&location.path=="/";
  if(!s.content||s.loading){page::content_state(scene,layout,page::State::Loading,"正在读取文件");return;}
  const auto &content=*s.content;
  if(!content.available||(!content.message.empty()&&location.view!=FileView::Information)){
    page::content_state(scene,layout,content.available?page::State::Error:page::State::Offline,
                content.message.empty()?"存储已离线":content.message.c_str(),{retry_id,"重试"});return;
  }
  if(!root&&location.view!=FileView::Directory&&location.view!=FileView::Sort){scene.label(40,{ui::px(8),ui::px(32),ui::px(184),ui::px(24)},name(location.path).c_str(),ui::font(16),white,Align::Left);scene.label(41,{ui::px(8),ui::px(54),ui::px(224),ui::px(16)},location.view==FileView::Information?"文件信息":"只读浏览",ui::font(12),muted,Align::Left);}
  if(location.view==FileView::Preview)scene.button(info_id,{ui::px(202),ui::px(32),ui::px(30),ui::px(30)},"i",ui::font(16),panel,accent);
  if(location.view==FileView::Preview&&content.image){
    const auto &image=*content.image;
    if(image.width&&image.height&&image.pixels.size()==size_t(image.width)*image.height){
      const int height=content.file.kind==FileKind::Video?ui::px(74):ui::px(114);
      // The source snapshot stays immutable during the build; Scene owns the
      // copy and LVGL handles resampling on the existing render owner.
      scene.image(6200,{ui::px(8),ui::px(70),ui::px(224),height},
        {image.width,image.height,image.width,image.pixels.data(),nullptr,0xffff,false,0,ImageFit::Contain,1});
    }
  }
  if(location.view==FileView::Directory||location.view==FileView::Sort){
    scene.container(list_id,{ui::px(8),layout.top,ui::px(224),layout.height()},location.scroll*layout.row_pitch(),scroll_id);
    scene.flex_layout(list_id,false,layout.row_pitch()-layout.row_height());
    for(unsigned i=0;i<file_rows(s);++i){std::string title,detail;
      if(location.view==FileView::Sort){title=i?"文件大小":"名称";detail=s.size_sort==(i==1)?"当前排序":"文件夹优先";}
      else if(i<content.entries.size()){const auto &entry=content.entries[i];title=name(entry.path);detail=entry.kind==FileKind::Directory?"目录":bytes(entry.size)+" · "+kind(entry.kind);}
      else{unsigned action=i-unsigned(content.entries.size());if(location.page&&action--==0)title="上一页";else if(content.more&&action--==0)title="下一页";else{title="排序方式";detail=s.size_sort?"按文件大小":"按名称";}}
      // The filename is bounded by the storage contract; clipping belongs to LVGL.
      if(title.size()>210){size_t end=210;while(end&&(static_cast<unsigned char>(title[end])&0xc0)==0x80)--end;title.resize(end);}
      page::row(scene,file_id+i,{0,0,ui::px(224),layout.row_height()},title.c_str(),detail.c_str(),shell.keys_enabled&&location.focus==i,true,list_id,theme::Tone::Normal,48);
      scene.flow_item(file_id+i,0,i);
      const auto icon=location.view==FileView::Sort?lunaros::icons::Icon::Adjust:i<content.entries.size()?(content.entries[i].kind==FileKind::Directory?lunaros::icons::Icon::Folder:content.entries[i].kind==FileKind::Text?lunaros::icons::Icon::Code:content.entries[i].kind==FileKind::Jpeg?lunaros::icons::Icon::Display:lunaros::icons::Icon::Download):lunaros::icons::Icon::ArrowRight;
      page::icon_badge(scene,6100+i*3,{ui::px(8),(layout.row_height()-ui::px(30))/2,ui::px(30),ui::px(30)},icon,theme::amber,file_id+i,shell.preferences.antialias);
    }
    return;
  }
  if(location.view==FileView::Information){
    const auto &entry=content.file;std::string text="名称\n"+std::string(file_name(entry.path))+"\n类型\n"+kind(entry.kind)+"\n大小\n"+bytes(entry.size)+"\n位置\n"+entry.path;
    if(entry.modified_ms)text+="\n修改时间 (ms)\n"+std::to_string(entry.modified_ms);
    text+="\n访问权限\n只读";
    scene.container(list_id,{ui::px(8),ui::px(72),ui::px(224),ui::px(132)},location.scroll,scroll_id);scene.paragraph(44,{0,0,ui::px(220),0},text.c_str(),ui::font(16),white,list_id);
  }else if(content.file.kind==FileKind::Audio||content.file.kind==FileKind::Video){
    if(!content.image)label(scene,42,ui::px(98),"WAV / PCM16",desktop::typography::label.size,muted);
    scene.slider(seek_id,{ui::px(16),ui::px(153),ui::px(208),ui::px(8)},s.playback.duration>0?unsigned(std::clamp(s.playback.position/s.playback.duration,0.,1.)*100):0,accent,s.capabilities.seek&&s.playback.duration>0);
    const bool playing=s.playback.state==media::State::playing;
    scene.button(play_id,{ui::px(8),ui::px(176),ui::px(64),ui::px(32)},playing?"暂停":"播放",ui::font(16),panel,white,shell.keys_enabled&&location.focus==0,s.capabilities.pause||!playing);
    if(s.capabilities.audio_output&&s.capabilities.volume){scene.label(45,{ui::px(82),ui::px(174),ui::px(48),ui::px(16)},"音量",ui::font(12),muted);scene.slider(volume_id,{ui::px(136),ui::px(187),ui::px(88),ui::px(8)},s.playback.volume,accent);}
    else scene.label(45,{ui::px(86),ui::px(178),ui::px(138),ui::px(24)},"静音",desktop::typography::label.size,muted);
  }else if(!content.image){
    scene.container(list_id,{ui::px(8),ui::px(70),ui::px(224),ui::px(132)},location.scroll,scroll_id);scene.paragraph(44,{0,0,ui::px(220),0},content.text.c_str(),ui::font(16),white,list_id);
  }
  if(location.view==FileView::Preview&&content.file.kind==FileKind::Jpeg&&content.image){
    scene.button(wallpaper_id,{ui::px(8),ui::px(192),ui::px(224),ui::px(40)},s.wallpaper_applied?"已设为壁纸":"设为桌面壁纸",ui::font(16),panel,accent,shell.keys_enabled);
    return;
  }
  scene.label(43,{ui::px(8),ui::px(206),ui::px(224),ui::px(14)},content.text_truncated?"预览前 4KB · 只读":"只读浏览",ui::font(12),muted);
}
}
void build_file_surface(Scene &scene,const desktop::DesktopShell &shell){files(scene,shell);}
bool apply_file_action(desktop::DesktopShell &s,Action a){using namespace desktop;
const ContentLayout layout(s.preferences.auto_hide_status);
if(a.id==scroll_id){const unsigned v=unsigned(std::max<int32_t>(0,a.value));return file_scroll(s,file_browser(s)&&(file_browser(s)->location.view==FileView::Directory||file_browser(s)->location.view==FileView::Sort)?v/layout.row_pitch():v);}
    if(a.id>=file_id&&a.id<file_id+12)return file_control(s,FileControl::Row,a.id-file_id);
    if(a.id==info_id)return file_control(s,FileControl::Information);
    if(a.id==seek_id)return file_control(s,FileControl::Seek,0,a.value/100.);
    if(a.id==volume_id)return file_control(s,FileControl::Volume,0,a.value);
    if(a.id==play_id)return file_control(s,FileControl::Toggle);
    if(a.id==wallpaper_id)return file_control(s,FileControl::Wallpaper);
    if(a.id==retry_id)return file_control(s,FileControl::Retry);
return false;}
}
