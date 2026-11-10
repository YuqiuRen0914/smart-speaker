#pragma once
#include <cstdint>
#include <string>

namespace lunaredu::pet {
enum class InteractionEventKind { Status, Emotion, ConversationTurn, ActivityPhase };
// Independent background work; it cannot replace listening/speaking or mutate
// the voice conversation phase. Waiting alone does not play a busy animation.
void update_background_work(bool active);
// Input strings are borrowed during the call and copied into a bounded queue.
struct InteractionEvent {
  InteractionEventKind kind = InteractionEventKind::Status;
  const char *value = nullptr;
  const char *detail = nullptr;
};
bool post_interaction_event(const InteractionEvent &event);
// Native activity overlay; RAM only, bounded and owned by a generation token.
// A completed display may use duration 0 with retain_until_interaction=true;
// the next conversation, activity or explicit clear retires it.
// It never changes saved student appearance or the actual conversation phase.
bool set_activity_appearance(uint64_t &lease,const char *shape,const char *color,
    const char *expression,uint32_t duration_ms,std::string &error,bool retain_until_interaction=false);
void clear_activity_appearance(uint64_t lease);
bool post_touch_reaction(const char *emotion);
} // namespace lunaredu::pet
