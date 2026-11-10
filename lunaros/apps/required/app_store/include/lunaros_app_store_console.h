#pragma once
#include "lunaros_app_store.h"
#include "lunaros_app_store_runtime_console.h"
namespace lunaredu::lunaros::apps {
namespace manager { struct View; }
// Resource discovery/acquisition commands only. Execution and saves use RuntimeConsole.
class StoreConsole {
public:
  ApplicationCommandResult dispatch(Store &, manager::View &, const std::string &);
};
}
