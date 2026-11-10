#pragma once
#include <cstddef>
#include <string>
namespace lunaredu::lunaros {
bool storage_pool_enabled();
size_t storage_pool_user_reserve();
bool storage_pool_set_user_reserve(size_t bytes, std::string &error);
size_t storage_pool_user_used();
}
