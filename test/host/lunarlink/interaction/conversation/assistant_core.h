#pragma once
#include "interaction/transport/lunarlink_protocol.h"
#include "interaction/tools/assistant_mcp_queue.h"
#include <mutex>
#include <vector>
namespace lunaredu::pet::conversation::internal {
using ToolCallHandler=bool(*)(void*,const char*,std::string&,std::string&);
struct HostIdentity {int epoch=1;bool operator==(const HostIdentity &)const=default;};
struct Tool {std::string name,description,input_schema_json;ToolCallHandler handler=nullptr;void *context=nullptr;};
struct HostRuntime {
 std::mutex mutex;
 struct {LunarLinkProtocol wire;std::string session_id="ses_test";}transport;
 std::vector<Tool> external_tools;
 PendingMcpMessages pending_mcp_messages;
 struct {std::string last_error;}diagnostics;
 uint32_t platform_generation=1;
 HostIdentity platform_identity;
};
extern HostRuntime g_runtime;
HostIdentity lunarlink_identity();
}
