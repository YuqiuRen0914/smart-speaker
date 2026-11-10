#pragma once

#include <string>
#include <cstdint>


namespace lunaredu::lunaros {

// Core implementation contract. No App, Service or product may include this.
struct SettingsWrite;

// Boot and recovery decide when initialization is admitted; no task or Service.
bool initialize_nvs(std::string &error);
// Named state partitions are initialized explicitly; failure never erases them.
bool initialize_nvs_partition(const char *partition, std::string &error);

// 初始化 NVS 存储。NVS 是设备启动早期的基础存储能力。
bool setup_nvs_store(std::string *error = nullptr);
bool nvs_store_initialize(std::string *, bool force);

// 当前 NVS 是否可用。
bool nvs_store_ready();

// Error output belongs to this operation; no shared last-error state.

// 向 NVS 写入字符串。
bool nvs_store_write_string(const char *namespace_name, const char *key,
                            const std::string &value, std::string *error = nullptr);

enum class NvsReadResult { Found, Missing, Failed };
NvsReadResult nvs_store_read_u8(const char *, const char *, uint8_t &, std::string *);
NvsReadResult nvs_store_read_i8(const char *, const char *, int8_t &, std::string *);
NvsReadResult nvs_store_read_u32(const char *, const char *, uint32_t &, std::string *);
NvsReadResult nvs_store_read_i32(const char *, const char *, int32_t &, std::string *);
NvsReadResult nvs_store_read_blob_into(const char *, const char *, void *, size_t,
                                      size_t &, std::string *);
bool nvs_store_erase_namespace(const char *, std::string *);
bool nvs_store_validate_batch(const char *, const SettingsWrite *, size_t, std::string *);
bool nvs_store_write_batch(const char *, const SettingsWrite *, size_t, std::string *);
NvsReadResult nvs_store_read_u16(const char *, const char *, uint16_t &, std::string *error = nullptr);
bool nvs_store_write_u16(const char *, const char *, uint16_t, std::string *error = nullptr);
// A missing record permits first-boot defaults; unreadable/oversized records must not be
// mistaken for first boot or cause allocation from an untrusted stored length.
NvsReadResult nvs_store_read_blob(const char *namespace_name, const char *key,
                                     size_t maximum_bytes, std::string &value, std::string *error = nullptr);
NvsReadResult nvs_store_read_blob(const char *namespace_name, const char *key,
                                     size_t maximum_bytes, std::string &value, std::string *error,
                                     const char *partition);
// String bounds count payload bytes, excluding the terminating NUL.
NvsReadResult nvs_store_read_string_bounded(const char *namespace_name, const char *key,
                                            size_t maximum_bytes, std::string &value, std::string *error = nullptr);
bool nvs_store_write_blob(const char *namespace_name, const char *key,
                          const std::string &value, std::string *error = nullptr);
bool nvs_store_write_blob(const char *namespace_name, const char *key,
                          const std::string &value, std::string *error,
                          const char *partition);

NvsReadResult nvs_store_read_bool(const char *namespace_name, const char *key,
                         bool &value, std::string *error = nullptr);
bool nvs_store_write_bool(const char *namespace_name, const char *key,
                          bool value, std::string *error = nullptr);

// 删除 NVS 中的单个键。
bool nvs_store_erase_key(const char *namespace_name, const char *key, std::string *error = nullptr);

} // namespace lunaredu::lunaros
