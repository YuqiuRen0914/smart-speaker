#include "interaction/tools/assistant_mcp.h"
#include <cassert>
#include <iostream>

namespace lunaredu::pet::conversation::internal {
struct TestContext {
  std::mutex mutex;
  struct { std::string session_id = "session-a"; } transport;
  std::string last_error;
  PendingMcpMessages pending_mcp_messages;
  std::vector<ToolRegistration> external_tools;
};
TestContext g_runtime;
}
namespace {
int called = 0;
bool reset_session = false;
bool change_session_id = false;
JsonDocument *source_message = nullptr;
bool handler(void *, const char *arguments, std::string &response, std::string &) {
  using namespace lunaredu::pet::conversation::internal;
  // A real cross-application callback must be able to query assistant state.
  std::unique_lock<std::mutex> nested(g_runtime.mutex, std::try_to_lock);
  assert(nested.owns_lock());
  assert(std::string(arguments) == "{\"enabled\":false}");
  ++called;
  source_message->clear(); // The next network message can replace rx_doc.
  // Another producer cannot steal the reserved maximum-sized response slot.
  assert(g_runtime.pending_mcp_messages.try_push(std::string(32768, 'x')));
  assert(!g_runtime.pending_mcp_messages.try_push("overflow"));
  if (reset_session) g_runtime.pending_mcp_messages.clear();
  if (change_session_id) g_runtime.transport.session_id = "session-b";
  // Reallocation must not invalidate the borrowed tool entry used by dispatch.
  g_runtime.external_tools.clear();
  g_runtime.external_tools.shrink_to_fit();
  response = "applied"; return true;
}
}
int main() {
  using namespace lunaredu::pet::conversation::internal;
  JsonDocument request, response;
  source_message = &request;
  auto setup = [&] {
    g_runtime.external_tools.push_back({"apps.call", "call", "{}", handler, nullptr});
    assert(deserializeJson(request, R"({"method":"tools/call","id":"request-7","params":{"name":"apps.call","arguments":{"enabled":false}}})") == DeserializationError::Ok);
  };
  std::unique_lock<std::mutex> lock(g_runtime.mutex);
  setup();
  handle_mcp_message_locked({g_runtime.pending_mcp_messages,
      g_runtime.external_tools, g_runtime.transport.session_id,
      g_runtime.last_error}, request.as<JsonVariant>(), lock);
  assert(lock.owns_lock() && called == 1);
  assert(g_runtime.pending_mcp_messages.pop() == std::string(32768, 'x'));
  assert(deserializeJson(response, g_runtime.pending_mcp_messages.pop()) == DeserializationError::Ok);
  assert(response["payload"]["id"] == "request-7");
  assert(response["payload"]["result"]["isError"] == false);
  assert(response["payload"]["result"]["content"][0]["text"] == "applied");
  assert(g_runtime.pending_mcp_messages.empty());
  assert(g_runtime.pending_mcp_messages.can_accept(2, 65536));

  setup(); reset_session = true;
  handle_mcp_message_locked({g_runtime.pending_mcp_messages,
      g_runtime.external_tools, g_runtime.transport.session_id,
      g_runtime.last_error}, request.as<JsonVariant>(), lock);
  assert(called == 2 && g_runtime.pending_mcp_messages.empty());
  assert(g_runtime.pending_mcp_messages.can_accept(2, 65536));

  setup(); reset_session = false; change_session_id = true;
  handle_mcp_message_locked({g_runtime.pending_mcp_messages,
      g_runtime.external_tools, g_runtime.transport.session_id,
      g_runtime.last_error}, request.as<JsonVariant>(), lock);
  assert(called == 3);
  assert(g_runtime.pending_mcp_messages.pop() == std::string(32768, 'x'));
  assert(g_runtime.pending_mcp_messages.empty());

  setup(); change_session_id = false;
  assert(g_runtime.pending_mcp_messages.try_push(std::string(32768, 'a')));
  assert(g_runtime.pending_mcp_messages.try_push(std::string(32768, 'b')));
  handle_mcp_message_locked({g_runtime.pending_mcp_messages,
      g_runtime.external_tools, g_runtime.transport.session_id,
      g_runtime.last_error}, request.as<JsonVariant>(), lock);
  assert(called == 3); // No capacity: no side effects.

  // A second conversation uses only its supplied context, not the first one's
  // full response queue or registry. No fake session/transport implementation.
  TestContext other;
  other.transport.session_id = "independent";
  JsonDocument ping;
  ping["method"] = "ping"; ping["id"] = 12;
  std::unique_lock<std::mutex> other_lock(other.mutex);
  handle_mcp_message_locked({other.pending_mcp_messages, other.external_tools,
      other.transport.session_id, other.last_error}, ping.as<JsonVariant>(), other_lock);
  assert(deserializeJson(response, other.pending_mcp_messages.pop()) == DeserializationError::Ok);
  assert(response["session_id"] == "independent");
  assert(response["payload"]["id"] == 12);
  std::cout << "MCP callbacks release runtime lock, reserve results and discard old-session replies\n";
}
