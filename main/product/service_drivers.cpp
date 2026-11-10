#include "lunaredu_account_resources_json.h"
#include "lunaredu_device_sdcard_storage.h"
#include "lunaredu_device_sdcard_tfcard_store.h"
#include "lunaredu_device_spi_lcd_surface.h"
#include "lunaredu_lunaros/delivery/resources.h"
#include "lunaredu_lunaros/delivery/update.h"
#include "lunaredu_lunaros/device_api/device_api.h"
#include "lunaredu_lunaros/device_api/standard_projections.h"
#include "lunaredu_lunaros/device_link/device_link.h"
#include "lunaredu_lunaros/kernel/product_definition.h"
#include "lunaredu_lunaros/kernel/service_ids.h"
#include "lunaredu_lunaros/kernel/service_registry.h"
#include "lunaredu_lunaros/network/network.h"
#include "lunaredu_lunaros/persistence/settings.h"
#include "lunaredu_lunaros/persistence/storage.h"
#include "lunaredu_lunaros/persistence/system_runtime.h"
#include "product/product_runtime_internal.h"
#include "sdkconfig.h"
#include "smart_speaker_runtime.h"
#include "smart_speaker_xiaozhi.h"
#include <array>
namespace smart_speaker::product::runtime_internal {
using namespace lunaredu::lunaros;
namespace {
bool declare_standard(void *, std::string &error) {
  return setup_standard_device_projections<
      lunaredu::lunaros::standard_projection::services,
      lunaredu::lunaros::standard_projection::tasks,
      lunaredu::lunaros::standard_projection::identity,
      lunaredu::lunaros::standard_projection::device_link,
      lunaredu::lunaros::standard_projection::system,
      lunaredu::lunaros::standard_projection::storage,
      lunaredu::lunaros::standard_projection::network,
      lunaredu::lunaros::standard_projection::update>(error);
}
bool declare_speaker(void *, std::string &error) {
  if (!lunaredu::device::register_device_spi_lcd_surface_projection(error))
    return false;
  if (!smart_speaker::register_xiaozhi_projection(error))
    return false;
  if (!lunaredu::device::setup_tfcard_store(false)) {
    error = "speaker_storage_declaration_failed";
    return false;
  }
  return smart_speaker::register_projection(error);
}
void reset_declarations(void *) { setup_device_projection_registry(); }
ServiceStartResult start_speaker(void *) {
  if (!smart_speaker::setup_runtime())
    return {ServiceStartDisposition::Failed, "speaker_runtime_init_failed"};
  if (!smart_speaker::status_snapshot().audio_ready)
    return {ServiceStartDisposition::Failed, "speaker_audio_init_failed"};
  return {ServiceStartDisposition::Ready, "speaker_ready"};
}
ServiceStartResult start_audio(void *) {
  std::string error;
  if (!smart_speaker::start_audio_service(error)) return {ServiceStartDisposition::Failed, error};
  return {ServiceStartDisposition::Ready, "audio_ready"};
}
bool stop_audio(void *, std::string &error) { return smart_speaker::stop_audio_service(error); }
ServiceStartResult start_xiaozhi(void *) {
  if (!smart_speaker::setup_xiaozhi_service())
    return {ServiceStartDisposition::Failed, "xiaozhi_start_failed"};
  return {ServiceStartDisposition::Ready, "xiaozhi_initialized"};
}
} // namespace
bool register_services(std::string &error) {
  DeviceLinkConfig link;
  static const DeviceLinkObserver observer = lunaredu::account_resources::observer();
  link.observer = &observer;
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
  if (!configure_device_link_runtime(link, error))
    return false;
  static constexpr ServiceDependency dependencies[] = {
      {kSystemRuntimeServiceId, true},
      {kSettingsServiceId, true},
      {kStorageServiceId, true}};
  static constexpr ServiceDependency hardware_dependencies[] = {{"speaker.audio", true}};
  static constexpr ServiceDependency xiaozhi_dependencies[] = {
      {"speaker.audio", true},
      {kNetworkRuntimeServiceId, false}};
  const std::array<ServiceNode, 11> services = {
      system_service_node(),
      settings_service_node(),
      storage_service_node(),
      update_service_node(),
      network_service_node(),
      resource_service_node(),
      ServiceNode{{"speaker.audio", "Speaker Audio", kMainServiceExecutor,
                   ServiceCriticality::Required, dependencies, std::size(dependencies)},
                  start_audio, stop_audio},
      ServiceNode{{"assistant.xiaozhi", "XiaoZhi Assistant",
                   kNetworkServiceExecutor, ServiceCriticality::Optional,
                   xiaozhi_dependencies, std::size(xiaozhi_dependencies)},
                  start_xiaozhi},
      device_api_service_node(),
      device_link_service_node(),
      ServiceNode{{"device.hardware", "Smart Speaker", kMainServiceExecutor,
                   ServiceCriticality::Required, hardware_dependencies,
                   std::size(hardware_dependencies)},
                  start_speaker}};
  const std::array<ProductDeclarationNode, 2> declarations = {
      {{"lunaros.standard", declare_standard, nullptr},
       {"product.smart_speaker", declare_speaker, nullptr}}};
  return install_product_definition(
      {"lunaredu-smart-speaker", services.data(), services.size(),
       declarations.data(), declarations.size(), reset_declarations, nullptr},
      error);
}
} // namespace smart_speaker::product::runtime_internal
