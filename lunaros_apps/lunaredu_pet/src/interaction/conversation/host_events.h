#pragma once

#include <string>

#include "lunaredu_pet_conversation.h"

void conversation_set_host_callbacks(
    const lunaredu::pet::conversation::HostCallbacks &callbacks);
void conversation_emit_status(const char *state, const char *detail = nullptr,
                            const char *result = nullptr,
                            const char *error = nullptr);
void conversation_emit_emotion(const std::string &emotion);
void conversation_emit_transcript(const std::string &text);
std::string conversation_host_last_error();
bool conversation_request_audio_focus(const char *reason, uint32_t timeout_ms);
