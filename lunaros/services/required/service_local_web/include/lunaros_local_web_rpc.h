#pragma once
#include <cstddef>
#include <string>
#include <ArduinoJson.h>
#include "memory/json_schema_workspace.h"
namespace lunaredu::lunaros {
struct LocalRpcRequest {
  const char *body = nullptr;
  size_t bytes = 0;
  std::string protocol, method, name;
  void *header_context = nullptr;
  std::string (*read_header)(void *, const char *) = nullptr;
  void *authority_context = nullptr;
  bool (*authorized)(void *) = nullptr;
  // Synchronous transport loan; protocol documents must return before invoke.
  ArduinoJson::Allocator *document_allocator=nullptr;
  lunaredu::lunaros::JsonSchemaWorkspace *schema_workspace=nullptr;
};
// Account-bound, authenticated JSON-RPC endpoint at /mcp. No unauthenticated
// onboarding access; bearer grants use the existing owner binding verifier.
// Configure before HTTP starts, callbacks and context have boot lifetime.
bool configure_local_rpc(void *context,
    int (*invoke)(void *, const LocalRpcRequest &, std::string &), std::string &error);
}
