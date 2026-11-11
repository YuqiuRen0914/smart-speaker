#include "interaction/conversation/assistant_endpoint_policy.h"

#include <cassert>
#include <string>

using namespace lunaredu::pet::conversation::internal;
namespace {
bool persist_ok = true;
int persist_calls = 0;
bool persist(const char *key, const std::string &value) {
  assert(std::string(key) == "ll_url");
  (void)value;
  ++persist_calls;
  return persist_ok;
}
}
int main() {
  assert(valid_activation_endpoint(""));
  assert(valid_activation_endpoint("https://gateway.lunaredu.cn/wire/v1/device/bootstrap"));
  assert(valid_activation_endpoint("https://assistant.lan:8443/ota"));
  for (const auto *url : {"http://assistant.example/ota", "https:///missing-host",
       "https://user:pass@example.com/ota", "https://example.com/?token=x",
       "https://example.com/#fragment", "https://example.com:0/ota",
       "https://example.com:65536/ota", "https://example.com/../private",
       "https://example.com/ota/activate",
       "https://example.com/path with spaces"}) assert(!valid_activation_endpoint(url));
  std::string current;
  persist_calls = 0; persist_ok = false;
  assert(!commit_activation_endpoint("https://custom.example/ota", false, false, current, persist));
  assert(current.empty() && persist_calls == 1);
  persist_ok = true;
  assert(!commit_activation_endpoint("https://custom.example/ota", true, false, current, persist));
  assert(current.empty() && persist_calls == 1);
  assert(!commit_activation_endpoint("https://custom.example/ota", false, true, current, persist));
  assert(current.empty() && persist_calls == 1);
  assert(commit_activation_endpoint("https://custom.example/ota", false, false, current, persist));
  assert(current == "https://custom.example/ota" && persist_calls == 2);
  assert(commit_activation_endpoint("https://custom.example/ota", false, false, current, persist));
  assert(persist_calls == 2);
  assert(commit_activation_endpoint("", false, false, current, persist));
  assert(current.empty() && persist_calls == 3);
}
