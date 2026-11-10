#pragma once
#include "app/execution/application_backend.h"
#include "app/lifecycle/application.h"
#include "app/package/application_operation.h"
#include <memory>
#include <vector>
namespace lunaredu::lunaros::apps {
// Prepared on the application task and retained through open/cancellation.
// Storage contexts belong to the caller and survive until close acknowledges
// task retirement. Preparation must not modify foreground-owned projections.
struct ApplicationEntry {
  std::vector<uint8_t> bytes;
  size_t offset = 0;
  SessionStorage storage;
  uint32_t seed = 0;
  int tick_hz = 0;
  std::string backend;
};
using ApplicationEntryPreparation = bool (*)(void *, ApplicationEntry &, std::string &);
// Serialize lifecycle calls on this executor's declaring owner (input owner
// for foreground, a separate I/O-owned instance for explicit validation).
// Storage callback contexts survive a failed close, including a timed-out worker.
class AppExecutor {
public:
  AppExecutor();
  ~AppExecutor();
  AppExecutor(const AppExecutor &) = delete;
  AppExecutor &operator=(const AppExecutor &) = delete;
  bool start(KernelRuntime &,std::string &,const char *owner_service_id = nullptr);
  bool begin_open(ApplicationEntryPreparation, void *, std::string &);
  app::ApplicationStartResult open_result(std::string &) const;
  int tick_hz() const;
  bool open(ApplicationEntry, std::string &);
  bool checkpoint(std::string &);
  // Queue on the existing VM task and observe a retained immutable completion.
  // No wait on the input owner; stop retains/cancels the actual request safely.
  bool begin_checkpoint(ApplicationOperation &, std::string &);
  bool validate_save(SessionStorage,std::string &);
  // Publish cancellation directly. Failure retains the worker, blocks input
  // and entry, and permits another close attempt. The final checkpoint is
  // attempted once per opened instance, including across retirement retries.
  // Success includes task release.
  bool close(std::string &,uint32_t timeout_ms = 3000);
  bool input(int,int,int,int);
  bool active()const;
  std::shared_ptr<const DrawList> frame()const;
  std::string failure()const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
