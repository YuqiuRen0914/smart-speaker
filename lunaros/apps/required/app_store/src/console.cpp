#include "lunaros_app_store_console.h"
#include "lunaros_app_manager_view.h"
namespace lunaredu::lunaros::apps {
ApplicationCommandResult StoreConsole::dispatch(Store &store, manager::View &view, const std::string &command) {
  ApplicationCommandResult out;
  if (command.size()>1024) {out.handled=true;out.error="command_too_long";return out;}
  std::vector<std::string> args;
  for(size_t offset=0;offset<command.size();) {
    auto begin=command.find_first_not_of(" \t\r\n",offset);if(begin==std::string::npos)break;
    auto end=command.find_first_of(" \t\r\n",begin);
    args.push_back(command.substr(begin,end==std::string::npos?end:end-begin));
    if(end==std::string::npos||args.size()==4)break;
    offset=end;
  }
  if(args.empty())return out;
  const auto &op=args[0];out.handled=true;
  if (op == "status" && args.size() == 1) out.success = true;
  else if (op == "refresh" && args.size() == 1) { out.success = store.refresh(); out.action = ApplicationHostAction::ShowStore; }
  else if ((op == "install" || op == "update") && args.size() == 2) {
    out.success = op == "install" ? store.install(args[1]) : store.update(args[1]); out.action = ApplicationHostAction::ShowStore;
  } else if (op == "detail" && args.size() == 2) { view.detail_id = args[1]; out.action = ApplicationHostAction::ShowStore; out.success = true; }

  else if(op=="endpoint"&&args.size()==2)out.success=store.set_endpoint(args[1]);
  else {out.handled=false;out.error="unknown_store_command";}
  return out;
}
}
