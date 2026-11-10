#pragma once
#include "lunaros_applications.h"
#include "app/execution/app_executor.h"
#include "application_maintenance_internal.h"
#include <atomic>
#include "kernel/registration_storage.h"
#include <map>
#include <mutex>

namespace lunaredu::lunaros::apps {
namespace runtime_detail { class ApplicationIdentityReservation; }
struct ApplicationsService::Impl {
  enum class Phase { Registered, Ready, Degraded, Stopping, Stopped };
  std::atomic<Phase> phase{Phase::Registered};
  runtime_detail::ApplicationMaintenanceState maintenance;
  ApplicationMaintenanceWorkerPorts maintenance_worker;
  std::mutex admission_mutex;
  std::atomic<size_t> operations{0};
  struct ApplicationWork {
    enum class Kind { Install, Uninstall, Checkpoint, SaveInfo, ExportSave, ResetSave, ImportSave };
    Kind kind=Kind::Install;
    std::string id;
    bool delete_data=false,started=false,ready_for_io=true,wait_for_retirement=false,allow_foreground_read=false;
    // Admission metadata above is serialized by admission_mutex, never held
    // across I/O. Main completes its own VM handshake before marking I/O ready.
    ApplicationOperationSource result;
    ApplicationOperation checkpoint;
    std::shared_ptr<ApplicationSaveData> save;
    std::vector<uint8_t> input;
  };
  // Cold explicit work only; no queue/task/idle payload. Terminal receipts may
  // outlive this slot and contain no borrowed service or runtime references.
  std::shared_ptr<ApplicationWork> application_work;
  bool queue_work(const std::shared_ptr<ApplicationWork> &work,std::string &error) {
    std::lock_guard lock(admission_mutex);
    if(phase.load()!=Phase::Ready){error="applications_service_unavailable";return false;}
    if(application_work&&application_work->result.pending()){error="application_operation_pending";return false;}
    application_work=work;return true;
  }
  void finish_work(const std::shared_ptr<ApplicationWork> &work,bool ok,std::string error,uint64_t revision=0) {
    work->result.complete(ok?ApplicationOperationPhase::Succeeded:ApplicationOperationPhase::Failed,
        std::move(error),revision);
    std::lock_guard lock(admission_mutex);
    if(application_work==work)application_work.reset(); // Local shared owner retires payload outside the gate.
  }
  bool execute_save_work(const std::shared_ptr<ApplicationWork> &,std::string &);
  bool install_reserved(const Application &,PackageBytes,std::vector<Application>,ApplicationCatalogSnapshot &,std::string &);
  RegistrationArray<ServiceDependency> dependencies;
  size_t dependency_count = 0;
  // Only admission is locked. Package/save I/O keeps its existing scheduling
  // and per-ID transaction reservation. Stop retains dependencies until all
  // admitted operations have returned, including Network commit callbacks.
  struct Admission {
    Impl *owner = nullptr;
    Admission(Impl &s, std::string &error, bool recovery = false) {
      std::lock_guard lock(s.admission_mutex);
      const auto phase = s.phase.load();
      if (!s.runtime || (phase != Phase::Ready && !(recovery && phase == Phase::Degraded))) {
        error = "applications_service_unavailable"; return;
      }
      ++s.operations; owner = &s;
    }
    ~Admission() { if (owner) --owner->operations; }
    Admission(const Admission &) = delete;
    explicit operator bool() const { return owner != nullptr; }
  };
  ApplicationCatalogSnapshot catalog;
  std::string active_id, occupied_id, save_path, error;
  bool retiring = false;
  bool starting = false;
  AppExecutor session;
  ApplicationDeviceHost devices;
  static bool prepare_entry(void *, ApplicationEntry &, std::string &);
  SessionStorage storage_callbacks();
  bool save_access(const std::string &, runtime_detail::ApplicationIdentityReservation &, std::string &, Application * = nullptr);
  bool read_save(const std::string &, SaveInfo &, std::vector<uint8_t> &, std::string &);
  KernelRuntime *runtime = nullptr;
  int active_tick_hz = 0;
  uint32_t generation = 0;
  std::vector<uint8_t> last_saved;
  std::shared_ptr<std::vector<uint8_t>> validation_save;
  runtime_detail::RetiredApplicationOwner retirement;
};
namespace runtime_detail {
// One gate for catalog commits and active session identities across all channels.
extern std::mutex catalog_mutex;
extern std::map<const void *, std::string> active_owners;
bool read_catalog(std::vector<Application> &catalog);
// Caller holds catalog_mutex. Validation sessions reserve the same identity as
// foreground sessions, through acknowledged retirement and final save commit.
bool owned_by_other(const void *owner, const std::string &id);
// Reserves one ID for a synchronous data/package transaction. Slow I/O runs
// outside catalog_mutex; other IDs can still commit, while this ID cannot be
// launched, replaced, reset or collected. The reservation never borrows a VM.
class ApplicationIdentityReservation {
 public:
  ApplicationIdentityReservation()=default;
  ApplicationIdentityReservation(const ApplicationIdentityReservation &)=delete;
  ApplicationIdentityReservation &operator=(const ApplicationIdentityReservation &)=delete;
  ~ApplicationIdentityReservation(){if(held_){std::lock_guard lock(catalog_mutex);active_owners.erase(this);}}
  void reserve_locked(const std::string &id){active_owners[this]=id;held_=true;}
 private:
  bool held_=false;
};
bool read_verified_package(const Application &, std::vector<uint8_t> &,
                           Application &, size_t &, std::string &);
bool verify_package(const Application &, PackageBytes,
                    Application &, size_t &, std::string &);
}
} // namespace lunaredu::lunaros::apps
