#pragma once
#include "lunaredu_qr.h"
#include "lunaredu_qr_payload.h"
#include "desktop/ui/provisioning_step.h"
#include <algorithm>
#include <utility>
#include <string>
#include <string_view>

namespace lunaredu::desktop {
// Only the device's provisioning AP secret belongs here, never a saved
// router credential. Empty/not-yet-started APs must not yield a usable symbol.
std::string provisioning_payload(ProvisioningStep step,std::string_view ssid,
    std::string_view ap_password,std::string_view ip);
class ProvisioningQr {
 public:
  void update_url(const std::string &url);
  void update(bool active,ProvisioningStep step,std::string_view ssid,
      std::string_view password,std::string_view ip);
  void clear();
  ~ProvisioningQr();
  bool paint(int x,int y,void *context,lunaredu::lunaros::QrCode::FillRect fill,
      int max_side)const{
    return qr.paint(x,y,max_side,context,fill);
  }
  lunaredu::lunaros::QrCode qr;
 private:
  std::string cache_key_;
};
} // namespace lunaredu::desktop
