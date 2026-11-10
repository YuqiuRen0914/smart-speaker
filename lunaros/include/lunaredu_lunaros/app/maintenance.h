#pragma once

#include <array>
#include "lunaredu_lunaros/network/network.h"
#include "lunaredu_lunaros/kernel/product_definition.h"

namespace lunaredu::lunaros {
// Explicit composition helpers for applications with one foreground loop.
// The caller owns/pumps main and network; only blocking downloads get a worker.
bool prepare_maintenance_runtime(NetworkPolicy policy, std::string &error);
std::array<ServiceNode, 7> maintenance_service_nodes();
bool declare_maintenance_projections(void *, std::string &error);
// Idempotent once ready. A wait timeout leaves the admitted startup in progress;
// retry joins that attempt. Creation/bind failure permits a fresh admission.
bool start_maintenance_io(std::string &error);
void poll_maintenance_runtime(bool application_ready = true);
// Existing multi-executor products call this from their main owner instead.
void tick_maintenance_boot_confirmation(bool application_ready);
}
