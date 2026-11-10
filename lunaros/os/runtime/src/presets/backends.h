#pragma once
#include <string>
namespace lunaredu::lunaros {
struct RuntimePreset;
bool configure_execution_backends(const RuntimePreset &,std::string &error);
}
