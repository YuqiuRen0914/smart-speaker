#pragma once
#include "desktop/ui/text_input.h"
#include <memory>
#include "desktop/ui/surface_extension.h"
#include "desktop/ui/content_layout.h"

namespace lunaredu::desktop {
struct DesktopShell;
struct DesktopPainter;
enum class DesktopCommand : uint8_t;
enum class WifiView : uint8_t { List, Password, Review, Connecting, Failed, Connected, Forget };
enum class WifiScan : uint8_t { Idle, Scanning, Ready, Failed };
enum class WifiRequest : uint8_t { None, Scan, Connect, Forget };
struct WifiAccessPoint {
  char ssid[33]{};
  int16_t rssi=-127;
  int8_t saved_index=-1;
  bool secure=false,supported=true;
};
struct WifiContent {
  std::array<WifiAccessPoint,24> networks{};
  uint8_t count=0;
  WifiScan scan=WifiScan::Idle;
  char connected_ssid[33]{};
  int8_t connected_saved_index=-1;
  bool available=false,editable=true;
};
inline constexpr char kWifiSurfaceStateKind=0;
struct WifiBrowserState : DesktopSurfaceState {
  WifiBrowserState():DesktopSurfaceState(&kWifiSurfaceStateKind) {}
  std::shared_ptr<const WifiContent> content;
  WifiView view=WifiView::List;
  WifiRequest request=WifiRequest::None;
  WifiAccessPoint selected;
  TextInputState input;
  uint8_t focus=0,scroll=0;
  uint32_t generation=0;
  char message[96]{};
};
enum class WifiControl : uint8_t { Network, Scan, Mobile, Edit, Connect, Retry, Networks, Forget, Cancel, ConfirmForget, Current };
struct WifiButton {WifiControl action;uint8_t index=0;const char *label;};
using WifiButtons=std::array<WifiButton,27>;
struct WifiListLayout {
  struct Geometry{int top,height,pitch,row,rows;};
  static constexpr Geometry compile(bool hidden){
    namespace m=lunaros::graphics::metrics;
    const ContentLayout c(hidden);
    const int top=c.top+(m::native?(m::row_min+m::gap)*2:92),height=c.bottom-top;
    const int rows=m::native?std::max(1,(height+c.gap)/(m::row_min+c.gap)):2;
    const int pitch=(height+c.gap)/rows;
    return {top,height,pitch,pitch-c.gap,rows};
  }
  ContentLayout content;
  explicit constexpr WifiListLayout(bool hide_status):content(hide_status){}
  constexpr int top() const;
  constexpr int height() const;
  constexpr int pitch() const;
  constexpr int row_height() const;
  constexpr int rows() const;
};
inline constexpr WifiListLayout::Geometry wifi_list_geometry[]{WifiListLayout::compile(false),WifiListLayout::compile(true)};
inline constexpr int WifiListLayout::top()const{return wifi_list_geometry[content.top==ContentLayout(true).top].top;}
inline constexpr int WifiListLayout::height()const{return wifi_list_geometry[content.top==ContentLayout(true).top].height;}
inline constexpr int WifiListLayout::pitch()const{return wifi_list_geometry[content.top==ContentLayout(true).top].pitch;}
inline constexpr int WifiListLayout::row_height()const{return wifi_list_geometry[content.top==ContentLayout(true).top].row;}
inline constexpr int WifiListLayout::rows()const{return wifi_list_geometry[content.top==ContentLayout(true).top].rows;}
std::shared_ptr<const WifiBrowserState> wifi_browser(const DesktopShell &);
void publish_wifi_browser(DesktopShell &,std::shared_ptr<const WifiBrowserState>);
size_t wifi_buttons(const WifiBrowserState &,WifiButtons &);
bool wifi_password_valid(const WifiBrowserState &);
void open_desktop_wifi(DesktopShell &);
void close_desktop_wifi(DesktopShell &);
bool wifi_command(DesktopShell &,DesktopCommand);
bool wifi_activate(DesktopShell &,size_t index);
bool wifi_scroll(DesktopShell &,unsigned row);
bool wifi_tick(DesktopShell &,uint32_t now_ms);
} // namespace lunaredu::desktop
