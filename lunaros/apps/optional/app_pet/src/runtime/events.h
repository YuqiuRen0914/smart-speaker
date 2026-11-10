#pragma once
#include <cstddef>
namespace lunaredu::pet::runtime {
inline constexpr size_t kEventTextSize = 64;
inline constexpr size_t kEventDetailSize = 128;
enum class RobotEventKind : uint8_t {
  AssistantStatus,
  AssistantEmotion,
  ConversationTurn,
  ActivityPhase,
  Touch,
};

struct RobotEvent {
  RobotEventKind kind = RobotEventKind::AssistantStatus;
  char text[kEventTextSize] = {};
  char detail[kEventDetailSize] = {};
};

} // namespace lunaredu::pet::runtime
