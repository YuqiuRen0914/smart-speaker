#include "lunaros_storage_service_id.h"
#include "lunaros_applications_service_id.h"
#include "applications_service_internal.h"
#include "app/application_installation.h"
#include "app/package/package_validation.h"
#include "app/package/package_trust.h"
#include "app/package/application_limits.h"
#include "lunaros_storage.h"
#include "kernel/runtime.h"
#include <algorithm>
#include <cstdlib>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
namespace lunaredu::lunaros::apps {
using namespace runtime_detail;
ApplicationsService::ApplicationsService() : impl_(new Impl) {}
ServiceNode ApplicationsService::node(KernelRuntime &runtime,
    const ServiceDependency *engines, size_t count) {
  auto &s = *impl_;
  const auto *device_provider=s.devices.provider_service_id();
  const size_t dependency_count=count+1+(device_provider?1:0);
  if ((s.runtime && s.runtime != &runtime) || (count && !engines) || count > SIZE_MAX-2) return {};
  // Declaration is immutable once installed. Re-declaring the same owner is
  // allowed only with the same lifetime edges (e.g. a rejected product retry).
  if (s.runtime) {
    if (s.dependency_count != dependency_count) return {};
    for (size_t i = 0; i < count; ++i)
      if (std::string(s.dependencies[i + 1].id) != (engines[i].id ? engines[i].id : "") ||
          s.dependencies[i + 1].required != engines[i].required) return {};
  } else {
    for (size_t i = 0; i < count; ++i) if (!engines[i].id || !*engines[i].id) return {};
    if (!s.dependencies.resize(dependency_count)) return {};
    s.runtime = &runtime;
    s.dependencies[0] = {kStorageServiceId, true};
    if (count) std::copy_n(engines, count, s.dependencies.begin() + 1);
    if(device_provider)s.dependencies[count+1]={device_provider,true};
    s.dependency_count = dependency_count;
  }
  ServiceNode node;
  node.definition = {kApplicationsServiceId, "Applications", kMainServiceExecutor,
      ServiceCriticality::Optional, s.dependencies.data(), s.dependency_count};
  node.context = this;
  node.start = [](void *p) {
    auto &owner = *static_cast<ApplicationsService *>(p);
    auto &s = *owner.impl_;
    if (s.operations.load() || !s.occupied_id.empty())
      return ServiceStartResult{ServiceStartDisposition::Failed, "applications_service_busy"};
    s.error.clear();
    s.phase = Impl::Phase::Degraded;
    const bool ok = owner.initialize();
    s.phase = ok ? Impl::Phase::Ready : Impl::Phase::Degraded;
    if(s.maintenance_worker.enable)s.maintenance_worker.enable(s.maintenance_worker.context,true);
    return ServiceStartResult{ok ? ServiceStartDisposition::Ready : ServiceStartDisposition::Degraded, s.error};
  };
  node.stop_step = [](void *p) { return static_cast<ApplicationsService *>(p)->stop_step(); };
  return node;
}
bool ApplicationsService::ready() const { return impl_->phase == Impl::Phase::Ready; }
ApplicationDeviceStatus ApplicationsService::device_status() const {return impl_->devices.status();}
bool ApplicationsService::owned_by(const KernelRuntime &runtime) const { return impl_->runtime == &runtime; }
bool ApplicationsService::configure_devices(const ApplicationDevicePorts &ports,std::string &error){
  auto &s=*impl_;
  if(s.runtime||s.phase.load()!=Impl::Phase::Registered||!s.occupied_id.empty()) {error="application_device_configuration_closed";return false;}
  return s.devices.configure(ports,error);
}
bool ApplicationsService::discard_uninstalled_declaration(std::string &error) {
  error.clear();auto &s=*impl_;
  if (!s.runtime) return true;
  if (s.runtime->service_registered(kApplicationsServiceId) ||
      s.phase.load()!=Impl::Phase::Registered || s.operations.load() ||
      !s.occupied_id.empty() || s.maintenance.wake_bound()) {
    error="applications_declaration_retained"; return false;
  }
  if (!s.runtime->can_reset_composition(error)) return false;
  s.runtime=nullptr;s.dependencies.clear();s.dependency_count=0;
  return true;
}
bool ApplicationsService::prepare(std::string &error) {
  if (ready()) { error.clear(); return true; }
  Impl::Admission admission(*impl_, error, true);
  if (!admission) return false;
  if (!initialize()) { error = impl_->error; return false; }
  // Lifecycle and UI operations share Main. Recovery publishes readiness only
  // after valid storage/catalog state; it cannot reverse a stop transition.
  if (!impl_->runtime->report_service_ready(kApplicationsServiceId, "catalog_recovered", error)) return false;
  impl_->phase = Impl::Phase::Ready;
  return true;
}
ServiceStopResult ApplicationsService::stop_step() {
  auto &s = *impl_;
  // Withdraw hardware immediately; an unrelated storage transaction must not
  // extend the lifetime of an application's output authority.
  s.devices.revoke();s.devices.poll();
  if(s.maintenance_worker.enable)s.maintenance_worker.enable(s.maintenance_worker.context,false);
  {
    std::lock_guard lock(s.admission_mutex);
    s.phase = Impl::Phase::Stopping;
    if (s.application_work && !s.application_work->started && s.application_work->result.pending()) {
      s.application_work->result.complete(ApplicationOperationPhase::Cancelled, "applications_service_stopping");
      s.application_work.reset();
    }
    if (s.operations.load()) return {ServiceStopDisposition::Pending, "applications_operations_pending"};
  }
  if(s.maintenance_worker.retired&&!s.maintenance_worker.retired(s.maintenance_worker.context))
    return {ServiceStopDisposition::Pending,"applications_maintenance_worker_retiring"};
  std::string error;
  if (!close(error, 0)) {
    const bool pending = error == "application_stop_timeout" || error == "application_catalog_busy" || error=="application_device_retirement_pending";
    return {pending ? ServiceStopDisposition::Pending : ServiceStopDisposition::Failed, error};
  }
  if (runtime_detail::has_retired_applications(s.runtime)) {
    // A stopped service no longer admits maintenance I/O. Retire the existing
    // nonblocking task handshake here; final writes remain on its task.
    runtime_detail::poll_retired_applications();
    if (runtime_detail::has_retired_applications(s.runtime))
      return {ServiceStopDisposition::Pending, "applications_retirement_pending"};
  }
  s.phase = Impl::Phase::Stopped;
  return {ServiceStopDisposition::Stopped, {}};
}
ApplicationsService::~ApplicationsService() {
  if(impl_->maintenance_worker.enable)impl_->maintenance_worker.enable(impl_->maintenance_worker.context,false);
  // The lane borrows this public service object. Retaining only Impl cannot
  // extend that object's lifetime; require the normal stop handshake first.
  if(impl_->maintenance_worker.retired&&!impl_->maintenance_worker.retired(impl_->maintenance_worker.context)){
    ESP_LOGE("lunaros_app","maintenance worker outlives ApplicationsService");
    std::abort();
  }
  // Borrowers must retire before destroying this owner. Cancel queued work so
  // retained UI receipts cannot remain pending after a normal shutdown.
  if (impl_->application_work && !impl_->application_work->started && impl_->application_work->result.pending())
    impl_->application_work->result.complete(ApplicationOperationPhase::Cancelled, "applications_service_stopping");
  std::string error;
  if (!close(error)) {
    ESP_LOGE("lunaros_app", "retaining application owner after stop failure: %s", error.c_str());
    auto &retirement = impl_->retirement;
    retirement.context = impl_.get();
    retirement.runtime = impl_->runtime;
    retirement.stop = [](void *p) {
      auto &s=*static_cast<Impl*>(p);
      if(!s.runtime||!s.runtime->current_task_owns_executor(kMainServiceExecutor))return false;
      s.devices.revoke();s.devices.poll();std::string error;
      return s.session.close(error,0)&&s.devices.retired();
    };
    retirement.destroy = [](void *p) { delete static_cast<Impl *>(p); };
    adopt_retired_application(retirement);
    (void)impl_.release();
  }
}
const std::string &ApplicationsService::failure() const { return impl_->error; }
bool ApplicationsService::poll() {
  if(impl_->runtime&&!impl_->runtime->current_task_owns_executor(kMainServiceExecutor))return false;
  impl_->devices.poll();
  const bool catalog_changed = observe_catalog();
  const bool retired_session = !impl_->occupied_id.empty() &&
      (impl_->retiring || (!impl_->starting && !impl_->session.active()));
  if (retired_session) {
    const auto failure = impl_->session.failure(); std::string cleanup;
    (void)close(cleanup, 0); impl_->error = cleanup.empty() ? failure : cleanup;
  }
  // Session retirement must not suppress housekeeping. One owner poll advances
  // both paths exactly once, including a stop handshake still in progress.
  poll_maintenance();
  const bool work_changed=poll_application_work();
  return retired_session || catalog_changed || work_changed;
}
bool ApplicationsService::open(const std::string &id) {
  std::string error;
  if (!close(error)) return false;
  const auto deadline=esp_timer_get_time()+30'000'000;
  for (;;) {
    const auto result=start(id,error);
    if(result==app::ApplicationStartResult::Started)return true;
    if(result==app::ApplicationStartResult::Failed)break;
    if(esp_timer_get_time()>=deadline){error="application_start_timeout";break;}
    vTaskDelay(1);
  }
  impl_->error=error;
  std::string cleanup;(void)close(cleanup,0);
  // Preserve the startup cause while a cancelled task retires asynchronously.
  impl_->error=error;
  return false;
}
app::ApplicationStartResult ApplicationsService::start(const std::string &id,std::string &error) {
  auto &s=*impl_;error.clear();
  if(s.runtime&&!s.runtime->current_task_owns_executor(kMainServiceExecutor)){
    error="application_start_requires_main";return app::ApplicationStartResult::Failed;
  }
  Impl::Admission admission(s, error);
  if (!admission) return app::ApplicationStartResult::Failed;
  (void)observe_catalog();
  if(s.starting && s.occupied_id==id && !s.retiring) {
    const auto result=s.session.open_result(error);
    if(result==app::ApplicationStartResult::Started){s.starting=false;s.active_id=id;s.active_tick_hz=s.session.tick_hz();}
    else if(result==app::ApplicationStartResult::Failed){s.error=error;s.retiring=true;++s.generation;}
    return result;
  }
  if(!s.retiring && s.active_id==id && s.session.active())return app::ApplicationStartResult::Started;
  if(!s.occupied_id.empty()){error="application_still_running";return app::ApplicationStartResult::Failed;}
  if(!id_valid(id)||!s.runtime){error=s.runtime?"application_identity_invalid":"service_applications_missing";s.error=error;return app::ApplicationStartResult::Failed;}
  {
    // Install/cleanup can hold this gate during storage commits. Foreground
    // admission retries instead of waiting behind them on the input owner.
    std::unique_lock lock(catalog_mutex,std::try_to_lock);
    if(!lock.owns_lock())return app::ApplicationStartResult::Pending;
    if(owned_by_other(impl_.get(),id)){error=s.error="application_still_running";return app::ApplicationStartResult::Failed;}
    s.occupied_id=id;active_owners[impl_.get()]=id;
  }
  s.save_path="/managed/apps/"+id+"/save.bin";s.error.clear();s.starting=true;
  const auto &catalog=s.catalog.entries();
  const auto selected=std::find_if(catalog.begin(),catalog.end(),[&](const Application &app){return app.id==id;});
  if(selected==catalog.end()||!s.devices.begin(id,selected->device_basic,error)){
    if(error.empty())error="application_not_installed";
    s.error=error;s.retiring=true;std::string cleanup;(void)close(cleanup,0);return app::ApplicationStartResult::Failed;
  }
  if(!s.session.start(*s.runtime,error,kApplicationsServiceId)||!s.session.begin_open(Impl::prepare_entry,impl_.get(),error)) {
    s.error=error;s.retiring=true;++s.generation;
    std::string cleanup;(void)close(cleanup,0);s.error=error;
    return app::ApplicationStartResult::Failed;
  }
  return app::ApplicationStartResult::Pending;
}
bool ApplicationsService::Impl::prepare_entry(void *context,ApplicationEntry &entry,std::string &error) {
  auto &s=*static_cast<Impl *>(context);
  Application selected;
  {
    std::lock_guard lock(catalog_mutex);
    std::vector<Application> catalog;
    if(!read_catalog(catalog)){error="installed_catalog_invalid";return false;}
    const auto it=std::find_if(catalog.begin(),catalog.end(),[&](const auto &a){return a.id==s.occupied_id;});
    if(it==catalog.end()){error="application_not_installed";return false;}
    selected=*it;
  }
  // The identity reservation prevents reinstall/uninstall/reset for this ID,
  // so package and save I/O need no catalog lock while the task prepares it.
  if (!application_backend_available(selected.entry_format)) {
    error = "此设备未启用 " + selected.entry_format + " 应用后端";
    return false;
  }
  Application app;
  if (!read_verified_package(selected, entry.bytes, app, entry.offset, error)) {
    if(error.empty())error="已安装应用校验失败";
    return false;
  }
  std::vector<uint8_t> saved;
  if (app.private_storage && storage_read_file_bounded(s.save_path.c_str(), kMaximumSaveBytes, saved))
    ESP_LOGI("lunaros_app", "restore id=%s save_bytes=%u sha256=%s", s.occupied_id.c_str(),
             unsigned(saved.size()),
             digest(saved.data(), saved.size()).c_str());
  s.last_saved=saved;
  entry.storage=app.private_storage?s.storage_callbacks():SessionStorage{};
  entry.storage.device=s.devices.binding(app.device_basic);
  entry.seed=uint32_t(esp_timer_get_time());entry.tick_hz=app.tick_hz;entry.backend=app.entry_format;
  return true;
}
bool ApplicationsService::close(std::string &error,uint32_t timeout_ms) {
  impl_->devices.revoke();
  if(impl_->runtime&&impl_->runtime->current_task_owns_executor(kMainServiceExecutor))impl_->devices.poll();
  if(!impl_->devices.retired())timeout_ms=0;
  if (!impl_->session.close(error,timeout_ms)) {
    impl_->retiring = true;
    impl_->error = error;
    ++impl_->generation;
    return false;
  }
  if(!impl_->devices.retired()){
    error="application_device_retirement_pending";impl_->retiring=true;return false;
  }
  {
    std::unique_lock lock(catalog_mutex,std::defer_lock);
    if(timeout_ms)lock.lock();
    else if(!lock.try_lock()){error="application_catalog_busy";impl_->retiring=true;return false;}
    active_owners.erase(impl_.get());
  }
  // Final writes were acknowledged by the application task. Retirement does
  // no diagnostic file read on the foreground owner or under the catalog gate.
  impl_->active_id.clear();
  impl_->occupied_id.clear();
  impl_->retiring = false;
  impl_->starting = false;
  impl_->active_tick_hz = 0;
  impl_->validation_save.reset();
  return true;
}
bool ApplicationsService::input(int kind, int a, int b, int c) {
  if (!ready()) return false;
  auto &s = *impl_;
  std::string error;
  if (!s.session.input(kind, a, b, c)) {
    error=s.session.failure();
    if (!error.empty()) {
      s.error = error;
      s.generation++;
    }
    return false;
  }
  return true;
}
bool ApplicationsService::running() const { return impl_->session.active(); }
bool ApplicationsService::realtime() const { return impl_->active_tick_hz>0; }
const std::string &ApplicationsService::running_id() const { return impl_->active_id; }
std::shared_ptr<const DrawList> ApplicationsService::frame() const {
  return impl_->session.frame();
}
bool ApplicationsService::checkpoint(std::string &error) {
  Impl::Admission admission(*impl_, error);
  if (!admission) return false;
  if(!running())return true;
  return impl_->session.checkpoint(error);
}
} // namespace lunaredu::lunaros::apps
