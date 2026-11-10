#include "lunaros_app_session.h"

namespace lunaredu::lunaros::apps {
bool session_backend_available() { return false; }
bool session_aot_available() { return false; }
struct Session::Impl {};
Session::Session() = default;
Session::~Session() = default;
bool Session::open(const uint8_t *, size_t, SessionStorage, uint32_t, std::string &error, bool) {
  error = "Wasm execution is unavailable on this target";
  return false;
}
bool Session::event(int, int, int, int, std::string &error) {
  error = "Wasm execution is unavailable on this target";
  return false;
}
bool Session::checkpoint(std::string &error) {
  error = "Wasm execution is unavailable on this target";
  return false;
}
bool Session::validate_save(SessionStorage, std::string &error) {
  error = "Wasm execution is unavailable on this target";
  return false;
}
void Session::close() {}
bool Session::close(std::string &error){error.clear();return true;}
bool Session::active() const { return false; }
std::shared_ptr<const DrawList> Session::frame() const { return {}; }
} // namespace lunaredu::lunaros::apps
