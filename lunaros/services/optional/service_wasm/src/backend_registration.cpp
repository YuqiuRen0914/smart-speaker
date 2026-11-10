#include "lunaros_wasm_app_backend.h"

namespace lunaredu::lunaros::apps {
bool register_wasm_application_backends(std::string &error) {
  if (!register_application_backend("wasm", create_wasm_application_session,
          validate_wasm_application_entry, session_backend_available) ||
      !register_application_backend("aot", create_aot_application_session,
          validate_aot_header, session_aot_available, &wasm_aot_artifact_profile())) {
    error = "application_backend_registration_failed";
    return false;
  }
  error.clear();
  return true;
}
} // namespace lunaredu::lunaros::apps
