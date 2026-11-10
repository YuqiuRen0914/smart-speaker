#pragma once
#include <cstdint>
#include "lunaros_network_jobs.h"
#include "lunaros_network_policy.h"
namespace lunaredu::lunaros {
class KernelRuntime;
class DeviceCapabilityRegistry;
struct DeviceCapabilityDescriptor;
struct ServiceNode;
struct DeviceApiContext;
// Public lifecycle API, included explicitly beside the observation contract.
bool configure_network_runtime(KernelRuntime &, const NetworkPolicy &,
    const char *timezone, std::string &error);
// Optional profile assembly; configure after configure_network_runtime and before start.
bool configure_network_bluetooth_profiles(KernelRuntime &, const char *name, std::string &error);
ServiceNode network_service_node(DeviceApiContext &device);
void poll_network_on_owner();
// Call only on the network I/O executor. One job per iteration keeps queued
// service operations responsive even when producers continuously submit work.
void set_network_job_wake_handler(NetworkJobWakeHandler handler,
                                  void *context);
bool run_next_network_job(uint32_t now_ms);
uint32_t next_network_job_wake_delay_ms(uint32_t now_ms);
bool network_job_runner_context_active();

bool network_runtime_owned_by(const KernelRuntime &runtime);

const DeviceCapabilityDescriptor &network_service_capability_descriptor();

bool set_network_capability_available(DeviceCapabilityRegistry &capabilities, bool available, std::string &error);
}
