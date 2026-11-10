#pragma once
#include "lunaros_app_session.h"
#include "app/execution/application_backend.h"
namespace lunaredu::lunaros { class KernelRuntime; }
namespace lunaredu::lunaros::apps {
std::unique_ptr<ApplicationSession> create_wasm_application_session(KernelRuntime &);
std::unique_ptr<ApplicationSession> create_aot_application_session(KernelRuntime &);
const ApplicationArtifactProfile &wasm_aot_artifact_profile();
bool validate_wasm_application_entry(const uint8_t *, size_t);
// Registration publishes validators and lazy factories without allocating a VM.
bool register_wasm_application_backends(std::string &error);
}
