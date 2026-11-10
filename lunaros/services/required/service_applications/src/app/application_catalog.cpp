#include "applications_service_internal.h"
#include "app/application_installation.h"
#include "app/package/package_validation.h"
#include "app/package/application_package_policy.h"
#include "app/package/package_trust.h"
#include "app/package/application_limits.h"
#include "lunaros_storage.h"
#include <algorithm>
#include <cstring>
#include <utility>
#include <esp_timer.h>
#include <esp_log.h>
namespace lunaredu::lunaros::apps {
namespace runtime_detail {
std::mutex catalog_mutex;
std::map<const void *, std::string> active_owners;
// One immutable publication for the current global Storage namespace. It holds
// no Kernel/callback contexts; startup/recovery still reloads durable storage.
// Keep the latest publication until replacement, even if its caller discards
// the receipt before Main observes it.
namespace {
std::shared_ptr<const std::vector<Application>> published_catalog;
std::atomic<uint64_t> catalog_revision{0};
}
bool read_catalog(std::vector<Application> &catalog) {
  if (!storage_path_exists("/managed/apps/catalog.json")) { catalog.clear(); return true; }
  return load_installed_applications(catalog);
}
}
using namespace runtime_detail;
namespace {
bool reserve_install(const std::string &id,ApplicationIdentityReservation &reservation,
    std::vector<Application> &installed,std::string &error) {
  std::lock_guard lock(catalog_mutex);
  if (owned_by_other(nullptr,id)) { error="application_still_running"; return false; }
  if (!read_catalog(installed)) { error="installed_catalog_invalid"; return false; }
  reservation.reserve_locked(id);return true;
}
bool stored_package_matches(const char *path, PackageBytes expected) {
  struct Comparison { PackageBytes expected; size_t offset = 0; } comparison{expected};
  const auto compare = [](const uint8_t *data, size_t size, void *context) {
    auto &state = *static_cast<Comparison *>(context);
    if (size > state.expected.size() - state.offset ||
        (size && std::memcmp(state.expected.data() + state.offset, data, size))) return false;
    state.offset += size;
    return true;
  };
  // Storage pins the open path throughout the synchronous stream. The input
  // already passed digest/signature admission; compare every byte without a
  // second package allocation, including the exact final length.
  return storage_stream_file(path, compare, &comparison) && comparison.offset == expected.size();
}
}
bool ApplicationsService::initialize() {
  if (!storage_ready()) { impl_->error = "application_storage_unavailable"; return false; }
  if (!storage_ensure_directory("/managed/apps")) {
    impl_->error = "application_directory_unavailable";
    return false;
  }
  return reload_catalog();
}
bool ApplicationsService::reload_catalog() {
  Impl::Admission admission(*impl_, impl_->error, true);
  if (!admission) return false;
  std::lock_guard lock(catalog_mutex);
  std::vector<Application> current;
  if (!read_catalog(current)) { impl_->error = "installed_catalog_invalid"; return false; }
  auto entries = std::make_shared<const std::vector<Application>>(std::move(current));
  const auto revision = catalog_revision.load(std::memory_order_relaxed) + 1;
  published_catalog = entries;
  impl_->catalog = ApplicationCatalogSnapshot(std::move(entries), revision);
  catalog_revision.store(revision, std::memory_order_release);
  if (impl_->error == "installed_catalog_invalid" || impl_->error == "application_directory_unavailable" ||
      impl_->error == "application_storage_unavailable" ||
      impl_->error == "service_applications_missing") impl_->error.clear();
  impl_->maintenance.request_cleanup();
  return true;
}
const std::vector<Application> &ApplicationsService::installed() const { return impl_->catalog.entries(); }
ApplicationCatalogSnapshot ApplicationsService::catalog_snapshot() const { return impl_->catalog; }
uint64_t ApplicationsService::installed_revision() const { return impl_->catalog.revision(); }
bool ApplicationsService::observe_catalog() {
  if (!ready() || installed_revision() == catalog_revision.load(std::memory_order_acquire)) return false;
  ApplicationCatalogSnapshot next;
  {
    std::unique_lock lock(catalog_mutex, std::try_to_lock);
    if (!lock.owns_lock()) return false;
    const auto revision = catalog_revision.load(std::memory_order_relaxed);
    auto entries = published_catalog;
    if (!entries || revision == installed_revision()) return false;
    next = ApplicationCatalogSnapshot(std::move(entries), revision);
  }
  impl_->catalog = std::move(next); // Retire old metadata outside the commit gate.
  return true;
}
bool ApplicationsService::uninstall(const std::string &id, bool data, std::string &error) {
  Impl::Admission admission(*impl_, error);
  if (!admission) return false;
  {
    std::lock_guard lock(impl_->admission_mutex);
    if(impl_->application_work&&impl_->application_work->result.pending()){error="application_operation_pending";return false;}
  }
  if (impl_->occupied_id==id && !close(error)) return false;
  ApplicationCatalogSnapshot committed;
  if (!uninstall_io(id, data, committed, error)) return false;
  (void)observe_catalog();
  return true;
}
bool ApplicationsService::request_uninstall(const std::string &id, bool data,
    ApplicationOperation &receipt, std::string &error) {
  return request_package_mutation(id,true,data,receipt,error);
}
bool ApplicationsService::request_install(const std::string &id,ApplicationOperation &receipt,std::string &error) {
  return request_package_mutation(id,false,false,receipt,error);
}
bool ApplicationsService::request_package_mutation(const std::string &id,bool uninstall,bool data,
    ApplicationOperation &receipt,std::string &error) {
  receipt = {}; error.clear();
  if (!ready()) { error="applications_service_unavailable"; return false; }
  if (!id_valid(id)) { error="application_identity_invalid"; return false; }
  if (impl_->occupied_id==id) { error="exit_active_package_first"; return false; }
  if (uninstall&&!impl_->maintenance.wake_bound()) { error="application_io_unavailable"; return false; }
  auto work=std::make_shared<Impl::ApplicationWork>();
  work->id=id;work->kind=uninstall?Impl::ApplicationWork::Kind::Uninstall:Impl::ApplicationWork::Kind::Install;work->delete_data=data;
  if(!impl_->queue_work(work,error))return false;
  receipt=work->result.receipt();
  if(uninstall){impl_->maintenance.schedule(0);impl_->maintenance.poll();}
  return true;
}
bool ApplicationsService::cancel_install(const ApplicationOperation &receipt,std::string &error) {
  error.clear();std::shared_ptr<Impl::ApplicationWork> retired;
  {
    std::lock_guard lock(impl_->admission_mutex);
    const auto &work=impl_->application_work;
    if(!work||work->kind!=Impl::ApplicationWork::Kind::Install||!receipt.same(work->result.receipt()))return true; // Already retired; no side effect to replay.
    if(work->started){error="application_operation_running";return false;}
    work->result.complete(ApplicationOperationPhase::Cancelled,"application_install_cancelled");
    retired=std::move(impl_->application_work);
  }
  return true;
}
void ApplicationsService::run_application_work_io() {
  std::shared_ptr<Impl::ApplicationWork> work;
  {
    std::lock_guard lock(impl_->admission_mutex);
    if (impl_->phase.load()!=Impl::Phase::Ready || !impl_->application_work ||
        impl_->application_work->kind==Impl::ApplicationWork::Kind::Install || !impl_->application_work->ready_for_io ||
        impl_->application_work->started || !impl_->application_work->result.pending()) return;
    work=impl_->application_work;work->started=true;
  }
  // run_maintenance_io holds lifecycle admission until this transaction and
  // terminal publication return. Stop cannot release its dependencies midway.
  ApplicationCatalogSnapshot committed;std::string error;
  const bool ok=work->kind==Impl::ApplicationWork::Kind::Uninstall
      ?uninstall_io(work->id,work->delete_data,committed,error):impl_->execute_save_work(work,error);
  impl_->finish_work(work,ok,std::move(error),committed.revision());
}
bool ApplicationsService::uninstall_io(const std::string &id, bool data,
    ApplicationCatalogSnapshot &committed, std::string &error) {
  error.clear();committed={};
  if (!id_valid(id)) { error="application_identity_invalid"; return false; }
  std::lock_guard lock(catalog_mutex);
  if (std::any_of(active_owners.begin(), active_owners.end(), [&](const auto &owner) { return owner.second==id; })) {
    error="application_still_running"; return false;
  }
  std::vector<Application> current;
  if (!read_catalog(current)) { error="installed_catalog_invalid"; return false; }
  auto next = std::make_shared<std::vector<Application>>(std::move(current));
  if (!remove_installed_application(*next, id, data, error)) return false;
  std::shared_ptr<const std::vector<Application>> entries = std::move(next);
  const auto revision = catalog_revision.load(std::memory_order_relaxed) + 1;
  published_catalog = entries;
  committed = ApplicationCatalogSnapshot(std::move(entries), revision);
  catalog_revision.store(revision, std::memory_order_release);
  impl_->maintenance.request_cleanup();
  return true;
}
bool ApplicationsService::install(const Application &selected, PackageBytes bytes,
    ApplicationCatalogSnapshot &result, std::string &error) {
  error.clear();
  result = {};
  Impl::Admission admission(*impl_, error);
  if (!admission) return false;
  if (!id_valid(selected.id)) { error="application_identity_invalid"; return false; }
  {
    std::lock_guard lock(impl_->admission_mutex);
    if(impl_->application_work&&impl_->application_work->result.pending()){error="application_operation_pending";return false;}
  }
  ApplicationIdentityReservation reservation;
  std::vector<Application> installed;
  if(!reserve_install(selected.id,reservation,installed,error))return false;
  return impl_->install_reserved(selected,bytes,std::move(installed),result,error);
}
bool ApplicationsService::install_acquired(const ApplicationOperation &receipt,const Application &selected,
    PackageAcquisition acquire,void *context,ApplicationCatalogSnapshot &result,std::string &error) {
  error.clear();result={};
  Impl::Admission admission(*impl_,error);
  if(!admission)return false;
  std::shared_ptr<Impl::ApplicationWork> work;
  {
    std::lock_guard lock(impl_->admission_mutex);
    const auto &pending=impl_->application_work;
    if(!pending||pending->kind!=Impl::ApplicationWork::Kind::Install||pending->id!=selected.id||!receipt.same(pending->result.receipt())||!acquire){
      error="application_install_request_invalid";return false;
    }
    if(pending->started){error="application_operation_running";return false;}
    if(impl_->phase.load()!=Impl::Phase::Ready){error="applications_service_unavailable";return false;}
    work=pending;work->started=true;
  }
  const bool ok=[&] {
    ApplicationIdentityReservation reservation;std::vector<Application> installed;
    if(!reserve_install(selected.id,reservation,installed,error))return false;
    PackageBytes bytes(nullptr,0);
    if(!acquire(context,bytes,error)){
      if(error.empty())error="application_acquisition_failed";
      return false;
    }
    if(impl_->phase.load()!=Impl::Phase::Ready){error="applications_service_stopping";return false;}
    return impl_->install_reserved(selected,bytes,std::move(installed),result,error);
  }(); // Release physical ID on I/O before publishing completion to Main.
  impl_->finish_work(work,ok,error,result.revision());
  return ok;
}
bool ApplicationsService::Impl::install_reserved(const Application &selected,PackageBytes bytes,
    std::vector<Application> installed,ApplicationCatalogSnapshot &result,std::string &error) {
  const auto previous=std::find_if(installed.begin(),installed.end(),[&](const auto &a){return a.id==selected.id;});
  if (previous!=installed.end() && compare_app_versions(selected.version,previous->version)<0) {
    error="application_downgrade_rejected"; return false;
  }
  // Finish a previous uninstall's data deletion before admitting a reinstall.
  // Otherwise a delayed cleanup could erase the new installation's saves.
  const auto deletion_marker="/managed/apps/"+selected.id+"/delete-data.pending";
  if (storage_path_exists(deletion_marker.c_str()) && !finish_application_data_deletion(selected.id, previous!=installed.end())) {
    error="application_cleanup_pending"; return false;
  }
  bool ok=false;
  Application checked;
  size_t offset = 0;
  if (!verify_package(selected, bytes, checked, offset, error)) {
  }
  else {
    checked.size = selected.size;
    checked.sha256 = selected.sha256;
    checked.signer_id = selected.signer_id;
    checked.package_signature = selected.package_signature;
    checked.installed = true;

    std::string dir = "/managed/apps/" + checked.id;
    bool room = installed.size() < kMaximumApplications ||
                std::any_of(
                    installed.begin(), installed.end(),
                    [&](const auto &a) { return a.id == checked.id; });
    auto path = application_package_path(checked);
    const bool package_exists = storage_path_exists(path.c_str());
    bool immutable =
        !package_exists ||
        stored_package_matches(path.c_str(), bytes);
    if (!room)
      error = "已安装应用数量已满";
    else if (!immutable)
      error = "同一版本不能覆盖";
    else if (!storage_ensure_directory(dir.c_str()) ||
             (!package_exists && !storage_write_file(path.c_str(), bytes.data(),
                                 bytes.size())))
      error = "空间不足或写入失败";
    else {
      // Other IDs can commit during package verification/writes. Rebase once
      // more at the serialized catalog commit, retaining our ID reservation.
      std::lock_guard lock(catalog_mutex);
      if(!read_catalog(installed)){maintenance.request_cleanup();error="installed_catalog_invalid";return false;}
      if(installed.size()>=kMaximumApplications && std::none_of(installed.begin(),installed.end(),[&](const auto &a){return a.id==checked.id;})){
        maintenance.request_cleanup();error="已安装应用数量已满";return false;
      }
      auto previous=std::find_if(installed.begin(),installed.end(),[&](const auto &a){return a.id==checked.id;});
      if(previous==installed.end())installed.push_back(std::move(checked));else *previous=std::move(checked);
      auto entries = std::make_shared<const std::vector<Application>>(std::move(installed));
      if (!(ok = commit_installed_applications(*entries))) {
        maintenance.request_cleanup();
        error = "安装目录提交失败";
      }
      else {
        const auto revision = catalog_revision.load(std::memory_order_relaxed) + 1;
        published_catalog = entries;
        result = ApplicationCatalogSnapshot(std::move(entries), revision);
        catalog_revision.store(revision, std::memory_order_release);
        maintenance.request_cleanup();
        error="application_cleanup_pending";
      }
    }
  }
  return ok;
}
} // namespace lunaredu::lunaros::apps
