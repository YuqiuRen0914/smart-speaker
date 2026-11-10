#pragma once
#include "app/execution/application_backend.h"
namespace lunaredu::lunaros::apps {
const ApplicationArtifactProfile *application_artifact_profile(const std::string &id);
struct ApplicationExecutionCapabilities {
  bool wasm = false, micropython = false, aot = false;
  uint32_t abi = 1, memory_pages = kApplicationMemoryPages, maximum_package_bytes = kMaximumPackageBytes;
};
// No VM/task allocation. AOT requires the opt-in build, executable PSRAM mapping
// and configured publisher trust; compiler metadata cannot enable it.
ApplicationExecutionCapabilities application_execution_capabilities();
std::unique_ptr<ApplicationSession> create_application_session(
    const std::string &id, KernelRuntime &);
}
