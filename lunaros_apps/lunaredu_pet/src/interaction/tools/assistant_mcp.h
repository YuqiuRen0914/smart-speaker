#pragma once

#include <ArduinoJson.h>
#include <mutex>
#include <string>
#include <vector>
#include "lunaredu_pet_conversation.h"
#include "interaction/tools/assistant_mcp_queue.h"

namespace lunaredu::pet::conversation::internal {

// Borrowed state owned by the conversation service. All references must remain
// valid until dispatch returns. Enter with the conversation mutex locked; tool
// callbacks temporarily release it and must not retain any borrowed JSON data.
// The dispatcher owns protocol encoding, not transport, audio or lifecycle.
struct McpContext {
  PendingMcpMessages &responses;
  const std::vector<ToolRegistration> &tools;
  const std::string &session_id;
  std::string &last_error;
};

void handle_mcp_message_locked(McpContext context, JsonVariant payload,
                               std::unique_lock<std::mutex> &runtime_lock);
} // namespace lunaredu::pet::conversation::internal
