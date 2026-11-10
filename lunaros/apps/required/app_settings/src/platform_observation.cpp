#include "lunaros_settings_platform_observation.h"
#include "lunaros_settings_ui_session.h"
#include "lunaros_settings_account_avatar.h"
#ifdef LUNAROS_SETTINGS_PLATFORM_COMPONENT
#include "lunaros_platform_link.h"
#include <sys/time.h>
#include <algorithm>
#endif
#ifdef LUNAROS_SETTINGS_OTA_COMPONENT
#include "lunaredu_ota.h"
#endif
namespace lunaredu::settings {
void observe_platform_settings(SystemPresentation &model,int64_t now_us,bool avatar){
#ifdef LUNAROS_SETTINGS_PLATFORM_COMPONENT
  // Constant-initialized string SSO pointers must remain in initialized data.
  // External BSS would zero those pointers before the first snapshot assignment.
  // This small control block is borrowed only by the single UI executor.
  static lunaros::PlatformLinkUiSnapshot pairing;
  if(!lunaros::platform_link_ui_snapshot_into(pairing))pairing={};
  model.platform_bound=pairing.binding.state==lunaros::PlatformLinkBindingState::Bound;
  model.pairing_pending=lunaros::platform_link_pairing_pending();
  timeval wall{};gettimeofday(&wall,nullptr);
  const int64_t remaining=pairing.expires_at_ms-(int64_t(wall.tv_sec)*1000+wall.tv_usec/1000);
  model.pairing_seconds=remaining>0?int(std::min<int64_t>(remaining/1000,INT32_MAX)):0;
  model.pairing_code=model.pairing_seconds>0?pairing.code:"";
  model.pairing_url=model.pairing_seconds>0?pairing.bind_url:"";
  model.pairing_error=pairing.error;model.account_name=pairing.account_display_name;
  if(avatar)model.account_avatar=observe_account_avatar_view({model.platform_bound,pairing.account_avatar_resource_available,
      pairing.account_id,pairing.account_avatar_resource_revision,pairing.account_avatar_resource_url,
      pairing.account_avatar_resource_format,pairing.binding.binding_epoch,pairing.account_avatar_resource_width,
      pairing.account_avatar_resource_height},uint32_t(now_us/1000));
  else model.account_avatar.reset();
  const auto check=lunaros::platform_link_firmware_check_state();
  bool available=false;model.available_version.clear();
#ifdef LUNAROS_SETTINGS_OTA_COMPONENT
  const auto update=lunaros::ota_service_available_update_snapshot();available=update.available;
  if(available)model.available_version=update.version;
#endif
  using Check=SystemPresentation::UpdateCheck;
  model.update_check=check==lunaros::PlatformLinkFirmwareCheckState::Pending?Check::Checking:
      check==lunaros::PlatformLinkFirmwareCheckState::Failed?Check::Failed:available?Check::Available:
      check==lunaros::PlatformLinkFirmwareCheckState::Succeeded?Check::Current:Check::Idle;
#else
  (void)now_us;(void)avatar;
  model.platform_bound=false;model.pairing_pending=false;model.pairing_seconds=0;
  model.pairing_code.clear();model.pairing_url.clear();model.pairing_error.clear();model.account_name.clear();model.account_avatar.reset();
  model.available_version.clear();model.update_check=SystemPresentation::UpdateCheck::Idle;
#endif
}
bool platform_settings_command(UiCommand command,std::string &error){
#ifdef LUNAROS_SETTINGS_PLATFORM_COMPONENT
  if(command==UiCommand::CheckUpdate)return lunaros::request_platform_link_firmware_check(error);
  if(command==UiCommand::InstallUpdate)return lunaros::request_platform_link_firmware_update(error);
#else
  (void)command;
#endif
  error="unknown_settings_command";return false;
}
}
