#pragma once
#include "persistence/application_artifact_json.h"
namespace lunaredu::lunaros::apps {
// Check v2 source/target binding before entry validation. AOT is admitted only
// by an explicitly enabled runtime; detached package signatures are separate.
bool validate_package_artifact(JsonVariantConst, uint32_t package_version,
                               const Application &, std::string &error, bool aot_available = false);
}
