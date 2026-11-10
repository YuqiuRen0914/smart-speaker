#include "diagnostics/function_probe.h"
#include "app/presentation_projector.h"
#include "app/foreground_work.h"
#include "lunaredu_pet_snapshot.h"
#include "pet_render_core.h"

#include <utility>

namespace lunaredu::pet {

void PresentationProjector::reset() {
  applied_appearance_revision_ = UINT32_MAX;
  applied_expression_.clear();
  policy_state_ = {};
  inputs_ = {};
  resolved_expression_.clear();
  next_due_ms_.reset();
}

void PresentationProjector::project(const Ports &ports, uint32_t now_ms,
                                    const std::string &factory_override) {
  lunaredu::system::probe::Scope diagnostic_scope(lunaredu::system::probe::Point::Projection);
  using foreground_work::Stage;
  auto pet = foreground_work::measure(Stage::VisualSnapshot,[&]{
    return visual_snapshot(applied_appearance_revision_,inputs_.expression_revision);
  });
  const VisualEnvironment environment = foreground_work::measure(Stage::PlatformEnvironment,[&]{
    return ports.read_visual_environment ? ports.read_visual_environment(ports.context) : VisualEnvironment{};
  });
  if (ports.visual_policy == VisualPolicy::PairingDevice)
    pet_render_set_progress_fraction(
        environment.link == VisualLinkState::AwaitingPairing
            ? environment.pairing_remaining_fraction : -1.0f);

  const char *expression_override = environment.expression_override
      ? environment.expression_override : "";
  const bool pairing_positive = environment.pairing_remaining_fraction > 0.0f;
  const bool due = next_due_ms_ &&
      static_cast<int32_t>(now_ms - *next_due_ms_) >= 0;
  if (!inputs_.valid || due ||inputs_.media_playing!=environment.media_playing||
      inputs_.binding_committed != environment.binding_committed ||
      inputs_.expression_revision != pet.expression_revision ||
      inputs_.phase != pet.conversation_phase ||
      inputs_.boot_complete != pet.boot_complete ||
      inputs_.provisioning != environment.provisioning ||
      inputs_.link != environment.link ||
      inputs_.pairing_positive != pairing_positive ||
      inputs_.expression_override != expression_override) {
    if (inputs_.valid &&
        inputs_.expression_revision == pet.expression_revision)
      pet.expression_state = inputs_.expression_state;
    auto decision = foreground_work::measure(Stage::Policy,[&]{return presentation_policy::resolve(
        pet, ports.visual_policy, environment, now_ms, policy_state_);});
    resolved_expression_ = std::move(decision.expression);
    next_due_ms_ = decision.next_due_ms;
    inputs_ = {pet.expression_revision, pet.conversation_phase,
               pet.boot_complete, environment.provisioning, environment.link,
               pairing_positive, pet.expression_state, expression_override,
               true};
    inputs_.media_playing=environment.media_playing;
    inputs_.binding_committed = environment.binding_committed;
  }

  if (applied_appearance_revision_ != pet.appearance_revision) {
    if (pet_render_set_shape(pet.shape.c_str()) &&
        pet_render_set_color(pet.color.c_str()))
      applied_appearance_revision_ = pet.appearance_revision;
  }
  const std::string &expression = factory_override.empty()
      ? resolved_expression_ : factory_override;
  if (applied_expression_ != expression &&
      pet_render_set_state(expression.c_str()))
    applied_expression_ = expression;
}

} // namespace lunaredu::pet
