#pragma once
#include "lunaredu_account_resources.h"
#include "lunaredu_account_resources_json.h"
namespace lunaredu::account_resources {
void load_cache();
void persist_cache();
void erase_cache();
void replace_snapshot(AccountResourcesSnapshot value);
} // namespace lunaredu::account_resources
