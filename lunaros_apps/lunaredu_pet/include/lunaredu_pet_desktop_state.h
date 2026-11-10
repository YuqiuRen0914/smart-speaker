#pragma once
#include "lunaredu_lunaros/desktop/core/view_data.h"
namespace lunaredu::pet {
// Borrowed, read-only input. Valid for one call on the display owner.
struct DesktopViewState {
 bool pet_expression_fullscreen;
 bool pet_subtitles_enabled;
 bool pet_continuous_conversation;
 const std::string &device_serial_number;
 bool assistant_enabled;
 bool assistant_service_enabled;
 bool assistant_microphone_enabled;
 bool assistant_activation_ready;
 bool assistant_activation_pending;
 const std::string &assistant_activation_code;
 bool assistant_transport_connected;
 const std::string &assistant_session_state;
 const std::string &assistant_last_error;
 const std::string &assistant_transcript_role;
 const std::string &assistant_transcript_text;
 uint32_t assistant_transcript_revision;
 bool assistant_suspended;
 bool assistant_speaking;
 bool network_provisioning;
 bool network_connected;
 bool network_connecting;
 bool pet_companion_mode = false;
};
void apply_desktop_view_state(const DesktopViewState &state);
}
