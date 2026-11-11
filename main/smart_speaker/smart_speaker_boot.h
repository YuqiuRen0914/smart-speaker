#pragma once

#include <cstdint>
#include <lvgl.h>

namespace smart_speaker {

// Read-only observations from the existing network owner. No connection or
// clock operation is started by the boot view.
class SpeakerHome;

struct BootNetworkStatus {
  bool connected = false;
  bool clock_valid = false;
  bool needs_setup = false;
};

class SpeakerBootScreen {
 public:
  void create(lv_obj_t *parent, SpeakerHome &home, const char *version, uint32_t now_ms);
  // Called by the resident display loop; false means its objects are released.
  bool update(uint32_t now_ms, const BootNetworkStatus &network, bool home_target = true);
  void dismiss();
  bool active() const { return root_ != nullptr; }
  // The same home clock is settling into its final position.
  bool revealing() const { return root_ && finishing_; }

 private:
  SpeakerHome *home_ = nullptr; // Borrowed; owned by the resident display task.
  lv_obj_t *root_ = nullptr, *line_ = nullptr;
  lv_obj_t *status_ = nullptr, *version_ = nullptr;
  uint32_t started_ms_ = 0, finishing_ms_ = 0;
  bool finishing_ = false;
};

} // namespace smart_speaker
