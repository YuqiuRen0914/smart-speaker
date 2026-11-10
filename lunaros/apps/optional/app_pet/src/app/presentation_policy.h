#pragma once

#include "lunaredu_pet_snapshot.h"
#include "lunaredu_pet_visual_environment.h"
#include <cstdint>
#include <optional>
#include <string>

namespace lunaredu::pet::presentation_policy {

struct State {
    std::string visible_expression;
    uint32_t expression_started_ms = 0;
    bool was_online_unbound = false;
    bool receive_intro_pending = false;
};

struct Decision {
    std::string expression;
    std::optional<uint32_t> next_due_ms;
};

inline uint32_t minimum_duration_ms(const std::string &state) {
    if (state == "loading") return 1500;
    if (state == "radar") return 1300;
    if (state == "orbit") return 1000;
    if (state == "receiving") return 1700;
    return 0;
}

inline std::string base_expression(const PetVisualSnapshot &pet, bool provisioning,
                                   bool connection_animation = true) {
    if (provisioning) return "radar";
    if (!pet.boot_complete) return connection_animation ? "orbit" : "loading";
    switch (pet.conversation_phase) {
    case PetConversationPhase::Listening: return "dictating";
    case PetConversationPhase::Thinking: return "thinking";
    case PetConversationPhase::Activating:
    case PetConversationPhase::Connecting:
        if (connection_animation) return "orbit";
        break;
    default: break;
    }
    const auto &theme = pet.expression_state;
    if (theme == "activating" || theme == "connecting")
        return connection_animation ? "orbit" : "idle";
    if (theme == "listening") return "dictating";
    if (theme == "session_thinking") return "thinking";
    if (theme == "alert") return "alerting";
    if (theme == "neutral" || theme == "relaxed") return "idle";
    if (theme == "funny" || theme == "silly") return "excited";
    if (theme == "winking") return "playful";
    if (theme == "loving" || theme == "delicious") return "happy";
    if (theme == "embarrassed") return "shy";
    if (theme == "confident") return "proud";
    if (theme == "crying") return "sad";
    if (theme == "sleepy") return "drowsy";
    if (theme == "shocked") return "scared";
    return theme;
}

inline Decision resolve(const PetVisualSnapshot &pet, VisualPolicy policy,
                        const VisualEnvironment &environment, uint32_t now_ms,
                        State &state) {
    const bool bound_companion = policy == VisualPolicy::PairingDevice &&
        (environment.binding_committed || environment.link == VisualLinkState::Bound);
    const bool connection_animation = policy != VisualPolicy::Companion &&
        !bound_companion;
    Decision decision{base_expression(pet,
                      connection_animation && environment.provisioning,
                      connection_animation),
                      std::nullopt};
    if (policy == VisualPolicy::PairingDevice && !bound_companion) {
        const bool awaiting = environment.link == VisualLinkState::AwaitingPairing;
        const bool online_unbound = awaiting ||
            environment.link == VisualLinkState::OnlineUnbound;
        if (online_unbound && !state.was_online_unbound)
            state.receive_intro_pending = true;
        if (!online_unbound) state.receive_intro_pending = false;
        state.was_online_unbound = online_unbound;
        const float progress_fraction = awaiting
            ? environment.pairing_remaining_fraction : -1.0f;
        if (!pet.boot_complete) decision.expression = "loading";
        else if (environment.provisioning) decision.expression = "radar";
        else if (environment.link == VisualLinkState::Offline)
            decision.expression = "orbit";
        else if (online_unbound &&
                 (state.receive_intro_pending || !awaiting ||
                  progress_fraction <= 0.0f))
            decision.expression = "receiving";
        else if (awaiting) decision.expression = "progress";

        const std::string requested_expression = decision.expression;
        if (state.visible_expression.empty()) {
            state.visible_expression = "loading";
            state.expression_started_ms = now_ms;
        } else if (state.visible_expression != requested_expression &&
                   now_ms - state.expression_started_ms >=
                       minimum_duration_ms(state.visible_expression)) {
            state.visible_expression = requested_expression;
            state.expression_started_ms = now_ms;
        }
        if (state.visible_expression != requested_expression)
            decision.next_due_ms = state.expression_started_ms +
                minimum_duration_ms(state.visible_expression);
        if (state.visible_expression == "receiving" &&
            now_ms - state.expression_started_ms >= 1700 && awaiting &&
            progress_fraction > 0.0f) {
            state.receive_intro_pending = false;
            decision.next_due_ms = now_ms + 1;
        } else if (state.visible_expression == "receiving" && awaiting &&
                   progress_fraction > 0.0f &&
                   state.receive_intro_pending && !decision.next_due_ms) {
            decision.next_due_ms = state.expression_started_ms + 1700;
        }
        decision.expression = state.visible_expression;
    } else if (policy == VisualPolicy::PairingDevice) {
        state = {};
    }
    if(environment.media_playing&&pet.boot_complete&&!environment.provisioning&&
       (pet.conversation_phase==PetConversationPhase::Disabled||pet.conversation_phase==PetConversationPhase::Ready)&&
       decision.expression=="idle")decision.expression="bouncing";
    if (environment.expression_override && environment.expression_override[0])
        decision.expression = environment.expression_override;
    return decision;
}

} // namespace lunaredu::pet::presentation_policy
