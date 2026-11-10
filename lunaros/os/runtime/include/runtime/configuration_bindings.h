#pragma once
#include <string>
namespace lunaredu::lunaros {
// Boot-lifetime preferences provider. Binding never reads or writes storage.
bool bind_system_configuration(std::string &error);
}
