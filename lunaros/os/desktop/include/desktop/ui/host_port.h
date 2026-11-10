#pragma once
#include "desktop/ui/route.h"
#include <cstdint>
#include <memory>
#include <string>
namespace lunaredu::display { struct ScreenSessionSnapshot; }
namespace lunaredu::lunaros::graphics {struct Action;enum class FocusCommand:uint8_t;}
namespace lunaredu::desktop {
struct UiEntry; struct TouchInput; struct TouchEvent;
class DesktopApplicationCatalog;
enum class DesktopCommand : uint8_t;
enum class UiLogLevel { Info,Warning };
struct UiPoint { float x,y; };
struct UiAppearance { bool auto_hide_status=false,antialias=true; };
// Calls remain serialized on the input owner. Apps receive logical operations,
// not the desktop session, mutable Shell, graphics runtime or command storage.
class UiHostPort {
 public:
  virtual ~UiHostPort()=default;
  virtual bool ready_ui() const=0;
  virtual UiAppearance ui_appearance() const { return {}; }
  virtual app::ViewRoute ui_route() const=0;
  virtual app::ViewRoute previous_ui_route() const=0;
  virtual bool take_ui_redraw()=0;
  virtual void request_ui_redraw()=0;
  virtual bool enter_ui(UiEntry)=0;
  virtual bool application_entered(UiEntry)=0;
  virtual void select_application(const char *id,bool remember_location=false)=0;
  virtual void display_application(const std::string &id)=0;
  virtual std::string displayed_application_title() const=0;
  virtual bool catalog_publish_allowed() const=0;
  virtual bool publish_catalog(std::shared_ptr<const DesktopApplicationCatalog>)=0;
  virtual UiPoint logical_point(int x,int y) const=0;
  virtual TouchEvent update_touch(const TouchInput &,int64_t,bool pull_refresh)=0;
  virtual int pull_refresh_distance(int64_t) const=0;
  virtual bool tap(int x,int y)=0;
  virtual bool take_action(lunaros::graphics::Action &)=0;
  // Logical opt-in traversal intent; no graphics runtime or LVGL object leaks
  // to App input contexts. Hosts without focus support safely reject it.
  virtual bool focus_ui(lunaros::graphics::FocusCommand){return false;}
  virtual void invalidate_input()=0;
  virtual void wake_ui()=0;
  virtual bool launch_ui(const char *id,bool downloaded)=0;
  virtual void render_ui(uint32_t copy_ticket=0)=0;
  virtual void rearm_ui()=0;
  virtual bool ui_blocked() const=0;
  virtual bool queue_command(DesktopCommand)=0;
  virtual bool take_command(DesktopCommand &)=0;
  virtual void discard_commands()=0;
  virtual display::ScreenSessionSnapshot ui_metrics()=0;
  virtual void log_ui(const char *owner,const std::string &record,UiLogLevel=UiLogLevel::Info)=0;
  virtual void unavailable(const char *title,const char *reason)=0;
  virtual void home()=0;
};
}
