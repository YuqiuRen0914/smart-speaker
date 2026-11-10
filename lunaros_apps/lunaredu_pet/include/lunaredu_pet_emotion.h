#pragma once
#include <string>
#include "lunaredu_pet_types.h"
namespace lunaredu::pet {
lunaredu::presentation::Expression pet_expression_from_emotion(const std::string &emotion);
// Returns false when the upstream emotion is not part of the supported
// vocabulary. Unknown input must not mutate the Pet's mood.
bool try_pet_expression_from_emotion(const std::string &emotion,
                                     lunaredu::presentation::Expression &expression);
// Stable semantic state ids consumed by replaceable expression themes. These
// preserve XiaoZhi/session meaning instead of flattening every event into the
// shared coarse presentation expression enum.
const char *pet_theme_state_from_emotion(const std::string &emotion);
const char *pet_theme_state_for_conversation_phase(PetConversationPhase phase);
const char *pet_theme_state_for_expression(lunaredu::presentation::Expression expression);
// Stable event ids consumed by the replaceable theme binding table.
const char *pet_theme_event_for_state(const std::string &state_id);
bool pet_expression_from_theme_state_id(const std::string &state_id,
                                        lunaredu::presentation::Expression &expression);
const char *pet_conversation_phase_name(PetConversationPhase);
}
