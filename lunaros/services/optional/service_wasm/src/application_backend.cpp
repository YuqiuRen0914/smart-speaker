#include "lunaros_wasm_app_backend.h"
namespace lunaredu::lunaros::apps {
namespace {
class WasmSession final : public ApplicationSession {
  Session session_;
  bool aot_;
public:
  explicit WasmSession(bool aot) : aot_(aot) {}
  bool open(const uint8_t *p, size_t n, SessionStorage s, uint32_t seed,
            std::string &error) override { return session_.open(p,n,s,seed,error,aot_); }
  bool event(int k,int a,int b,int c,std::string &e) override { return session_.event(k,a,b,c,e); }
  bool checkpoint(std::string &e) override { return session_.checkpoint(e); }
  bool validate_save(SessionStorage s,std::string &e) override { return session_.validate_save(s,e); }
  bool close(std::string &error) override { return session_.close(error); }
  bool active() const override { return session_.active(); }
  std::shared_ptr<const DrawList> frame() const override { return session_.frame(); }
};
}
std::unique_ptr<ApplicationSession> create_wasm_application_session(KernelRuntime &) {
  return std::make_unique<WasmSession>(false);
}
std::unique_ptr<ApplicationSession> create_aot_application_session(KernelRuntime &) {
  return std::make_unique<WasmSession>(true);
}
bool validate_wasm_application_entry(const uint8_t *bytes, size_t size) {
  return validate_module(bytes, size);
}
}
