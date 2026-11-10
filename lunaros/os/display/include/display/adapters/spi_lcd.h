#pragma once
#include "display/backend.h"

namespace lunaredu::device { struct DeviceSpiLcdPanel; enum class DeviceSpiLcdController; }

namespace lunaredu::display {
enum class SpiLcdTransfer { Packed, Strided };
struct SpiLcdSurfaceRequirements {
  uint16_t width=0,height=0;
  SpiLcdTransfer transfer=SpiLcdTransfer::Strided;
  ScreenCapabilities capabilities{};
#ifdef ESP_PLATFORM
  bool initialize=true;
#else
  bool initialize=false; // Host fixtures supply the observed surface.
#endif
  const char *setup_error="screen_panel_setup_failed";
  const char *dimensions_error="screen_panel_dimensions_invalid";
};
// One retained adapter per physical panel. Drivers consume each source before
// returning; this adapter creates no worker and must outlive its ScreenBackend.
class SpiLcdAdapter {
 public:
  SpiLcdAdapter() = default;
  ~SpiLcdAdapter();
  SpiLcdAdapter(const SpiLcdAdapter &) = delete;
  SpiLcdAdapter &operator=(const SpiLcdAdapter &) = delete;
  bool prepare(const device::DeviceSpiLcdPanel &, SpiLcdTransfer,
               ScreenCapabilities, std::string &error);
  // Device owns hardware setup/acquisition; Display validates the requested
  // logical geometry and retains the transport adapter. No task is started.
  bool prepare_surface(const SpiLcdSurfaceRequirements &,std::string &error);
  void guard(void *context, bool (*active)(void *)) { guard_context_ = context; active_ = active; }
  ScreenBackend backend();
 private:
  static bool present(void *, ScreenFrameView, ScreenRegion, std::string &);
  static bool present_patch(void *, ScreenPatchView, ScreenRegion, std::string &);
  static bool wait(void *, std::string &);
  uint16_t width_ = 0, height_ = 0;
  device::DeviceSpiLcdController controller_{};
  bool panel_byte_swap_ = false;
  ScreenCapabilities capabilities_{};
  SpiLcdTransfer mode_ = SpiLcdTransfer::Packed;
  uint16_t *scratch_ = nullptr;
  void *guard_context_ = nullptr;
  bool (*active_)(void *) = nullptr;
  bool prepared_ = false;
};
} // namespace lunaredu::display
