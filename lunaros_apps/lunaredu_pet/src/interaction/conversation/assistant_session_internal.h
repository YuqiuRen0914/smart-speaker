#pragma once

#include "interaction/conversation/assistant_session.h"

namespace lunaredu::pet::conversation::internal {

void enter_connecting_state_locked(const char *detail, const char *result,
                                   bool clear_session_identity);
void enter_idle_ready_state_locked(const char *detail, const char *result);
void enter_speaking_state_locked(bool entering_speaking,
                                 const char *detail,
                                 const char *result);
void enter_realtime_listening_state_locked(const char *detail,
                                           const char *result);
void enter_idle_after_tts_stop_locked(const char *detail, const char *result);
void mark_transport_connected_waiting_session_locked();
void enter_idle_after_stop_locked(const char *detail, const char *result,
                                  AssistantSessionState next_state);
void enter_idle_after_connect_failure_locked(const char *error,
                                             const char *transport_error,
                                             bool clear_session_identity);

} // namespace lunaredu::pet::conversation::internal

