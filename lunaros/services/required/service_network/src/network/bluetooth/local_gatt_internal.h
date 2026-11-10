#pragma once
#include <string>
namespace lunaredu::lunaros {
// Detach callbacks only after the radio and the old session have retired.
// The static Bluetooth profile registration survives Runtime recomposition.
bool reset_network_local_gatt_binding(std::string &error);
}
