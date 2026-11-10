#pragma once
#include "app/package/application_list.h"
#include "app/package/application_operation.h"
namespace lunaredu::lunaros::apps {
// Settings owns package presentation/confirmation; OS owns the transaction.
struct ApplicationDataPorts {
  void *context = nullptr, *prepare_context = nullptr;
  bool (*prepare)(void *, std::string &) = nullptr;
  // Cached catalog published by the lifecycle owner; no filesystem refresh.
  bool (*catalog)(void *, ApplicationList &, std::string &) = nullptr;
  // Accepted request; receipt supplies the durable outcome. No Main storage I/O.
  bool (*uninstall)(void *, const std::string &, bool delete_data, ApplicationOperation &, std::string &) = nullptr;
  bool (*observe_revision)(void *, uint64_t &) = nullptr;
};
}
