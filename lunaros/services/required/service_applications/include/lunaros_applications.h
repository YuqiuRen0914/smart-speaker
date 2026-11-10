#pragma once
#include "lunaros_applications_service_id.h"
#include "app/package/application_package.h"
#include "app/package/application_list.h"
#include "app/execution/application_session.h"
#include "app/execution/application_maintenance.h"
#include "app/package/application_data_ports.h"
#include "app/package/application_operation.h"
#include "app/package/application_save_operation.h"
#include "app/execution/application_foreground_ports.h"
#include "kernel/service.h"
#include <memory>
#include <utility>
#include <vector>
namespace lunaredu::lunaros { class KernelRuntime; }
namespace lunaredu::lunaros::apps {
// Immutable catalog observation/commit receipt. Only the storage owner can
// publish entries; readers may retain old observations across later commits.
class ApplicationCatalogSnapshot {
public:
  ApplicationCatalogSnapshot() = default;
  const std::vector<Application> &entries() const {
    static const std::vector<Application> empty;
    return entries_ ? *entries_ : empty;
  }
  uint64_t revision() const { return revision_; }
  ApplicationList application_list() const { return ApplicationList(entries_); }
  explicit operator bool() const { return bool(entries_); }
private:
  friend class ApplicationsService;
  ApplicationCatalogSnapshot(std::shared_ptr<const std::vector<Application>> entries, uint64_t revision)
      : entries_(std::move(entries)), revision_(revision) {}
  std::shared_ptr<const std::vector<Application>> entries_;
  uint64_t revision_ = 0;
};
// Lifecycle, recovery and foreground state are serialized on Main, independently
// of Store lifetime. install() and maintenance I/O may run on the existing I/O
// executor; they publish immutable catalog values for Main to observe, never
// mutate the foreground catalog reference from that thread.
// Network acquisition submits immutable packages; only OS commits local state.
class ApplicationsService {
public:
  ApplicationsService();
  ~ApplicationsService();
  ApplicationsService(const ApplicationsService &) = delete;
  ApplicationsService &operator=(const ApplicationsService &) = delete;
  // Owned by system composition. Borrowers must not destroy/reset this owner.
  // Node declaration creates no task/VM. Additional dependencies describe
  // selected engine lifetimes and are copied into the service owner. Dependency
  // IDs and the Kernel must remain valid for the service node's whole lifetime.
  ServiceNode node(KernelRuntime &, const ServiceDependency * = nullptr, size_t = 0);
  bool ready() const;
  bool owned_by(const KernelRuntime &) const;
  // Boot composition only. Products provide bounded Main-owned device ports;
  // package permission is independently rechecked before creating the guest.
  bool configure_devices(const ApplicationDevicePorts &,std::string &error);
  // Boot-only rollback of a node which was never installed/started. Rejects
  // registered graphs, active execution and retained maintenance callbacks.
  bool discard_uninstalled_declaration(std::string &error);
  // Explicit catalog recovery on the lifecycle/input owner; never reopens a
  // Registered, Stopping or Stopped service.
  bool prepare(std::string &);
  // Borrows the immutable bytes only until this synchronous call returns.
  bool install(const Application &, PackageBytes,
               ApplicationCatalogSnapshot &, std::string &);
  // Main reserves one explicit mutation before handing it to its existing I/O
  // acquisition owner. Callback-supplied bytes and its context stay valid until
  // install_acquired returns. It runs after cross-channel identity reservation,
  // outside the catalog gate; the OS never retains its bytes after that call.
  using PackageAcquisition = bool (*)(void *, PackageBytes &, std::string &);
  bool request_install(const std::string &, ApplicationOperation &, std::string &);
  bool install_acquired(const ApplicationOperation &, const Application &,
      PackageAcquisition, void *, ApplicationCatalogSnapshot &, std::string &);
  bool cancel_install(const ApplicationOperation &, std::string &);
  bool reload_catalog();
  // Main adopts an immutable publication without reading storage or waiting
  // behind a catalog commit. False means no new observation was available.
  bool observe_catalog();
  // Last successfully reloaded catalog, borrowed only on its foreground owner.
  const std::vector<Application> &installed() const;
  ApplicationCatalogSnapshot catalog_snapshot() const;
  uint64_t installed_revision() const;
  // Input/lifecycle owner queues work on the composition-bound maintenance I/O
  // lane. Success means accepted; retain the receipt to observe completion.
  bool request_uninstall(const std::string &, bool, ApplicationOperation &, std::string &);
  // Synchronous transaction for explicit I/O callers. UI uses request_uninstall.
  bool uninstall(const std::string &, bool, std::string &);
  bool poll();
  // Called only by the single composition-bound application I/O owner.
  void run_maintenance_io();
  uint32_t maintenance_delay_ms() const;
  ApplicationCleanupStatus cleanup_status() const;
  ApplicationDeviceStatus device_status() const;
  bool retry_cleanup();
  // Immutable binding to the composition-owned I/O bridge; never a worker handle.
  bool bind_maintenance_wake(void (*)(void *), void *);
  bool bind_maintenance_worker(const ApplicationMaintenanceWorkerPorts &);
  const std::string &failure() const;
  bool open(const std::string &);
  // Idempotent foreground start. Admission and completion reads do no storage
  // I/O; the existing application task owns preparation and backend open.
  app::ApplicationStartResult start(const std::string &, std::string &);
  bool close(std::string &, uint32_t timeout_ms = 3000);
  bool input(int, int = 0, int = 0, int = 0);
  bool running() const;
  bool realtime() const;
  const std::string &running_id() const;
  std::shared_ptr<const DrawList> frame() const;
  bool checkpoint(std::string &);
  // Main submits bounded save maintenance; payloads become readable only after
  // success. Import moves its complete transfer; no input pointer is borrowed.
  bool request_save(SaveRequest, const std::string &, ApplicationSaveTicket &, std::string &,
      std::vector<uint8_t> input = {});
  bool save_info(const std::string &, SaveInfo &, std::string &);
  bool export_save(const std::string &, std::vector<uint8_t> &, std::string &);
  bool import_save(const std::string &, const uint8_t *, size_t, std::string &);
  bool reset_save(const std::string &, std::string &);
  bool backup_exists(const std::string &);
  bool execute_save(const std::string &, SaveOperation, std::string &);
private:
  void poll_maintenance();
  bool initialize();
  bool uninstall_io(const std::string &, bool, ApplicationCatalogSnapshot &, std::string &);
  void run_application_work_io();
  bool poll_application_work();
  bool request_package_mutation(const std::string &, bool uninstall, bool delete_data,
      ApplicationOperation &, std::string &);
  ServiceStopResult stop_step();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
ApplicationMaintenancePorts application_maintenance_ports(ApplicationsService &service);
ApplicationDataPorts application_data_ports(ApplicationsService &);
ApplicationForegroundPorts application_foreground_ports(ApplicationsService &runtime);
}
