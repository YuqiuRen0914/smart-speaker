#pragma once
#include <cstddef>
#include <string>
namespace lunaredu::lunaros {
struct OtaInstallIntent {
  std::string url;
  std::string sha256;
  size_t size = 0;
};
// Dedicated recovery_state storage. No grant, private key or user data is
// copied. URLs may contain short-lived tickets and never enter status/logs.
bool ota_intent_stage(const OtaInstallIntent &, std::string &error);
bool ota_intent_begin_attempt(OtaInstallIntent &, std::string &error);
bool ota_intent_clear(std::string &error);
// Factory records explicit authorization before a newly staged pull can
// resume across reset. Crash-rescue phases never authorize an old intent.
bool ota_recovery_stage_install_resume(std::string &error);
void ota_intent_finish(const std::string &sha256, bool success);
void ota_service_tick_recovery_install();
}
