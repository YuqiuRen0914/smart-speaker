#include "lunaros_app_session.h"
#include "application_abi_contract.h"
#include "app/execution/execution_engine_lease.h"
#include "app/package/application_profile.h"
#include "wasm_export.h"
#include "lunaros_wamr.h"
#include "lunaros_aot_budget.h"
#include <atomic>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
namespace lunaredu::lunaros::apps {
namespace {
void *allocate(unsigned int size) {
  return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
void *reallocate(void *p, unsigned int size) {
  return heap_caps_realloc(p, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
void deallocate(void *p) { heap_caps_free(p); }
std::atomic<bool> engine_ready{false};
} // namespace
bool session_backend_available() { return true; }
bool session_aot_available() { return lunaros_wamr_aot_available(); }
struct Session::Impl {
  ExecutionEngineLease engine_lease;
  bool owned = false, initialized = false;
  bool aot = false, trapped = false;
  bool closing=false,stop_attempted=false;
  SessionDeviceScope devices;
  bool validation_only=false;
  LunarOSAotBudget budget{};
  std::vector<uint8_t> bytes;
  wasm_module_t module = nullptr;
  wasm_module_inst_t instance = nullptr;
  wasm_exec_env_t env = nullptr;
  wasm_function_inst_t init = nullptr, input = nullptr, stop = nullptr, save = nullptr, validate = nullptr;
  SessionStorage storage;
  std::shared_ptr<const DrawList> latest;
  std::shared_ptr<const DrawList> pending;
  unsigned submissions = 0, reads = 0, writes = 0, logs = 0;
  bool import_budget_exceeded = false;
  void observe(SessionObservationPhase phase, const char *operation, const char *code = nullptr) const {
    if (storage.observe) storage.observe(storage.observation_context, {phase, operation, code});
  }
  bool credit(unsigned &counter, unsigned maximum) {
    if (++counter <= maximum)
      return true;
    import_budget_exceeded = true;
    return false;
  }
  static Impl *owner(wasm_exec_env_t e) {
    return static_cast<Impl *>(wasm_runtime_get_user_data(e));
  }
  static uint64_t now_us() { return uint64_t(esp_timer_get_time()); }
  static bool cancelled(void *context) {
    auto &s = *static_cast<Impl *>(context);
    return s.storage.cancelled && s.storage.cancelled(s.storage.cancel_context);
  }
  static int32_t poll(wasm_exec_env_t e) {
    auto *s = owner(e);
    return s ? lunaros_aot_budget_poll(&s->budget) : 1;
  }
  static uint8_t *pointer(wasm_exec_env_t e, uint32_t at, uint32_t n) {
    auto inst = wasm_runtime_get_module_inst(e);
    return wasm_runtime_validate_app_addr(inst, at, n)
               ? static_cast<uint8_t *>(
                     wasm_runtime_addr_app_to_native(inst, at))
               : nullptr;
  }
  static int32_t submit(wasm_exec_env_t e, uint32_t at, uint32_t n) {
    auto *self = owner(e);
    if (!self) return -1;
    self->observe(SessionObservationPhase::HostStarted, "wasm.graphics_submit");
    const auto failed = [&] (const char *code) {
      self->observe(SessionObservationPhase::HostFailed, "wasm.graphics_submit", code); return -1;
    };
    if (!self->credit(self->submissions, 2)) return failed("host_call_budget_exceeded");
    auto *p = n <= 32768 ? pointer(e, at, n) : nullptr;
    if (!p) return failed("invalid_guest_buffer");
    auto draw = std::make_shared<DrawList>();
    if (!decode_draw_list(p, n, *draw, {kApplicationBudget.draw_bytes, kApplicationBudget.draw_commands, kApplicationPlatform.canvas_width, kApplicationPlatform.canvas_height})) return failed("invalid_draw_list");
    self->pending = std::move(draw);
    self->observe(SessionObservationPhase::HostReturned, "wasm.graphics_submit");
    return 0;
  }
  static int32_t read(wasm_exec_env_t e, uint32_t at, uint32_t n) {
    auto *self = owner(e);
    if (!self) return -1;
    self->observe(SessionObservationPhase::HostStarted, "wasm.storage_read");
    if (!self->credit(self->reads, 4)) {
      self->observe(SessionObservationPhase::HostFailed, "wasm.storage_read", "host_call_budget_exceeded"); return -1;
    }
    auto *p = n <= 8192 ? pointer(e, at, n) : nullptr;
    const int32_t result = p && self->storage.read
               ? self->storage.read(self->storage.context, p, n)
               : -1;
    self->observe(result >= 0 ? SessionObservationPhase::HostReturned : SessionObservationPhase::HostFailed,
        "wasm.storage_read", result >= 0 ? nullptr : !p ? "invalid_guest_buffer" : "storage_read_failed");
    return result;
  }
  static int32_t write(wasm_exec_env_t e, uint32_t at, uint32_t n) {
    auto *self = owner(e);
    if (!self) return -1;
    self->observe(SessionObservationPhase::HostStarted, "wasm.storage_write");
    if (!self->credit(self->writes, 1)) {
      self->observe(SessionObservationPhase::HostFailed, "wasm.storage_write", "host_call_budget_exceeded"); return -1;
    }
    auto *p = n <= 8192 ? pointer(e, at, n) : nullptr;
    const int32_t result = p && self->storage.write &&
                   self->storage.write(self->storage.context, p, n)
               ? int32_t(n)
               : -1;
    self->observe(result >= 0 ? SessionObservationPhase::HostReturned : SessionObservationPhase::HostFailed,
        "wasm.storage_write", result >= 0 ? nullptr : !p ? "invalid_guest_buffer" : "storage_write_failed");
    return result;
  }
  static int32_t log(wasm_exec_env_t e, uint32_t at, uint32_t n) {
    auto *self = owner(e);
    if (!self) return -1;
    self->observe(SessionObservationPhase::HostStarted, "wasm.log_write");
    if (!self->credit(self->logs, 4)) {
      self->observe(SessionObservationPhase::HostFailed, "wasm.log_write", "host_call_budget_exceeded"); return -1;
    }
    auto *p = n <= 256 ? pointer(e, at, n) : nullptr;
    if (!p) {
      self->observe(SessionObservationPhase::HostFailed, "wasm.log_write", "invalid_guest_buffer"); return -1;
    }
    ESP_LOGI("lunaros_app", "%.*s", int(n), reinterpret_cast<const char *>(p));
    self->observe(SessionObservationPhase::HostReturned, "wasm.log_write");
    return int32_t(n);
  }
  static int32_t device(wasm_exec_env_t env,int32_t op,int32_t a,int32_t b,int32_t c){
    auto *s=owner(env);if(!s)return LUNA_DEVICE_DENIED;
    s->observe(SessionObservationPhase::HostStarted,"wasm.device_call");
    const int32_t result=s->devices.call({op,a,b,c},cancelled(s));
    s->observe(LUNA_DEVICE_VALUE_VALID(result)?SessionObservationPhase::HostReturned:SessionObservationPhase::HostFailed,
      "wasm.device_call",LUNA_DEVICE_VALUE_VALID(result)?nullptr:"device_call_rejected");
    return result;
  }
  void revoke_devices(){
    devices.revoke();
  }
  bool call(wasm_function_inst_t f, uint32_t argc, uint32_t *args,
            std::string &error, const char *operation, bool nonnegative_result = false) {
    observe(SessionObservationPhase::CallStarted, operation);
    pending.reset();
    submissions = reads = writes = logs = 0;
    devices.begin_callback(!closing&&!validation_only&&
      (!std::strcmp(operation,"wasm.luna_init")||!std::strcmp(operation,"wasm.luna_event")));
    import_budget_exceeded = false;
    lunaros_aot_budget_begin(&budget, 20000, 200000);
    wasm_runtime_set_instruction_count_limit(env, 2000000);
    if (!wasm_runtime_call_wasm(env, f, argc, args)) {
      const char *message = wasm_runtime_get_exception(instance);
      error = message ? message : "application trapped";
      trapped = true;
      revoke_devices();
      observe(SessionObservationPhase::CallFailed, operation, "runtime_trap");
      return false;
    }
    devices.end_callback();
    if (import_budget_exceeded||devices.budget_exhausted()) {
      error = "application host-call budget exceeded";
      trapped = true;
      revoke_devices();
      observe(SessionObservationPhase::CallFailed, operation, "host_call_budget_exceeded");
      return false;
    }
    if (pending)
      latest = std::move(pending);
    const bool accepted = nonnegative_result ? int32_t(args[0]) >= 0 : int32_t(args[0]) == 0;
    observe(accepted ? SessionObservationPhase::CallReturned : SessionObservationPhase::CallFailed,
        operation, accepted ? nullptr : "guest_rejected");
    return true;
  }
};
Session::Session() : impl_(new Impl) {}
Session::~Session() {std::string error;if(!close(error))(void)impl_.release();}
bool Session::open(const uint8_t *p, size_t n, SessionStorage storage,
                   uint32_t seed, std::string &error, bool aot) {
  if(!close(error))return false;
  impl_->closing=impl_->stop_attempted=false;
  impl_->storage = storage;
  if(!impl_->devices.bind(storage.device,storage.validation_only,error))return false;
  impl_->validation_only=storage.validation_only;
  impl_->observe(SessionObservationPhase::Opening, "wasm.session.open");
  const auto began = esp_timer_get_time();
  bool uses_device=false;
  if (aot ? (!session_aot_available() || !validate_aot_header(p,n)) : !validate_module(p, n,4,&uses_device)) {
    error = "unsupported application module";
    impl_->observe(SessionObservationPhase::OpenFailed, "wasm.session.open", "unsupported_module");
    return false;
  }
  if(uses_device&&!storage.device.permitted&&!storage.validation_only){error="application_device_permission_required";return false;}
  if (!impl_->engine_lease.acquire(ExecutionEngine::Wasm)) {
    error = execution_engine_conflict();
    impl_->observe(SessionObservationPhase::OpenFailed, "wasm.session.open", "execution_engine_busy");
    return false;
  }
  impl_->owned = true;
  if (!engine_ready) {
#define LUNA_NATIVE(name,method,signature,parameters,device,args) {#name,reinterpret_cast<void *>(Impl::method),signature,nullptr},
    static NativeSymbol symbols[]{LUNAROS_APP_ABI_IMPORTS(LUNA_NATIVE)};
#undef LUNA_NATIVE
    static_assert(sizeof(symbols)/sizeof(symbols[0])==abi::import_count);
    RuntimeInitArgs args{};
    args.mem_alloc_type = Alloc_With_Allocator;
    args.mem_alloc_option.allocator.malloc_func =
        reinterpret_cast<void *>(allocate);
    args.mem_alloc_option.allocator.realloc_func =
        reinterpret_cast<void *>(reallocate);
    args.mem_alloc_option.allocator.free_func =
        reinterpret_cast<void *>(deallocate);
    args.native_module_name = LUNAROS_APP_ABI_MODULE;
    args.native_symbols = symbols;
    args.n_native_symbols = sizeof(symbols)/sizeof(symbols[0]);
    if (!wasm_runtime_full_init(&args)) {
      error = "VM initialization failed";
      impl_->observe(SessionObservationPhase::OpenFailed, "wasm.session.open", "vm_initialization_failed");
      close();
      return false;
    }
    static NativeSymbol internal[] = {
      {"poll", reinterpret_cast<void *>(Impl::poll), "()i", nullptr}};
    if (!wasm_runtime_register_natives("__lunaros_aot_v1", internal, 1)) {
      wasm_runtime_destroy();
      error = "AOT budget registration failed";
      impl_->observe(SessionObservationPhase::OpenFailed, "wasm.session.open", "budget_registration_failed");
      close();
      return false;
    }
    engine_ready = true;
  }
  auto &s = *impl_;
  s.owned = true;
  s.storage = storage;
  s.aot = aot;
  s.trapped = false;
  s.budget.now_us = Impl::now_us;
  s.budget.cancelled = Impl::cancelled;
  s.budget.context = &s;
  s.bytes.assign(p, p + n);
  char detail[160]{};
  s.module =
      wasm_runtime_load(s.bytes.data(), uint32_t(n), detail, sizeof(detail));
#define LUNA_ENGINE_IMPORT(name,method,signature,parameters,device,args) {#name,parameters,bool(device)},
  static constexpr LunarWamrImport imports[]{LUNAROS_APP_ABI_IMPORTS(LUNA_ENGINE_IMPORT)};
#undef LUNA_ENGINE_IMPORT
#define LUNA_ENGINE_EXPORT(name,kind,parameters,required) {#name,kind,parameters,bool(required)},
  static constexpr LunarWamrExport exports[]{LUNAROS_APP_ABI_EXPORTS(LUNA_ENGINE_EXPORT)};
#undef LUNA_ENGINE_EXPORT
  static constexpr LunarWamrContract contract{LUNAROS_APP_ABI_MODULE,imports,abi::import_count,exports,abi::export_count};
  if(s.module&&(!lunaros_wamr_module_admit(s.module,&contract,aot,&uses_device)||
      (uses_device&&!storage.device.permitted&&!storage.validation_only))){
    error="application imports, exports or permissions are incompatible";
    s.observe(SessionObservationPhase::OpenFailed,"wasm.session.open","module_contract_rejected");
    close();return false;
  }
  if (s.module && aot && !lunaros_wamr_aot_admit(s.module)) {
    error = "AOT lifecycle, memory or imports are incompatible";
    s.observe(SessionObservationPhase::OpenFailed, "wasm.session.open", "aot_admission_failed");
    close();
    return false;
  }
  if (s.module) {
    InstantiationArgs args{};
    args.default_stack_size = 16384;
    s.instance =
        wasm_runtime_instantiate_ex(s.module, &args, detail, sizeof(detail));
  }
  if (s.instance) {
    s.env = wasm_runtime_create_exec_env(s.instance, 16384);
    s.init = wasm_runtime_lookup_function(s.instance, "luna_init");
    s.input = wasm_runtime_lookup_function(s.instance, "luna_event");
    s.stop = wasm_runtime_lookup_function(s.instance, "luna_stop");
    s.save = wasm_runtime_lookup_function(s.instance, "luna_save");
    s.validate = wasm_runtime_lookup_function(s.instance, "luna_validate_save");
  }
  // Public module metadata already checked all required/optional signatures
  // against the canonical ABI before any instantiation callback could run.
  if (!s.env || !s.init || !s.input || !s.stop) {
    error = *detail ? detail : "invalid application exports";
    s.observe(SessionObservationPhase::OpenFailed, "wasm.session.open", "instantiation_or_exports_invalid");
    close();
    return false;
  }
  wasm_runtime_set_user_data(s.env, &s);
  if(s.validation_only){s.observe(SessionObservationPhase::Opened,"wasm.session.validate");return true;}
  uint32_t args[]{240, 240, seed};
  if (!s.call(s.init, 3, args, error, "wasm.luna_init") || int32_t(args[0]) != 0 || !s.latest) {
    if (error.empty())
      error = "application initialization failed";
    s.observe(SessionObservationPhase::OpenFailed, "wasm.session.open", "initialization_failed");
    close();
    return false;
  }
  s.initialized = true;
  s.observe(SessionObservationPhase::Opened, "wasm.session.open");
  ESP_LOGI("lunaros_app",
           "session opened wasm_bytes=%u init_us=%u commands=%u psram_free=%u",
           unsigned(n), unsigned(esp_timer_get_time() - began),
           unsigned(s.latest->size()),
           unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
  return true;
}
bool Session::event(int kind, int a, int b, int c, std::string &error) {
  if (!active())
    return false;
  uint32_t args[]{uint32_t(kind), uint32_t(a), uint32_t(b), uint32_t(c)};
  if (!impl_->call(impl_->input, 4, args, error, "wasm.luna_event", true) || int32_t(args[0])<0) {
    if(error.empty())error="application rejected input or rendering";
    close();
    return false;
  }
  return true;
}
bool Session::checkpoint(std::string &error) {
  if(!active() || !impl_->save) return true; // Old modules remain exit-save only.
  uint32_t result[1]{};
  if(!impl_->call(impl_->save,0,result,error,"wasm.luna_save")) { close(); return false; }
  if(int32_t(result[0])!=0) { error="存档写入失败"; return false; }
  return true;
}
bool Session::validate_save(SessionStorage candidate,std::string &error) {
  auto &s=*impl_;
  if(!active() || !s.validate) { error="应用不支持存档校验"; return false; }
  const auto original=s.storage;s.storage=session_validation_storage(original,candidate);
  uint32_t result[1]{};bool ok=s.call(s.validate,0,result,error,"wasm.luna_validate_save") && int32_t(result[0])==0;
  s.storage=original;if(!ok && error.empty())error="存档内容不适用于该应用";
  if(s.trapped)close();
  return ok;
}
void Session::close() {
  std::string error;(void)close(error);
}
bool Session::close(std::string &error) {
  auto &s = *impl_;
  s.closing=true;s.revoke_devices();
  const bool observed = s.owned || s.env || s.instance || s.module;
  if (observed) s.observe(SessionObservationPhase::Closing, "wasm.session.close");
  if (s.env && s.stop && s.initialized && !s.trapped&&!s.stop_attempted) {
    s.stop_attempted=true;
    uint32_t args[1]{};
    std::string ignored;
    (void)s.call(s.stop, 0, args, ignored, "wasm.luna_stop");
  }
  if(!s.devices.retire(error))return false;
  if (s.env)
    wasm_runtime_destroy_exec_env(s.env);
  if (s.instance)
    wasm_runtime_deinstantiate(s.instance);
  if (s.module)
    wasm_runtime_unload(s.module);
  s.env = nullptr;
  s.instance = nullptr;
  s.module = nullptr;
  s.init = nullptr;
  s.input = nullptr;
  s.stop = nullptr;
  s.save = s.validate = nullptr;
  s.bytes.clear();
  s.bytes.shrink_to_fit();
  s.latest.reset();
  s.pending.reset();
  s.initialized = false;
  if (s.owned) {
    // A single foreground session owns the engine as well as its instance.
    // Keep the ownership gate closed until global runtime allocations are gone.
    if (engine_ready.exchange(false))
      wasm_runtime_destroy();
    s.owned = false;
    s.engine_lease.release();
  }
  if (observed) s.observe(SessionObservationPhase::Closed, "wasm.session.close");
  s.storage = {};
  error.clear();return true;
}
bool Session::active() const { return impl_->env != nullptr&&!impl_->closing&&!impl_->trapped; }
std::shared_ptr<const DrawList> Session::frame() const { return impl_->latest; }
} // namespace lunaredu::lunaros::apps
