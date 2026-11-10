#pragma once

#include "app/presentation_policy.h"
#include "lunaredu_pet_render.h"

#include <cstdint>
#include <optional>
#include <string>

namespace lunaredu::pet {

// Adapts the semantic Pet snapshot and product visual environment to the
// native animator. Only the foreground owner calls project() and reset().
class PresentationProjector {
 public:
  void reset();
  void project(const Ports &ports, uint32_t now_ms,
               const std::string &factory_override);

 private:
  struct Inputs {
    uint32_t expression_revision = UINT32_MAX;
    PetConversationPhase phase = PetConversationPhase::Disabled;
    bool boot_complete = false;
    bool provisioning = false;
    VisualLinkState link = VisualLinkState::Unspecified;
    bool pairing_positive = false;
    std::string expression_state;
    std::string expression_override;
    bool valid = false;
    bool media_playing=false;
    bool binding_committed = false;
  };

  uint32_t applied_appearance_revision_ = UINT32_MAX;
  std::string applied_expression_;
  presentation_policy::State policy_state_;
  Inputs inputs_;
  std::string resolved_expression_;
  std::optional<uint32_t> next_due_ms_;
};

} // namespace lunaredu::pet
