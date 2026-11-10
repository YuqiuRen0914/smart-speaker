#include "lunaros_app_store_runtime_console.h"
#include "app/package/application_limits.h"
#include <algorithm>
#include <charconv>
namespace lunaredu::lunaros::apps {
namespace {
bool number(const std::string &text, size_t &value) {
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  return !text.empty() && parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}
int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
}

void RuntimeConsole::reset_transfer() {
  std::vector<uint8_t>().swap(transfer_);import_id_.clear();import_size_=0;exporting_=false;
  if(!save_.pending())save_={}; // An admitted job owns its input; keep its observable receipt.
}
ApplicationCommandResult RuntimeConsole::dispatch(ApplicationsService &applications, const std::string &command) {
  ApplicationCommandResult out;
  if (command.size() > 1024) { reset_transfer(); out.handled = true; out.error = "command_too_long"; return out; }
  std::vector<std::string> args;
  args.reserve(4);
  // Avoid stream/locale machinery on the embedded host.
  for (size_t offset = 0; offset < command.size();) {
    const auto begin = command.find_first_not_of(" \t\r\n", offset);
    if (begin == std::string::npos) break;
    const auto end = command.find_first_of(" \t\r\n", begin);
    args.push_back(command.substr(begin, end == std::string::npos ? end : end - begin));
    if (end == std::string::npos || args.size() == 4) break; // All supported commands take at most three tokens.
    offset = end;
  }
  if (args.empty()) return out;
  const auto &op = args[0];
  out.handled = true;
  if (op.rfind("save", 0) == 0) {
    bool accepted=false;
    const auto submit=[&](SaveRequest request,const std::string &id,std::vector<uint8_t> input=std::vector<uint8_t>{}) {
      reset_transfer();save_request_=request;
      accepted=out.success=applications.request_save(request,id,save_,out.error,std::move(input));
    };
    if(op=="save-status"&&args.size()==1){
      const auto result=save_.result();
      if(result.phase==ApplicationOperationPhase::Pending){out.success=true;out.records.push_back("APP_SAVE_RESULT PENDING ");return out;}
      out.success=result.phase==ApplicationOperationPhase::Succeeded;
      out.error=out.success?"":result.error.empty()?"save_operation_unavailable":result.error;
      const auto *data=save_.data();
      if(out.success&&save_request_==SaveRequest::Info&&data)
        out.records.push_back("APP_SAVE_INFO bytes="+std::to_string(data->info.bytes)+" timestamp="+std::to_string(data->info.timestamp));
      if(out.success&&save_request_==SaveRequest::Export){
        exporting_=data&&data->bytes.size()>=28&&data->bytes.size()<=kMaximumSaveFileBytes;
        if(exporting_)out.records.push_back("APP_SAVE_SIZE "+std::to_string(data->bytes.size()));
        else {out.success=false;out.error="invalid_save_size";}
      }
    }else if(save_.pending())out.error="application_operation_pending";
    else if (op == "save" && args.size() == 1) submit(SaveRequest::Checkpoint,{});
    else if (op == "save-info" && args.size() == 2) submit(SaveRequest::Info,args[1]);
    else if (op == "save-export" && args.size() == 2) submit(SaveRequest::Export,args[1]);
    else if (op == "save-reset" && args.size() == 2) submit(SaveRequest::Reset,args[1]);
    else if (op == "save-release" && args.size()==1){reset_transfer();out.success=true;}
    else if (op == "save-read" && args.size() == 2) {
      size_t offset = 0;
      const auto *data=save_.data();
      if (exporting_ && data && number(args[1], offset) && offset < data->bytes.size()) {
        static constexpr char digits[] = "0123456789abcdef";
        std::string hex;
        const auto count = std::min<size_t>(32, data->bytes.size() - offset);
        for (size_t i = 0; i < count; ++i) {
          const auto byte = data->bytes[offset + i]; hex += digits[byte >> 4]; hex += digits[byte & 15];
        }
        out.records.push_back("APP_SAVE_DATA " + std::to_string(offset) + " " + hex);
        out.success = true;
      }
    } else if (op == "save-begin") {
      // A new request always invalidates the previous import, even if malformed.
      reset_transfer();
      size_t bytes = 0;
      if (args.size() == 3 && args[1].size() >= 3 && args[1].size() <= 47 &&
          number(args[2], bytes) && bytes >= 28 && bytes <= kMaximumSaveFileBytes) {
        import_id_ = args[1]; import_size_ = bytes; out.success = true;
      }
    } else if (op == "save-chunk" && args.size() == 3) {
      size_t offset = 0;
      const auto &hex = args[2];
      if (!import_id_.empty() && number(args[1], offset) && offset == transfer_.size() &&
          !hex.empty() && hex.size() <= 32 && hex.size() % 2 == 0 &&
          hex.size() / 2 <= import_size_ - transfer_.size()) {
        std::vector<uint8_t> chunk;
        out.success = true;
        for (size_t i = 0; i < hex.size(); i += 2) {
          const int a = hex_digit(hex[i]), b = hex_digit(hex[i + 1]);
          if (a < 0 || b < 0) { out.success = false; break; }
          chunk.push_back(uint8_t(a * 16 + b));
        }
        if (out.success) transfer_.insert(transfer_.end(), chunk.begin(), chunk.end());
      }
    } else if (op == "save-commit") {
      if (args.size() == 1 && !import_id_.empty() && transfer_.size() == import_size_){
        const auto id=import_id_;auto input=std::move(transfer_);submit(SaveRequest::Import,id,std::move(input));
      }else reset_transfer();
    }
    if (!out.success && out.error.empty()) out.error = "invalid_save_command";
    out.records.push_back(std::string("APP_SAVE_RESULT ") + (accepted?"PENDING ":out.success ? "OK " : "ERROR ") + out.error);
    return out;
  }

  if (op == "open" && args.size()==2) { out.launch_id=args[1];out.action=ApplicationHostAction::Launch;out.success=true;return out; }
  if (op == "home" && args.size()==1) { out.action=ApplicationHostAction::Home;out.success=true;return out; }
  if (op == "uninstall" && args.size()==2) {
    if(uninstall_&&uninstall_.pending()){out.error="application_operation_pending";}
    else out.success=applications.request_uninstall(args[1],false,uninstall_,out.error);
    out.records.push_back(std::string("APP_MANAGE_RESULT ")+(out.success?"PENDING ":"ERROR ")+out.error);return out;
  }
  if (op == "uninstall-status" && args.size()==1) {
    const auto result=uninstall_.result();
    out.success=result.phase==ApplicationOperationPhase::Succeeded;
    out.error=result.error;
    out.records.push_back(std::string("APP_MANAGE_RESULT ")+
        (uninstall_.pending()?"PENDING ":out.success?"OK ":"ERROR ")+out.error);return out;
  }
  if (op == "confirm" && args.size() == 1) out.success = applications.input(5);
  else if (op == "pause" && args.size() == 1) { const bool down = applications.input(9, 2, 1); const bool up = applications.input(9, 0, 1); out.success = down && up; }
  else if ((op == "left" || op == "right" || op == "up" || op == "down") && args.size() == 1) {
    const int direction = op == "left" ? 1 : op == "right" ? 2 : op == "up" ? 3 : 4;
    if (applications.running()) { out.success = applications.input(1, direction); out.frame_changed = true; }
  } else { out.handled = false; out.error = "unknown_store_command"; }

  return out;
}
}
