#include "interaction/tools/assistant_mcp.h"
#include "interaction/tools/assistant_tool_execution.h"

#include <esp_log.h>

namespace lunaredu::pet::conversation::internal {

namespace {

constexpr const char *kDebugTag = "assistant_mcp";

void queue_mcp_locked(McpContext context, std::string body) {
  if (body.empty() || !context.responses.try_push(std::move(body))) {
    context.last_error = "mcp_response_queue_full_or_oversized";
    ESP_LOGW(kDebugTag, "MCP response rejected by bounded queue");
  }
}

const lunaredu::pet::conversation::ToolRegistration *find_external_tool_locked(
    McpContext context, const std::string &name) {
  for (const auto &tool : context.tools) {
    if (tool.name == name) {
      return &tool;
    }
  }
  return nullptr;
}

void add_external_tool(JsonArray tools,
                       const lunaredu::pet::conversation::ToolRegistration &registered) {
  JsonObject tool = tools.add<JsonObject>();
  tool["name"] = registered.name;
  tool["description"] = registered.description;
  JsonDocument schema;
  if (deserializeJson(schema, registered.input_schema_json) ==
      DeserializationError::Ok) {
    tool["inputSchema"] = schema.as<JsonVariantConst>();
  } else {
    JsonObject input_schema = tool["inputSchema"].to<JsonObject>();
    input_schema["type"] = "object";
    input_schema["additionalProperties"] = false;
    input_schema["properties"].to<JsonObject>();
  }
}

} // namespace

static bool build_mcp_response_locked(McpContext context, JsonDocument &payload,
                               std::string &body) {
  JsonDocument envelope;
  envelope["session_id"] = context.session_id;
  envelope["type"] = "mcp";
  envelope["payload"] = payload.as<JsonVariantConst>();
  body.clear();
  if (measureJson(envelope) > PendingMcpMessages::kMaximumMessageBytes) {
    JsonObject response = envelope["payload"].as<JsonObject>();
    response.remove("result");
    response["error"]["code"] = -32001;
    response["error"]["message"] = "Response exceeds device limit";
    if (measureJson(envelope) > PendingMcpMessages::kMaximumMessageBytes)
      return false;
  }
  serializeJson(envelope, body);
  return !body.empty();
}

static bool build_mcp_notification_locked(McpContext context, JsonDocument &payload, std::string &body) {
  JsonDocument envelope;
  envelope["session_id"] = context.session_id;
  envelope["type"] = "mcp";
  envelope["payload"] = payload.as<JsonVariantConst>();
  serializeJson(envelope, body);
  return !body.empty();
}

void handle_mcp_message_locked(McpContext context, JsonVariant payload, std::unique_lock<std::mutex> &runtime_lock) {
  if (!payload.is<JsonObject>()) {
    return;
  }
  const std::string method = payload["method"] | "";
  JsonVariantConst id_val = payload["id"];

  // Check response admission before any side effect. Cross-application tools
  // additionally reserve this room while releasing the runtime mutex below.
  const bool initializing = method == "initialize";
  if (!id_val.isNull() && !context.responses.can_accept(
          initializing ? 2 : 1,
          PendingMcpMessages::kMaximumMessageBytes + (initializing ? 1024 : 0))) {
    JsonDocument response;
    response["jsonrpc"] = "2.0";
    response["id"] = id_val;
    response["error"]["code"] = -32000;
    response["error"]["message"] = "Device response queue busy; retry later";
    std::string body;
    build_mcp_response_locked(context, response, body);
    queue_mcp_locked(context, std::move(body));
    return;
  }

  if (method == "initialize" && !id_val.isNull()) {
    ESP_LOGD(kDebugTag,
             "[DEBUG-MCP-LINK] initialize request id=%s external_tools=%u",
             id_val.as<std::string>().c_str(),
             static_cast<unsigned>(context.tools.size()));
    std::string response_body;
    std::string notification_body;
    {
      JsonDocument response;
      JsonObject root = response.to<JsonObject>();
      root["jsonrpc"] = "2.0";
      root["id"] = id_val;
      JsonObject result = root["result"].to<JsonObject>();
      result["protocolVersion"] = "2024-11-05";
      result["capabilities"]["tools"].to<JsonObject>();
      JsonObject server_info = result["serverInfo"].to<JsonObject>();
      server_info["name"] = "pc-xiaozhi-runtime";
      server_info["version"] = "0.1.0";
      build_mcp_response_locked(context, response, response_body);
    }
    {
      JsonDocument notification;
      JsonObject notify = notification.to<JsonObject>();
      notify["jsonrpc"] = "2.0";
      notify["method"] = "notifications/initialized";
      build_mcp_notification_locked(context, notification, notification_body);
    }
    if (response_body.empty()) return;
    queue_mcp_locked(context, std::move(response_body));
    queue_mcp_locked(context, std::move(notification_body));
    return;
  }
  if (method == "ping" && !id_val.isNull()) {
    JsonDocument response;
    JsonObject root = response.to<JsonObject>();
    root["jsonrpc"] = "2.0";
    root["id"] = id_val;
    root["result"].to<JsonObject>();
    std::string response_body;
    build_mcp_response_locked(context, response, response_body);
    queue_mcp_locked(context, std::move(response_body));
    return;
  }
  if (method == "tools/list" && !id_val.isNull()) {
    JsonDocument response;
    JsonObject root = response.to<JsonObject>();
    root["jsonrpc"] = "2.0";
    root["id"] = id_val;
    JsonArray tools = root["result"]["tools"].to<JsonArray>();
    for (const auto &tool : context.tools) {
      add_external_tool(tools, tool);
    }
#if CONFIG_LOG_MAXIMUM_LEVEL >= 4
    std::string tool_names;
    for (const auto &tool : context.tools) {
      if (!tool_names.empty()) {
        tool_names += ",";
      }
      tool_names += tool.name;
    }
    ESP_LOGD(kDebugTag,
             "[DEBUG-MCP-LINK] tools/list request id=%s count=%u names=%s",
             id_val.as<std::string>().c_str(),
             static_cast<unsigned>(context.tools.size()),
             tool_names.empty() ? "-" : tool_names.c_str());
#endif

    std::string response_body;
    build_mcp_response_locked(context, response, response_body);
    queue_mcp_locked(context, std::move(response_body));
    return;
  }
  if (method == "tools/call" && !id_val.isNull()) {
    const std::string name = payload["params"]["name"] | "";
    ESP_LOGD(kDebugTag,
             "[DEBUG-MCP-LINK] tools/call request id=%s name=%s",
             id_val.as<std::string>().c_str(), name.c_str());
    JsonDocument response;
    JsonObject root = response.to<JsonObject>();
    root["jsonrpc"] = "2.0";
    root["id"] = id_val;

    if (const auto *tool = find_external_tool_locked(context, name);
               tool != nullptr && tool->handler != nullptr) {
      std::string arguments_json = "{}";
      JsonVariantConst arguments = payload["params"]["arguments"];
      if (!arguments.isNull()) {
        arguments_json.clear();
        serializeJson(arguments, arguments_json);
      }
      std::string response_text;
      std::string error_text;
      bool ok = false;
      // No borrowed registry entry or rx_doc value may cross the unlock.
      const auto handler = tool->handler;
      void *const handler_context = tool->context;
      const std::string session_id = context.session_id;
      const auto disposition = execute_unlocked_tool(context.responses, runtime_lock, [&] {
        ok = handler(handler_context, arguments_json.c_str(), response_text, error_text);
      });
      if (disposition == ToolExecutionDisposition::SessionChanged ||
          session_id != context.session_id) return;
      if (disposition == ToolExecutionDisposition::QueueBusy) error_text = "mcp_response_queue_busy";
      ESP_LOGD(kDebugTag,
               "[DEBUG-MCP-LINK] tools/call handled name=%s ok=%d response=%s "
               "error=%s",
               name.c_str(), ok ? 1 : 0, response_text.c_str(),
               error_text.c_str());
      JsonObject result = root["result"].to<JsonObject>();
      JsonArray content = result["content"].to<JsonArray>();
      JsonObject content_item = content.add<JsonObject>();
      content_item["type"] = "text";
      content_item["text"] =
          ok ? response_text
             : (error_text.empty() ? "工具调用失败，请稍后重试" : error_text);
      result["isError"] = !ok;
    } else {
      ESP_LOGW(kDebugTag,
               "[DEBUG-MCP-LINK] tools/call unknown name=%s", name.c_str());
      root["error"]["code"] = -32601;
      root["error"]["message"] = "Tool not found";
    }

    std::string response_body;
    build_mcp_response_locked(context, response, response_body);
    ESP_LOGD(kDebugTag,
             "[DEBUG-MCP-LINK] tools/call response queued name=%s bytes=%u",
             name.c_str(), static_cast<unsigned>(response_body.size()));
    queue_mcp_locked(context, std::move(response_body));
    return;
  }
}

} // namespace lunaredu::pet::conversation::internal
