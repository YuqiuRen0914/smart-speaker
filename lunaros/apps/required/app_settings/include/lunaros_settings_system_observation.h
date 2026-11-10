#pragma once
#include "lunaros_settings_observed_presentation.h"
#include <cstdint>
namespace lunaredu::settings {
struct UpdateProgressObservation {
  unsigned percent=0;
  bool known=false,restarting=false;
};
// Screen-local facts. Provisioning credentials remain on the local screen and
// must never enter remote diagnostics or Device API publications.
class SystemObservation {
 public:
  explicit SystemObservation(int64_t interval_us,bool (*platform_connected)()=nullptr,void (*observe_details)(SystemPresentation &,int64_t)=nullptr)
      :interval_us_(interval_us),platform_connected_(platform_connected),observe_details_(observe_details){}
  SystemPresentation capture(int64_t now_us,bool updating,bool platform_available);
  void capture_into(SystemPresentation &,SystemObservationCursor &,int64_t now_us,bool updating,bool platform_available,
      const UpdateProgressObservation *progress=nullptr);
 private:
  SystemPresentation cached_, pending_;
  uint64_t revision_=0;
  uint32_t network_sequence_=0;
  bool have_network_=false;
  int64_t observed_at_us_=0,interval_us_;
  bool (*platform_connected_)()=nullptr;
  void (*observe_details_)(SystemPresentation &,int64_t)=nullptr;
};
}
