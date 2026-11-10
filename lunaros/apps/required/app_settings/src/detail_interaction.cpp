#include "lunaros_settings_detail_interaction.h"

namespace lunaredu::settings {
DetailInteraction update_detail_interaction(Detail detail, desktop::ProvisioningStep &step,
    const SystemPresentation &model, bool confirm, bool back, int navigation,
    int64_t now_us, int64_t &last_refresh_us) {
  if (back && !model.updating) return {true};
  const bool provision_page = detail == Detail::Provision;
  if (model.provisioning && !model.updating && provision_page && (confirm || navigation)) {
    if(step==desktop::ProvisioningStep::Device&&model.setup_url.empty())step=desktop::ProvisioningStep::Hotspot;
    step = step == desktop::ProvisioningStep::Device ? desktop::ProvisioningStep::Hotspot :
      step == desktop::ProvisioningStep::Hotspot ? desktop::ProvisioningStep::Setup :
      model.setup_url.empty() ? desktop::ProvisioningStep::Hotspot : desktop::ProvisioningStep::Device;
    return {false, true};
  }
  if (now_us - last_refresh_us >= (model.updating?100000:500000)) { last_refresh_us = now_us; return {false, true}; }
  return {};
}
}
