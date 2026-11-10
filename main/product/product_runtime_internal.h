#pragma once

#include <string>
namespace lunaredu::lunaros { class ProductRuntime; }

namespace smart_speaker::product::runtime_internal {

bool register_services(lunaredu::lunaros::ProductRuntime &runtime, std::string &error);
bool start_executor_tasks(lunaredu::lunaros::ProductRuntime &runtime, std::string &error);

}
