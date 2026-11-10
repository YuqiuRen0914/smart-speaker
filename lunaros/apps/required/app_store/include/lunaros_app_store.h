#pragma once
#include "app/package/application_list.h"
#include <memory>
#include <string>
#include <vector>
namespace lunaredu::lunaros::apps {
class ApplicationsService;
enum class StorePhase {
  Idle,
  Fetching,
  Downloading,
  Verifying,
  Committing,
  Failed
};
struct StoreSnapshot {
  ApplicationList discovered, installed;
  StorePhase phase = StorePhase::Idle;
  std::string error, message;
  uint32_t generation = 0, received_bytes = 0, total_bytes = 0;
};
// Called only by the input/lifecycle owner. Network jobs publish completed
// results into retained contexts; rendering consumes immutable snapshots.
class Store {
public:
  Store();
  ~Store();
  // Borrow the OS catalog owner; it must outlive this distribution client.
  // Setup and destruction do not acquire or stop an application session.
  void setup(ApplicationsService &);
  bool refresh();
  bool install(const std::string &id);
  bool update(const std::string &id);
  bool uninstall(const std::string &id, bool delete_data, std::string &error);
  bool poll();
  const StoreSnapshot &snapshot() const;
  const std::string &endpoint() const;
  bool set_endpoint(const std::string &url);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace lunaredu::lunaros::apps
