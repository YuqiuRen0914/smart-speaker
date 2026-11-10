#pragma once
#include "app/execution/application_input.h"
#include "app/execution/application_foreground_ports.h"
#include "desktop/shell/shell.h"
#include "desktop/ui/touch_input.h"

namespace lunaredu::lunaros::apps {
struct DownloadedDesktopInput {
  ApplicationInputSample controls;
  desktop::TouchInput touch;
  uint32_t now_ms = 0;
  bool reserved_contact = false, back = false;
  int command = -1;
};
struct DownloadedDesktopResult { bool changed = false, completed = false, request_home = false; };
// Input and immutable frame ownership for one downloaded foreground. The
// product supplies logical samples; backend/runtime policy remains in the OS.
class DownloadedSurface {
 public:
  explicit DownloadedSurface(ApplicationForegroundPorts ports) : ports_(ports) {}
  app::ApplicationStartResult open(const std::string &id, std::string &error);
  bool close(std::string &error);
  bool cancel_input();
  bool update(ApplicationInputSample sample) {
    if (!running()) return false;
    sample.realtime = realtime();
    const bool accepted = input_.update(sample, &ports_, sink);
    refresh(); return accepted;
  }
  bool refresh() {
    if (!ports_.valid()) return false;
    auto next = ports_.frame(ports_.context);
    const bool changed = next != frame_;
    frame_ = std::move(next); return changed;
  }
  template<class Logical> DownloadedDesktopResult update_desktop(DownloadedDesktopInput input,Logical logical) {
    const auto previous = frame_;
    const auto point = logical(input.touch.x, input.touch.y);
    auto &sample = input.controls;
    sample.now_ms=input.now_ms;
    sample.pointer_valid = !input.reserved_contact && input.touch.available && input.touch.valid &&
        input.now_ms - input.touch.sampled_at_ms <= 120 && input.touch.contact_count <= 1 &&
        input.touch.contact_count == (input.touch.pressed ? 1 : 0) &&
        point.x >= 0 && point.x < 240 && point.y >= 0 && point.y < 240;
    sample.pointer_pressed = input.touch.pressed; sample.x = int(point.x); sample.y = int(point.y);
    if (input.command >= int(desktop::DesktopCommand::Left) && input.command <= int(desktop::DesktopCommand::Down))
      sample.direction = input.command - int(desktop::DesktopCommand::Left) + 1;
    (void)update(sample);
    return {previous != frame_, !starting_ && !running(),
        (input.back && !realtime()) || input.command == int(desktop::DesktopCommand::Back)};
  }
  const std::shared_ptr<const DrawList> &frame() const { return frame_; }
 private:
  static bool sink(void *p, int kind, int a, int b, int c) {
    auto &ports = *static_cast<ApplicationForegroundPorts *>(p);
    return ports.input(ports.context, kind, a, b, c);
  }
  bool running() const { return ports_.valid() && ports_.running(ports_.context); }
  bool realtime() const { return ports_.valid() && ports_.realtime(ports_.context); }
  ApplicationForegroundPorts ports_;
  bool starting_ = false;
  std::string starting_id_;
  ApplicationInput input_;
  std::shared_ptr<const DrawList> frame_;
};
} // namespace lunaredu::lunaros::apps
