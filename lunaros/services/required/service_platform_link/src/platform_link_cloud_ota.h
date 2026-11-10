#pragma once
#include <ArduinoJson.h>
#include "lunaredu_ota.h"
namespace lunaredu::lunaros::platform_link {
class AutomaticOtaRetryGuard;
bool cloud_role_retry_repair_allowed(const OtaAvailableUpdate &);
bool reserve_cloud_role_retry(AutomaticOtaRetryGuard &,const OtaAvailableUpdate &,
    const std::string &key,int64_t epoch,uint32_t now);
void fill_cloud_role_state(JsonObject state);
bool decode_cloud_role_offer(JsonVariantConst offer,OtaAvailableUpdate &,std::string &error);
// True means the exact image is installed; never rewrite it, even if receipt persistence failed.
bool cloud_role_observe_installed(const OtaAvailableUpdate &);
bool cloud_role_offer_admitted(const OtaAvailableUpdate &);
bool prepare_cloud_role_install(const OtaAvailableUpdate &,std::string &error);
bool cloud_role_handoff_resume(const OtaAvailableUpdate &);
void cloud_role_install_rejected(const std::string &error);
bool clear_cloud_role_intent();
}
