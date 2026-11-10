#include "lunaredu_pet_visual_port.h"
#include "lunaredu_pet_desktop_view_api.h"
#include "lunaredu_pet_desktop_state.h"
#include <algorithm>
#include <array>
#include "lunaredu_pet_desktop_view.h"
#include "lunaredu_lunaros/desktop/local/application_view.h"
#include "view_internal.h"
#include <cstdio>
#include <cstring>
namespace {
using namespace lunaredu::desktop;
bool update(const lunaredu::pet::DesktopViewState &state) {
 auto &model = *pet_view_model();
 pet_view_begin_update();
 lunaros_set_pet_companion_mode(state.pet_companion_mode);
 bool changed = false;
 changed |= std::strcmp(model.device_serial_number, state.device_serial_number.c_str()) != 0;
 std::snprintf(model.device_serial_number, sizeof(model.device_serial_number), "%s", state.device_serial_number.c_str());
 changed |= model.assistant_enabled != static_cast<decltype(model.assistant_enabled)>(state.assistant_enabled);
 model.assistant_enabled = static_cast<decltype(model.assistant_enabled)>(state.assistant_enabled);
 changed |= model.assistant_microphone_enabled != static_cast<decltype(model.assistant_microphone_enabled)>(state.assistant_microphone_enabled);
 model.assistant_microphone_enabled = static_cast<decltype(model.assistant_microphone_enabled)>(state.assistant_microphone_enabled);
 changed |= model.network_provisioning != static_cast<decltype(model.network_provisioning)>(state.network_provisioning);
 model.network_provisioning = static_cast<decltype(model.network_provisioning)>(state.network_provisioning);
 changed |= model.network_connected != static_cast<decltype(model.network_connected)>(state.network_connected);
 model.network_connected = static_cast<decltype(model.network_connected)>(state.network_connected);
 changed |= model.network_connecting != static_cast<decltype(model.network_connecting)>(state.network_connecting);
 model.network_connecting = static_cast<decltype(model.network_connecting)>(state.network_connecting);
  lunaros_set_assistant_state(
      state.assistant_activation_ready, state.assistant_activation_pending,
      state.assistant_activation_code.c_str(),
      state.assistant_transport_connected,
      state.assistant_session_state.c_str(),
      state.assistant_last_error.c_str(), state.assistant_service_enabled,
      state.assistant_suspended, state.assistant_speaking);
  lunaros_set_assistant_transcript(
      state.assistant_transcript_role.c_str(),
      state.assistant_transcript_text.c_str(),
      state.assistant_transcript_revision);
  // Product-provided visuals refresh on the UI owner; an absent renderer
  // leaves Pet's fallback view and conversation controls available.
  pet_view_sync_visual();
  lunaros_set_eyes_mode(state.pet_expression_fullscreen ? 1 : 0);
  lunaros_set_pet_preferences(
      state.pet_subtitles_enabled ? 1 : 0,
      state.pet_continuous_conversation ? 1 : 0);
 return pet_view_end_update() || changed;
}
const lunaredu::desktop::ApplicationViewRoute routes[] = {
 {"desktop", false},
 {"eyes", true},
 {"pet-fullscreen", true},
 {"pet-binding", false},
};
}
namespace {
bool pending_change=false;
bool take_change() {const bool changed=pet_view_take_change() || pending_change;pending_change=false;return changed;}
}
namespace lunaredu::pet {
void apply_desktop_view_state(const DesktopViewState &state) {pending_change |= update(state);}
bool register_desktop_view() {
 return lunaredu::desktop::register_application_view({routes, sizeof(routes)/sizeof(routes[0]), pet_view_mount, pet_view_leave, take_change});
}
}
