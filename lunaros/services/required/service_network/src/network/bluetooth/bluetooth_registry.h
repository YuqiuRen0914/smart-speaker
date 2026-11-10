#pragma once
#include "network/bluetooth/bluetooth_lifecycle.h"
#include <array>
#include <atomic>
namespace lunaredu::lunaros::bluetooth_internal {
// Only Network owner mutates the registry, before its first successful start.
// Callbacks borrow immutable registrations after that admission point.
inline std::array<BluetoothProfile,4> profiles{};
inline size_t count=0;
inline bool sealed=false;
inline std::atomic<bool> running{false}, synchronized{false};
}
