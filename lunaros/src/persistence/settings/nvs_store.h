#pragma once

#include <string>

namespace lunaredu::lunaros {

// 初始化 NVS 存储。NVS 是设备启动早期的基础存储能力。
bool setup_nvs_store();

// 当前 NVS 是否可用。
bool nvs_store_ready();

// 最近一次 NVS 错误。
const char *nvs_store_last_error();

// 从 NVS 读取字符串。
bool nvs_store_read_string(const char *namespace_name, const char *key,
                           std::string &value);

// 向 NVS 写入字符串。
bool nvs_store_write_string(const char *namespace_name, const char *key,
                            const std::string &value);

enum class NvsReadResult { Found, Missing, Failed };
// A missing record may be migrated; unreadable/oversized records must not be
// mistaken for first boot or cause allocation from an untrusted stored length.
NvsReadResult nvs_store_read_blob(const char *namespace_name, const char *key,
                                     size_t maximum_bytes, std::string &value);
NvsReadResult nvs_store_read_string_bounded(const char *namespace_name, const char *key,
                                            size_t maximum_bytes, std::string &value);
bool nvs_store_write_blob(const char *namespace_name, const char *key,
                          const std::string &value);

bool nvs_store_read_bool(const char *namespace_name, const char *key,
                         bool &value);
bool nvs_store_write_bool(const char *namespace_name, const char *key,
                          bool value);

// 删除 NVS 中的单个键。
bool nvs_store_erase_key(const char *namespace_name, const char *key);

} // namespace lunaredu::lunaros
