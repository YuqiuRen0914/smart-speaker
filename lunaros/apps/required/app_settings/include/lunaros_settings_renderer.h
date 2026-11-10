#pragma once
#include "desktop/shell/shell.h"
#include "graphics/scene.h"
#include "lunaros_settings_detail.h"
#include "lunaros_settings_menu.h"
#include "desktop/ui/provisioning_view.h"
#include "desktop/graphics/canvas.h"
namespace lunaredu::settings {
bool build_preferences_surface(lunaros::graphics::Scene &,const desktop::DesktopShell &);
struct DetailActions { uint32_t confirm = 1, back = 2; };
// One renderer owner; content and QR cache do not read services or create tasks.
class DetailRenderer {
 public:
  static constexpr int kProvisioningQrSide = lunaros::graphics::metrics::px(200);
  static constexpr int kSetupLinkQrSide = lunaros::graphics::metrics::px(200);
  static const char *title(Detail, const SystemPresentation &);
  bool build(lunaros::graphics::Scene &, Detail, const SystemPresentation &,
             const char *version, const char *device_name,
             desktop::ProvisioningStep, DetailActions = {},bool auto_hide_status=false,unsigned focus=0);
  bool paint_overlay(void *, lunaros::QrCode::FillRect) const;
  void paint_activity(lunaros::graphics::canvas::Surface) const;
  void clear() { qr_.clear(); overlay_ = false; avatar_.reset(); }
 private:
  desktop::ProvisioningQr qr_;
  std::shared_ptr<const desktop::DesktopImage> avatar_;
  bool setup_link_qr_ = false;
  bool overlay_ = false;
  bool updating_ = false;
  unsigned activity_phase_ = 0;
};
}
