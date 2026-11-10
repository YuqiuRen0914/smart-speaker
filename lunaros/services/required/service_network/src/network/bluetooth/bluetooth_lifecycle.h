#pragma once
#include "lunaros_network_bluetooth.h"

// Network implementation owner only. Clients use the queued control API and
// copied snapshots, without starting/stopping or polling the SDK host.
namespace lunaredu::lunaros {
bool start_bluetooth(std::string &error);
// Owner-only retirement step. False retains ownership and denies a restart;
// poll continues pending NimBLE SDK work through the existing I/O executor.
// No new thread is created. Callbacks still run on their documented owners.
bool stop_bluetooth(std::string &error);
bool bluetooth_synchronized();
bool bluetooth_running();
// Network owner only: controller/host ownership, including pending cleanup.
bool bluetooth_occupied();
} // namespace lunaredu::lunaros

namespace lunaredu::lunaros {
void poll_bluetooth_profiles();
bool bluetooth_connected();
}
