#pragma once
#include "lunaros_applications.h"
namespace lunaredu::lunaros::apps {
enum class ApplicationHostAction { None, ShowStore, Launch, Home };
struct ApplicationCommandResult {
  bool handled=false, success=false, catalog_changed=false, frame_changed=false;
  ApplicationHostAction action=ApplicationHostAction::None;
  std::string launch_id, error;
  std::vector<std::string> records;
};
class RuntimeConsole {
public:
  ApplicationCommandResult dispatch(ApplicationsService &, const std::string &);
  void reset_transfer();
private:
  ApplicationOperation uninstall_;
  ApplicationSaveTicket save_;
  SaveRequest save_request_=SaveRequest::Checkpoint;
  std::vector<uint8_t> transfer_;
  std::string import_id_;
  size_t import_size_=0;
  bool exporting_=false;
};
}
