#pragma once
#include "lunaredu_pet_connections.h"
#include "lunaredu_pet_display_preferences.h"
#include "lunaredu_pet_feature_preferences.h"

namespace lunaredu::pet {
enum class Page { Home, Activities, Activity, Settings, Appearance, Connections, Features };
enum class UiAction : uint32_t {
  None, Home, Activities, Settings, ToggleMode, SaveDefaultMode, Voice,
  Previous, Next, SelectFirst, SelectSecond, SelectThird, CancelActivity,
  ConfirmCancel, DismissConfirm, Appearance, CycleShape, CycleColor,
  AutonomousAppearance, Connections, PairAccount, ConfigureNetwork,
  CloseConnection, RetryConnection, ConnectionNext, Features,
  ToggleFlexible, ToggleImu, ToggleVoice, ToggleAgent,
  MediaToggle, MediaPrevious, MediaNext, MediaStop, MediaOpen
};
struct UiSnapshot {
  Page page=Page::Home;
  DisplayMode mode=DisplayMode::Window;
  FeaturePreferences features;
  uint8_t feature_support=1;
  bool confirming=false;
  bool network_setup_link=false;
  size_t detail_page=0;
  uint64_t caption_page=0;
  ConnectionsView connections;
  std::string message;
  std::string setup_caption;
};
// Local navigation and transient mode only. Service state stays with providers.
class UiSession {
public:
  void enter(DisplayMode);
  void leave();
  void poll(const ConnectionPorts &,uint64_t now_ms,bool force=false);
  bool action(UiAction,const ConnectionPorts &);
  void message(std::string text);
  void setup_caption(const std::string &text);
  void presentation_changed(){invalidate();}
  void feature_support(uint8_t bits);
  const UiSnapshot &snapshot() const { return view_; }
  uint64_t input_revision() const { return input_revision_; }
  uint64_t presentation_revision() const {return presentation_revision_;}
  bool animate() const { return view_.page==Page::Home; }
private:
  bool active_=false;
  UiSnapshot view_;
  uint64_t next_poll_=0,input_revision_=1,confirmation_revision_=0;
  uint64_t presentation_revision_=1;
  uint64_t caption_started_ms_=0;
  std::string caption_;
  std::string selected_id_;
  void invalidate();
  void refresh(const ConnectionPorts &);
  bool send(const ConnectionPorts &,ConnectionCommand);
};
// Foreground-owner calls; mode changes here are session-scoped. Saving the
// default is a separate settings action and reports persistence failures.
UiSnapshot ui_snapshot();
uint64_t ui_input_revision();
bool ui_action(UiAction);
} // namespace lunaredu::pet
