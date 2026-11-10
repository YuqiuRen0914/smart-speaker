#include "product/speaker_desktop.h"
#include "runtime/product_runtime.h"
#include "runtime/application_host.h"
#include "desktop/composition/session_host.h"
#include "desktop/composition/default_contribution.h"
#include "desktop/composition/platform.h"
#include "desktop/composition/selected_applications.h"
#include "desktop/ui/physical_input.h"
#include "lunaros_settings_desktop_registration.h"
#include "lunaros_settings_system_observation.h"
#include "lunaros_settings_platform_observation.h"
#include "lunaros_files_desktop_registration.h"
#include "lunaros_app_store_desktop_registration.h"
#include "lunaros_python_desktop_entry.h"
#include "lunaros_python_runtime_entry.h"
#include "lunaredu_music_desktop.h"
#include "lunaredu_music_backend.h"
#include "lunaredu_pet_desktop_surface.h"
#include "lunaredu_pet_service.h"
#include "lunaredu_pet_device_projection.h"
#include "lunaredu_pet_display_preferences.h"
#include "lunaredu_pet_render.h"
#include "lunaros_audio_focus.h"
#include "smart_speaker_audio.h"
#include "lunaredu_device_spi_lcd_surface.h"
#include "lunaros_resource_fonts.h"
#include "lunaros_text_raster.h"
#include "lunaros_resource_service_id.h"
#include "lunaros_platform_link.h"
#include "lunaredu_ota.h"
#include "display/display_service.h"
#include "smart_speaker_runtime.h"
#include "smart_speaker_sensors.h"
#include <esp_app_desc.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <cstdio>

namespace smart_speaker {
namespace {
using namespace lunaredu;
using namespace lunaredu::lunaros;
pet::DesktopSceneSurface pet_surface{240};
bool open_music_requested=false;
const auto selection=desktop_selection(
    desktop_choice(DesktopShellEntry{{"speaker.desktop","音箱桌面",desktop::DesktopIcon::Music,0x05ca}},false),
    desktop_choice(music::DesktopEntry{}),desktop_choice(files::DesktopEntry{}),
    desktop_choice(pet::DesktopSceneEntry<&pet_surface>{}),
    desktop_choice(settings::DesktopEntry{},true,true),
    desktop_choice(apps::manager::DesktopEntry{}),desktop_choice(apps::manager::DownloadedDesktopEntry{},false)
#if CONFIG_LUNAROS_ENABLE_MICROPYTHON
    ,desktop_choice(python::DesktopEntry{})
#endif
    );
struct Configuration {
  static constexpr const char *shell_id="speaker.desktop",*settings_id="os.settings",*name="智能音箱";
  static const auto &selection(){return smart_speaker::selection;}
  static desktop::DesktopRenderOptions rendering_options(){return {};}
};
struct Model:settings::SystemPresentation {
  settings::SystemObservationCursor system_observation;
  desktop::TouchInput touch;
  uint16_t buttons=0;
  int16_t encoder_position=0;
};
using State=DefaultDesktopState<Model>;
using Payload=std::decay_t<decltype(selection)>::Presentation;
uint64_t clock_us(){return uint64_t(esp_timer_get_time());}
desktop::UiRenderHost<Payload> screen(clock_us);
desktop::UiFramePresentation presentation(const State &,const Model &model){
  desktop::DesktopStatus status;
  status.network_title=true;status.wifi=model.wifi_connected;status.wifi_connecting=model.provisioning;
  status.platform=model.platform_connected;status.platform_available=model.platform_available;
  return {status,status,model.local_ip,"","智能音箱",model.updating,false};
}
using Render=DesktopDisplayPort<screen,presentation>;
struct Platform:DesktopPlatformDefaults<Configuration,Model,State,std::monostate,
    SelectedDesktopApplications<Configuration>,DesktopApplicationPorts> {
  using Render=smart_speaker::Render;
  static const char *version(){return esp_app_get_description()->version;}
  static uint32_t now_ms(){return uint32_t(clock_us()/1000);}
  static std::monostate control_profile(){return {};}
  static bool settings_command(ApplicationContext &,settings::UiCommand command,std::string &error){
    return settings::platform_settings_command(command,error);
  }
  static auto keys(const Model &m){return std::pair{bool(m.buttons&1),bool(m.buttons&2)};}
  static auto observe_ui_model(const Model &m){
    desktop::DesktopModelObservation o;
    o.system_owned=m.updating;o.wifi_connected=m.wifi_connected;o.provisioning=m.provisioning;
    o.platform_connected=m.platform_connected;o.buttons=m.buttons;o.encoder_position=m.encoder_position;o.local_ip=m.local_ip;return o;
  }
  static desktop::DesktopUiSample input_sample(const Model &m,const State &,int64_t now){
    desktop::DesktopUiSample s;
    s.input.touch=m.touch;s.input.controls={m.buttons,2,1,m.encoder_position,false};
    s.input.now_us=now;s.input.blocked=m.updating;s.wifi_connected=m.wifi_connected;
    s.platform_available=m.platform_available;s.platform_connected=m.platform_connected;s.wifi_ssid=m.wifi_ssid.c_str();return s;
  }
  static desktop::LogicalInput logical_input(const Model &m){
    return {true,{},bool(m.buttons&2),bool(m.buttons&1),false,false,bool(m.buttons),m.touch};
  }
  static void log(const std::string &s,desktop::UiLogLevel level){
    if(level==desktop::UiLogLevel::Warning)ESP_LOGW("speaker.desktop","%s",s.c_str());
    else ESP_LOGI("speaker.desktop","%s",s.c_str());
  }
  template<class... Args>static void notification(Args &&...){}
  template<class Host>static void observe_touch(Host &,const Model &,const desktop::TouchEvent &,int64_t){}
};
using Host=DesktopSessionHost<Platform,DefaultDesktopContribution>;
Host &ui(){static Host owner;return owner;}
ApplicationHost applications;
settings::SystemObservation observation(500000,
    []{return platform_link_platform_connection_state()==PlatformLinkPlatformConnectionState::Connected;},
    [](settings::SystemPresentation &value,int64_t now){settings::observe_platform_settings(value,now);});
Model model;
bool started=false;
std::string media_token(const music::MusicSnapshot &s){
  if(!s.library||s.selected>=s.library->size())return "empty";
  return (*s.library)[s.selected].path+":"+(s.playing?(s.paused?"paused":"playing"):"idle")+":"+(s.stopping?"stopping":"ready");
}
bool configure_pet(std::string &error){
  pet::Ports ports;ports.visual_policy=pet::VisualPolicy::Companion;
  ports.target_fps=30;ports.animation_hz=30;ports.scene_width=240;ports.scene_height=240;
  ports.ready=[](void *){return screen.ready();};
  ports.on_start=[](void *,std::string &e){if(!screen.ready()){e="pet_screen_not_ready";return false;}pet_surface.clear();e.clear();return true;};
  ports.on_stop=[](void *,const char *){pet_surface.clear();screen.request_redraw();};
  ports.accept_scene=[](void *){return screen.accepts_frame();};
  ports.scene_content_top=[](void *)->uint16_t{return ui().state().desktop.preferences.auto_hide_status?0:desktop::status_layout::height;};
  ports.present_scene=[](void *,pet::Scene scene){pet_surface.publish(std::move(scene));screen.request_redraw();};
  ports.read_visual_environment=[](void *){pet::VisualEnvironment v;const auto s=music::music_activity();v.media_playing=s.playing&&!s.paused&&!s.stopping;return v;};
  ports.connections.read=[](void *,size_t,const char *,pet::ConnectionsView &out){
    out={};out.device_status="音乐陪伴";
    const auto s=music::music_snapshot();
    out.media.state=s.audio_ready&&!s.stopping?pet::ConnectionState::Ready:pet::ConnectionState::Unavailable;
    out.media.playing=s.playing;out.media.paused=s.paused;out.media.token=media_token(s);
    if(s.library&&s.selected<s.library->size())out.media.title=(*s.library)[s.selected].title;
    out.media.caption=s.paused?"音乐已暂停":s.lyric;
    return true;
  };
  ports.connections.command=[](void *,const pet::ConnectionCommand &command){
    if(command.kind!=pet::ConnectionCommandKind::Media)return pet::CommandReceipt{};
    const auto s=music::music_snapshot();
    if(!s.audio_ready||s.stopping||command.media_token!=media_token(s))return pet::CommandReceipt{pet::CommandState::Rejected,"音乐状态已变化，请重试"};
    if(command.media==pet::MediaAction::Open){open_music_requested=true;return pet::CommandReceipt{pet::CommandState::Accepted,"正在打开音乐"};}
    const uint32_t action=command.media==pet::MediaAction::Toggle?218:command.media==pet::MediaAction::Previous?223:command.media==pet::MediaAction::Next?219:227;
    std::string error;const bool okay=music::local_action(action,error);
    return pet::CommandReceipt{okay?pet::CommandState::Accepted:pet::CommandState::Rejected,okay?"已提交播放操作":"音乐暂不可用"};
  };
  return pet::configure(ports,error);
}
bool prepare_screen(void *,KernelRuntime &runtime,std::string &error){
  if(!device::device_spi_lcd_surface_setup()){error="speaker_panel_setup_failed";return false;}
  device::DeviceSpiLcdPanel panel;
  if(!device::device_spi_lcd_surface_get_panel(&panel)||panel.width!=240||panel.height!=240){error="speaker_panel_dimensions_invalid";return false;}
  if(!text_raster::setup_font({load_managed_font,discard_managed_font},error))return false;
  display::ScreenBackend backend{nullptr,240,240,nullptr,
    [](void *,std::string &e){return device::device_spi_lcd_surface_wait_idle(&e);},
    [](void *,display::ScreenPatchView p,display::ScreenRegion r,std::string &e){
      return device::device_spi_lcd_surface_draw_rgb565_strided_bitmap(r.x,r.y,r.width,r.height,p.pixels,p.stride,
          p.format==display::ScreenPixelFormat::Rgb565ByteSwapped,&e);
    },{display::ScreenPixelFormat::Rgb565,1,1,true,true,false,false},
    [](void *,void *p,void(*redraw)(void *)){device::device_spi_lcd_surface_bind_redraw(p,redraw);}};
  const display::ScreenPipelineTasks tasks{
    {.id="speaker.display",.owner_service_id="speaker.display",.role="display_io",.stack_bytes=6144,.priority=3,.core_affinity=1},
    {.id="speaker.render",.owner_service_id="speaker.display",.role="render",.stack_bytes=12288,.priority=2,.core_affinity=1}};
  desktop::UiRenderHooks hooks;
  hooks.presented=[](void *,display::ScreenFrameView frame,const display::ScreenRegion *,size_t,unsigned){pet_surface.presented(frame);};
  hooks.unchanged=[](void *,display::FrameCanvas canvas){pet_surface.unchanged(canvas);};
  return screen.setup(&runtime,backend,"speaker.desktop.ui",tasks,hooks,error);
}
}
bool declare_desktop(lunaredu::lunaros::ProductRuntime &runtime,std::string &error){
  auto &control=runtime.context().model.control;
  static constexpr const char *targets[]={"face"};
  pet::PetDeviceProjectionConfig pet_config;pet_config.description="Smart speaker music companion";
  pet_config.product_variant="smart-speaker";pet_config.motion_supported=false;pet_config.servo_supported=false;
  pet_config.supported_targets=targets;pet_config.supported_target_count=1;
  pet_config.observe_desktop_scene=[]{return pet_surface.status();};
  return control.register_provider({"speaker.audio.output","audio",0,false},error)&&
      control.register_consumer({"music.playback","speaker.audio.output",50},error)&&
      control.register_provider({"speaker.screen.main","screen",0,false},error)&&
      control.register_consumer({"speaker.desktop.ui","speaker.screen.main",50},error)&&
      screen.bind_control(control,"speaker.screen.main",error)&&
      applications.ensure(runtime.context(),lunaredu::python::runtime_selection(),error)&&
      pet::register_pet_device_projection(runtime.projections(),pet_config,error)&&
      configure_pet(error)&&applications->register_foreground(pet::application(),error)&&
      ui().declare_foregrounds(&runtime.context(),{},error)&&
      lunaredu::lunaros::apps::manager::DesktopEntry::bind_service(ui(),runtime.applications_service());
}
lunaredu::lunaros::ServiceNode desktop_display_service(lunaredu::lunaros::ProductRuntime &runtime){
  using namespace lunaredu;
  static constexpr lunaros::ServiceDependency deps[]={{lunaros::kResourceServiceId,true}};
  static display::DisplayService service(runtime.runtime(),"speaker.display",
      {nullptr,prepare_screen,deps,std::size(deps),[](void *){return screen.retained();},[](void *,std::string &e){return screen.stop(e);}});
  return service.node();
}
bool start_desktop(lunaredu::lunaros::ProductRuntime &runtime,std::string &error){
  const char *stage=nullptr;
  started=ui().start(&runtime.runtime(),&runtime.context(),{},stage,error);
  if(!started)return false;
  pet::update_environment({.boot_progress=100});
#if CONFIG_LUNAREDU_SMART_SPEAKER_AUTOPLAY_ON_BOOT
  if(!enter_music(error))return false;
  play();
#endif
  return true;
}
bool tick_desktop(lunaredu::lunaros::ProductRuntime &runtime){
  std::string error;
  const bool available=runtime.runtime().service_available("device.hardware")&&started;
  const bool ok=applications.poll(Configuration::shell_id,available,error);
  if(available){
    const auto now=esp_timer_get_time();
    static int64_t update_observed_us=0;
    static bool updating=false;
    if(!update_observed_us||now-update_observed_us>=50000){
      const auto update=ota_service_status_snapshot();
      updating=update.upload_active||update.pull_queued||update.pull_active||update.restart_pending;
      update_observed_us=now;
    }
    observation.capture_into(model,model.system_observation,now,updating,
        runtime.runtime().service_available(kPlatformLinkServiceId));
    const auto input=desktop_input();model.buttons=input.buttons;model.encoder_position=input.encoder_position;
    const auto touch=smart_speaker_touch_snapshot();
    model.touch={touch.initialized,touch.initialized&&touch.sample_count&&touch.last_error.empty(),touch.pressed,touch.contact_count,touch.x,touch.y,touch.sampled_at_ms};
    ui().update(model,now);ui().service_redraw();
    pet::tick_runtime();
    if(open_music_requested){open_music_requested=false;(void)enter_music(error);}
  }
  return ok&&available&&applications.ready()&&ui().ready();
}
bool python_foreground(bool active,std::string &error){return lunaredu::python::DesktopEntry::foreground(ui(),active,error);}
bool enter_music(std::string &error){
  if(!started){error="speaker_desktop_not_ready";return false;}
  const auto update=ota_service_status_snapshot();
  if(update.upload_active||update.pull_queued||update.pull_active||update.restart_pending){error="firmware_update_in_progress";return false;}
  if(ui().foreground_is(lunaredu::music::kApplicationId)){error.clear();return true;}
  return ui().applications().switch_to(lunaredu::music::kApplicationId,error);
}
void fill_desktop_status(lunaredu::lunaros::DeviceApiContext &,JsonObject payload){
  const auto metrics=screen.status();const auto state=ui().status();
  payload["initialized"]=screen.ready();payload["desktop"]=true;payload["microphone"]=false;
  payload["local_page"]=state.page;payload["canvas_bytes"]=metrics.storage.retained_bytes;
  payload["lifecycle"]=lunaredu::display::screen_lifecycle_name(metrics.storage.lifecycle);
}
}
