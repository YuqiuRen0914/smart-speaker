#include "product/speaker_desktop.h"
#include "lunaredu_device_spi_lcd_projection.h"
#include "lunaros_device_api_service_id.h"
#include "lunaros_network_service_id.h"
#include "lunaros_storage_service_id.h"
#include "lunaros_platform_link_projection.h"
#include "lunaros_local_web_lifecycle.h"
#include "lunaros_python_device_api.h"
#include "runtime/product_runtime.h"
#include "runtime/product_composition.h"
#include "lunaredu_device_sdcard_storage.h"
#include "lunaredu_device_sdcard_tfcard_store.h"
#include "lunaredu_device_spi_lcd_surface.h"
#include "lunaros_resource.h"
#include "lunaredu_ota.h"
#include "device_api/device_api.h"
#include "device_api/projection_composition.h"
#include "lunaros_platform_link.h"
#include "kernel/product_definition.h"
#include "kernel/service.h"
#include "lunaros_network.h"
#include "configuration/device_config.h"
#include "lunaros_storage.h"
#include "runtime/system/system.h"
#include "product/product_runtime_internal.h"
#include "sdkconfig.h"
#include "smart_speaker_runtime.h"
#include "smart_speaker_sensors.h"
#include "lunaredu_pet_service.h"
#include "lunaredu_pet_application.h"
#include "lunaros_audio_focus.h"
#include "lunaros_audio_dual_i2s.h"
#include <esp_timer.h>
#include <array>
#include <vector>
#include "lunaros_micropython_lifecycle.h"
namespace smart_speaker::product::runtime_internal {
using namespace lunaredu::lunaros;
namespace {
lunaredu::services::imu::ImuService &imu_service() {
  namespace imu = lunaredu::services::imu;
  static imu::ImuService service({
      nullptr,
      [](void *, std::string &error) {
        return smart_speaker::start_smart_speaker_imu(error);
      },
      [](void *, imu::ImuSample &sample, std::string &error) {
        return smart_speaker::read_smart_speaker_imu(sample, error);
      },
      [](void *, std::string &error) {
        return smart_speaker::stop_smart_speaker_imu(error);
      },
      [](void *) { return static_cast<uint64_t>(esp_timer_get_time()); }});
  return service;
}
bool declare_standard(void *context, std::string &error) {
  return setup_standard_device_projections<standard_projection::platform_link,standard_projection::applications>(
      static_cast<ProductRuntime *>(context)->projections(), error);
}
bool declare_speaker(void *context, std::string &error) {
  auto &runtime = *static_cast<ProductRuntime *>(context);
  if (!lunaredu::device::register_device_spi_lcd_surface_projection(
          runtime.projections(), error))
    return false;
  if (!lunaredu::device::declare_tfcard_store_projection(
          runtime.projections(), error)) {
    return false;
  }
  return smart_speaker::configure_music_backend(error) && smart_speaker::declare_desktop(runtime,error) && smart_speaker::register_projection(runtime.projections(), error);
}
ServiceStartResult start_speaker(void *context) {
  auto &runtime = *static_cast<ProductRuntime *>(context);
  if (!lunaredu::device::setup_tfcard_store(false))
    return {ServiceStartDisposition::Failed, "speaker_storage_init_failed"};
  if (!smart_speaker::setup_runtime(runtime.runtime()))
    return {ServiceStartDisposition::Failed, "speaker_runtime_init_failed"};
  if (!smart_speaker::status_snapshot().audio_ready)
    return {ServiceStartDisposition::Failed, "speaker_audio_init_failed"};
  std::string error;
  if(!smart_speaker::start_desktop(runtime,error))return {ServiceStartDisposition::Failed,error};
  return {ServiceStartDisposition::Ready, "speaker_ready"};
}
ServiceStartResult start_audio(void *context) {
  auto &runtime = *static_cast<ProductRuntime *>(context);
  std::string error;
  if(smart_speaker::status_snapshot().audio_ready)return {ServiceStartDisposition::Ready,"audio_ready"};
  if(!smart_speaker::stop_audio_service(runtime.runtime(),error))return {ServiceStartDisposition::Failed,error};
  if(!lunaredu::audio::local_audio_focus().configure(runtime.context().model.control,"speaker.audio.output",
      {nullptr,[](void *,std::string &e){
        if(!lunaredu::audio::dual_i2s_audio_fade_to_mute(100)){e="audio_output_stop_pending";return false;}
        e.clear();return true;}},error))return {ServiceStartDisposition::Failed,error};
  if (!smart_speaker::start_audio_service(runtime.runtime(), error)) return {ServiceStartDisposition::Failed, error};
  return {ServiceStartDisposition::Ready, "audio_ready"};
}
bool stop_audio(void *context, std::string &error) {
  return smart_speaker::stop_audio_service(
      static_cast<ProductRuntime *>(context)->runtime(), error);
}
} // namespace
bool register_services(ProductRuntime &runtime, std::string &error) {
  smart_speaker::set_smart_speaker_imu_service(&imu_service());
  PlatformLinkConfig link;
  link.board_type = "lunaredu-smart-speaker";
  link.model = "LunarEdu Smart Speaker";
  link.open_claim = false;
#if CONFIG_LUNAREDU_SMART_SPEAKER_DEVELOPMENT_PLATFORM
  link.development_base_url = CONFIG_LUNAREDU_SMART_SPEAKER_DEVELOPMENT_PLATFORM_URL;
  if (link.development_base_url.empty()) {
    error = "speaker_development_platform_url_missing";
    return false;
  }
#endif
  link.capabilities = {"local_device_api", "smart_speaker", "ota"};
  if (!configure_platform_link_runtime(runtime.context(), link, error))
    return false;
  static constexpr ServiceDependency dependencies[] = {
      {kKernelReadyServiceId, true},
      {kStorageServiceId, true}};
  static constexpr ServiceDependency hardware_dependencies[] = {{"speaker.audio", true},{"speaker.display",true},{lunaredu::pet::kDisplayServiceId,true},{lunaredu::pet::kAnimationServiceId,true}};
  static constexpr ServiceDependency pet_dependencies[] = {{"speaker.display",true},{kStorageServiceId,true}};
  std::vector<ServiceNode> services = {
      imu_service().node(kMainServiceExecutor, ServiceCriticality::Optional),
      ServiceNode{{"speaker.audio", "Speaker Audio", kMainServiceExecutor,
                   ServiceCriticality::Required, dependencies, std::size(dependencies)},
                  start_audio, nullptr, &runtime, nullptr, nullptr,
                  [](void *context){std::string error;const bool stopped=stop_audio(context,error);return ServiceStopResult{stopped?ServiceStopDisposition::Stopped:ServiceStopDisposition::Pending,error};}},
      smart_speaker::desktop_display_service(runtime),
      ServiceNode{{lunaredu::pet::kDisplayServiceId,"Speaker Pet",kMainServiceExecutor,
                   ServiceCriticality::Required,pet_dependencies,std::size(pet_dependencies)},
        [](void *p){auto &owner=*static_cast<ProductRuntime *>(p);std::string e;
          const bool okay=lunaredu::pet::start_runtime_service(owner.context().model,{},e);
          return ServiceStartResult{okay?ServiceStartDisposition::Ready:ServiceStartDisposition::Failed,e};},
        [](void *p,std::string &e){return lunaredu::pet::stop_runtime_service(static_cast<ProductRuntime *>(p)->context().model,e);},&runtime},
      lunaredu::pet::animation_service(runtime.runtime()),
      platform_link_service_node(runtime.context()),
      ServiceNode{{"device.hardware", "Smart Speaker", kMainServiceExecutor,
                   ServiceCriticality::Required, hardware_dependencies,
                   std::size(hardware_dependencies)},
                  start_speaker, nullptr, &runtime}};
  std::vector<ProductDeclarationNode> declarations = {
      {"platform_link", declare_standard, &runtime},
      {"product.smart_speaker", declare_speaker, &runtime}};
#if CONFIG_LUNAROS_ENABLE_MICROPYTHON
  static lunaredu::micropython::RuntimeConfig python{
      .start_on_demand = true, .robot_motion = {}, .machine = {},
      .foreground_transition=[](void *,bool active,std::string &e){return smart_speaker::python_foreground(active,e);}};
  static constexpr ServiceDependency python_dependencies[] = {{kStorageServiceId, true}, {kDeviceApiServiceId, true}};
  services.push_back(lunaredu::micropython::runtime_lifecycle_node(runtime.context(), python, python_dependencies, 2));
  declarations.push_back(lunaredu::python::projection_declaration_node(runtime.projections()));
#endif
  return install_product_composition(runtime.context(),runtime.projections(),runtime.applications_service(),
      {"lunaredu-smart-speaker", services.data(), services.size(),
       declarations.data(), declarations.size()},
      error);
}
} // namespace smart_speaker::product::runtime_internal
