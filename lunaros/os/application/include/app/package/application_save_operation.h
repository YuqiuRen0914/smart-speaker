#pragma once
#include "app/package/application_operation.h"
#include <vector>
namespace lunaredu::lunaros::apps {
struct SaveInfo { uint32_t bytes = 0, timestamp = 0; };
enum class SaveOperation { Export, Reset, Import };
enum class SaveRequest { Checkpoint, Info, Export, Reset, Import };
struct ApplicationSaveData {
  SaveInfo info;
  std::vector<uint8_t> bytes;
};
// Payload becomes readable only after completion. Copies retain the same
// immutable export; a consumer never receives a mutable I/O-owned vector.
class ApplicationSaveTicket {
public:
  explicit operator bool() const { return bool(completion_); }
  bool pending() const { return completion_.pending(); }
  ApplicationOperationResult result() const { return completion_.result(); }
  const ApplicationSaveData *data() const {
    return result().phase==ApplicationOperationPhase::Succeeded?data_.get():nullptr;
  }
private:
  friend class ApplicationsService;
  ApplicationOperation completion_;
  std::shared_ptr<const ApplicationSaveData> data_;
};
}
