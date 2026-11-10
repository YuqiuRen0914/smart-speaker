#include "lunaredu_music_desktop.h"
#include "lunaredu_camera_desktop_entry.h"
#include "desktop/ui/status_bar.h"
#include "lunaros_text_raster.h"
#include "lunaros_settings_renderer.h"
#include "lunaros_files_browser.h"
#include "desktop/graphics/desktop.h"
#include <array>
#include <vector>
#include <fstream>
#include <iterator>
#include <cassert>
#include <cstring>
using namespace lunaredu;
inline constexpr int side=desktop::kDesktopCanvas.width;
// The fixture links the actual Files renderer, as its extension does in firmware.
namespace lunaredu::lunaros::graphics {void build_file_surface(Scene &,const desktop::DesktopShell &);}
namespace lunaredu::desktop {
const DesktopSurfaceExtension &desktop_surface_extension(const DesktopShell &,DesktopSurface) {
  static const DesktopSurfaceExtension empty{};return empty;
}
}
desktop::DesktopPainter painter(){
  return {nullptr,[](void *,uint16_t *pixels,int x,int y,const char *text,int size,uint16_t color,bool aa){
    struct Ink{uint16_t *pixels;uint16_t color;bool aa;} ink{pixels,color,aa};
    lunaros::text_raster::draw(x,y,text,size,[](void *p,int xx,int yy,uint8_t alpha){
      if(xx<0||xx>=side||yy<0||yy>=side)return;
      auto &i=*static_cast<Ink *>(p);
      lunaros::graphics::canvas::Surface canvas{i.pixels,side,side,side,i.aa};canvas.pixel(xx,yy,i.color,alpha);
    },&ink);
  },[](void *,const char *text,int size){return lunaros::text_raster::width(text,size);},
  [](void *,const char *text,int size){const auto b=lunaros::text_raster::bounds(text,size);return desktop::DesktopPainter::TextBounds{b.left,b.top,b.width,b.height};}};
}
struct Port:desktop::UiPaintPort {
  Port(){assert(runtime.scene().configure(desktop::kDesktopCanvas));}
  lunaros::graphics::SceneRuntime runtime;desktop::DesktopShell desktop;
  alignas(4)std::array<uint16_t,side*side> pixels{};lunaros::graphics::Page page{};uint64_t generation=0;bool rendered=false;
  bool begin_canvas()override{return true;}
  bool begin_widgets()override{rendered=false;assert(runtime.scene().text_scale(lunaros::graphics::metrics::text_scale_percent(desktop.preferences.text_size)));page=runtime.select(++generation,true);return runtime.begin(page,lunaros::graphics::theme::appbackground,true);}
  lunaros::graphics::SceneRuntime &graphics()override{return runtime;}
  lunaros::graphics::canvas::Surface surface()override{return {pixels.data(),side,side,side,true};}
  bool finish_widgets(const char *title,bool)override{rendered=runtime.render({pixels.data(),pixels.size()});if(rendered)header(title);return rendered;}
  void rotate(unsigned)override{}
  void present_widgets()override{std::string error;assert(runtime.present(page,{nullptr,[](void *,std::string &){return true;},[](void *,std::string &){return true;}},error));}
  void flush()override{}
  bool begin_region(uint16_t,uint16_t,uint16_t,uint16_t,bool)override{return true;}
  bool flush_region(uint16_t,uint16_t)override{return true;}
  void header(const char *title)override{if(!desktop.preferences.auto_hide_status)assert(desktop::paint_status_overlay(runtime.scene(),desktop::kDesktopCanvas,pixels.data(),title,{true,false,false,true,70},{},false,true));}
  void overlay(const char *title)override{header(title);}
  unsigned rotation()const override{return 0;}
  const desktop::DesktopShell &shell()const override{return desktop;}
  void save(const std::string &path){assert(rendered);std::ofstream out(path+".ppm",std::ios::binary);assert(out);out<<"P6\n"<<side<<" "<<side<<"\n255\n";for(auto p:pixels){char rgb[]={char(((p>>11)&31)*255/31),char(((p>>5)&63)*255/63),char((p&31)*255/31)};out.write(rgb,3);}}
};
int main(int argc,char **argv){
  assert(argc==3);std::ifstream file(argv[1],std::ios::binary);std::vector<uint8_t> font{std::istreambuf_iterator<char>(file),{}};
  assert(lunaros::text_raster::initialize(font.data(),font.size()));
  Port port;
  const desktop::DesktopApplication apps[]={
    {"os.settings","设置",desktop::DesktopIcon::Settings,0x8410},{"os.files","文件",desktop::DesktopIcon::Files,0x267f},
    {"pet","小星",desktop::DesktopIcon::Motion,0xfb40},{"camera","相机",desktop::DesktopIcon::Sensor,0x05ca},
    {"python","编程",desktop::DesktopIcon::Code,0xffe0},music::DesktopEntry::tile()};
  assert(port.desktop.configure(apps,std::size(apps)));port.desktop.grid_columns=port.desktop.grid_rows=3;
  port.desktop.set_preferences({desktop::DesktopLayout::Grid,true,0});
  if(!desktop::draw_launcher(port.desktop,{true,false,false,true,70},port.pixels.data(),painter()))return 1;port.rendered=true;port.save(std::string(argv[2])+"desktop-grid");
  for(bool hide:{false,true}){
    auto pref=port.desktop.preferences;pref.auto_hide_status=hide;port.desktop.set_preferences(pref);
    const std::string suffix=hide?"-full":"-status";
    if constexpr(lunaros::graphics::metrics::native){
      port.desktop.surface=desktop::DesktopSurface::Settings;
      assert(port.begin_widgets());assert(settings::build_preferences_surface(port.runtime.scene(),port.desktop));
      assert(port.finish_widgets("设置",true));port.save(std::string(argv[2])+"settings"+suffix);
      for(uint8_t size:{uint8_t(0),uint8_t(1),uint8_t(2)}){
        pref=port.desktop.preferences;pref.text_size=size;port.desktop.set_preferences(pref);port.desktop.list_offset=4;port.desktop.row=7;
        assert(port.begin_widgets());assert(settings::build_preferences_surface(port.runtime.scene(),port.desktop));
        assert(port.finish_widgets("设置",true));port.present_widgets();port.save(std::string(argv[2])+"settings-text-"+std::to_string(size)+suffix);
      }
      pref=port.desktop.preferences;pref.text_size=1;port.desktop.set_preferences(pref);port.desktop.list_offset=0;
      port.desktop.surface=desktop::DesktopSurface::Startup;
      assert(port.begin_widgets());assert(settings::build_preferences_surface(port.runtime.scene(),port.desktop));
      assert(port.finish_widgets("启动应用",true));port.save(std::string(argv[2])+"startup"+suffix);
      auto files=std::make_shared<desktop::FileBrowserState>();auto content=std::make_shared<desktop::FileContent>();
      content->available=true;
      for(unsigned i=0;i<12;++i){desktop::FileEntry entry;entry.path="/scripts/程序-"+std::to_string(i+1)+".py";entry.kind=desktop::FileKind::Text;entry.size=512+i*1024;content->entries.push_back(std::move(entry));}
      files->content=content;desktop::publish_file_browser(port.desktop,files);port.desktop.surface=desktop::DesktopSurface::Files;
      assert(port.begin_widgets());lunaros::graphics::build_file_surface(port.runtime.scene(),port.desktop);
      assert(port.finish_widgets("文件",true));port.save(std::string(argv[2])+"files"+suffix);
      port.desktop.surface=desktop::DesktopSurface::Launcher;
    }
    camera::CameraPresentation camera_home;camera_home.ready=true;camera_home.vision_available=true;
    camera_home.paint(port,true);port.save(std::string(argv[2])+"camera-home"+suffix);
    camera_home.vision_available=false;camera_home.paint(port,true);port.save(std::string(argv[2])+"camera-vision-unavailable"+suffix);
    camera::LabPresentation lab;lab.model.vision_available=lab.model.analysis_ready=lab.model.identity_ready=true;lab.model.observation={};lab.model.observation.red=85;lab.model.observation.green=177;lab.model.observation.blue=218;
    lab.model.observation.inference_us=42150;lab.model.observation.completed_jobs=12;lab.model.observation.face_count=1;lab.model.observation.hand_count=1;
    lab.model.observation.gesture_score=.93;lab.model.gesture_events=8;lab.model.challenge_score=5;lab.model.challenge_target=3;
    lab.model.people=3;std::strcpy(lab.model.person.data(),"成员 2");std::strcpy(lab.model.stable_gesture.data(),"three");std::strcpy(lab.model.message.data(),"保持稳定，让镜头看清你的手");
    for(unsigned i=0;i<5;++i){lab.model.mode=camera::LabMode(i);lab.paint(port,true);port.save(std::string(argv[2])+"lab-"+std::to_string(i)+suffix);}
    auto music_model=std::make_shared<music::MusicSnapshot>();music::Presentation player;player.model=music_model;
    music_model->storage_ready=music_model->audio_ready=true;music_model->volume=35;
    player.paint(port,true);port.save(std::string(argv[2])+"music-local-empty"+suffix);
    auto library=std::make_shared<music::Library>();
    library->push_back({"/performances/music/晴天.mp3","晴天.mp3",true});
    library->push_back({"/performances/music/练习曲.wav","练习曲.wav",false});
    music_model->library=library;
    player.paint(port,true);port.save(std::string(argv[2])+"music-local-idle"+suffix);
    music_model->playing=true;music_model->position_seconds=83;
    player.paint(port,true);port.save(std::string(argv[2])+"music-local-playing"+suffix);
    music_model->paused=true;player.paint(port,true);port.save(std::string(argv[2])+"music-local-paused"+suffix);
    music_model->speaker.supported=true;music_model->speaker.enabled=true;music_model->playing=false;
    player.scroll=5*lunaros::graphics::metrics::min_touch;
    player.paint(port,true);port.save(std::string(argv[2])+"music-speaker-optional"+suffix);
    music_model->speaker.receiving=true;music_model->speaker.connected=music_model->speaker.controllable=music_model->speaker.playing=true;
    player.scroll=0;player.paint(port,true);port.save(std::string(argv[2])+"music-speaker-playing"+suffix);
    player.scroll=8192;player.paint(port,true);port.save(std::string(argv[2])+"music-controls"+suffix);
    std::strcpy(music_model->error.data(),"music_stop_bluetooth_first");player.paint(port,true);port.save(std::string(argv[2])+"music-error"+suffix);
    auto connection=std::make_shared<settings::BluetoothPresentation>();connection->available=connection->enabled=true;
    connection->ble_scan_supported=connection->classic_supported=connection->pairing_supported=true;
    settings::DetailRenderer detail;settings::SystemPresentation model;model.bluetooth=connection;
    const auto connection_paint=[&](const char *name){
      assert(port.begin_widgets());assert(detail.build(port.runtime.scene(),settings::Detail::Bluetooth,model,"test","LunarOS",desktop::ProvisioningStep::Device,{1,2},hide));
      assert(port.finish_widgets("蓝牙",true));port.present_widgets();port.save(std::string(argv[2])+name+suffix);
    };
    connection_paint("settings-bluetooth-empty");
    connection->classic_supported=connection->pairing_supported=false;
    connection_paint("settings-bluetooth-s3");
    connection->classic_supported=connection->pairing_supported=true;
    connection->confirm_pending=true;connection->pairing_code=315926;connection->pairing=true;
    model.bluetooth_scroll=8192;connection_paint("settings-bluetooth-pair");
    connection->confirm_pending=false;connection->device_count=24;
    for(size_t i=0;i<24;++i){std::snprintf(connection->devices[i].name.data(),40,"附近设备 %02u",unsigned(i+1));connection->devices[i].rssi=-40-int(i);}
    connection_paint("settings-bluetooth-scroll-end");
    if constexpr(lunaros::graphics::metrics::native){
      pref=port.desktop.preferences;pref.text_size=2;port.desktop.set_preferences(pref);
      player.paint(port,true);port.save(std::string(argv[2])+"music-large"+suffix);
      connection_paint("settings-bluetooth-large");
      for(unsigned i=0;i<5;++i){lab.model.mode=camera::LabMode(i);lab.paint(port,true);port.save(std::string(argv[2])+"lab-large-"+std::to_string(i)+suffix);}
      pref=port.desktop.preferences;pref.text_size=1;port.desktop.set_preferences(pref);
    }
  }
  if constexpr(lunaros::graphics::metrics::native){
    auto &scene=port.runtime.scene();scene.clear();assert(scene.text_scale(100));
    std::vector<lunaros::graphics::Action> actions;
    const auto draw=[&]{
      assert(scene.begin(999,0x0841,true,[](void *p,lunaros::graphics::Action a){static_cast<std::vector<lunaros::graphics::Action> *>(p)->push_back(a);},&actions));
      assert(scene.label(1,{16,52,448,40},"文字大小与缓存",28,0xffff));
      assert(scene.button(2,{16,104,216,77},"保存",28,0x3186,0xffff));
      assert(scene.button(3,{248,104,216,77},"不可用",28,0x3186,0xffff,false,false));
      assert(scene.render({port.pixels.data(),port.pixels.size()}));
    };
    draw();const auto standard=port.pixels;
    assert(scene.text_scale(115));draw();assert(port.pixels!=standard);
    const auto flushes=scene.stats().flushes,glyphs=lunaros::text_raster::glyph_cache_stats().rasterizations;
    for(unsigned i=0;i<30;++i){assert(scene.text_scale(115));draw();}
    assert(scene.stats().flushes==flushes&&lunaros::text_raster::glyph_cache_stats().rasterizations==glyphs);
    assert(scene.tap(999,100,140)&&actions.size()==1&&actions.back().id==2);
    assert(scene.tap(999,300,140)&&actions.size()==1);
    assert(scene.text_scale(100));draw();assert(port.pixels==standard);
  }
}
