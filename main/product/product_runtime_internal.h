#pragma once

#include <string>

namespace smart_speaker::product::runtime_internal {

bool register_services(std::string &error);
bool start_executor_tasks(std::string &error);

}
