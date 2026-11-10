#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <string_view>

namespace lunaredu::lunaros {

// OS owns initialization; failure never erases existing records. Named recovery
// partitions retain an explicit owner and do not change the default partition.
bool initialize_settings(std::string &error);
bool initialize_settings_partition(const char *partition, std::string &error);

// Generic persistent settings facade. Callers never depend on NVS handles,
// or ESP-IDF error types. Namespace/key names remain owner-defined.
bool settings_set_string(const char *name_space, const char *key,
                         const std::string &value);
bool settings_set_bool(const char *name_space, const char *key, bool value);
bool settings_erase(const char *name_space, const char *key);

// Typed, bounded persistence. Missing data is distinct from a storage failure;
// failed reads clear the output. Writes succeed only after commit. Existing NVS
// u16/blob records retain their type and bytes across firmware upgrades.
enum class SettingsReadResult { Found, Missing, Failed };
// maximum_bytes bounds the string payload, excluding its terminating NUL.
SettingsReadResult settings_read_string(const char *name_space, const char *key,
                                        size_t maximum_bytes, std::string &value,
                                        std::string &error);
SettingsReadResult settings_read_bool(const char *name_space, const char *key,
                                      bool &value, std::string &error);
SettingsReadResult settings_read_u16(const char *name_space, const char *key,
                                     uint16_t &value, std::string &error);
bool settings_write_u16(const char *name_space, const char *key,
                        uint16_t value, std::string &error);
SettingsReadResult settings_read_blob(const char *name_space, const char *key,
                                      size_t maximum_bytes, std::string &value,
                                      std::string &error);
bool settings_write_blob(const char *name_space, const char *key,
                         const std::string &value, std::string &error);
bool settings_erase(const char *name_space, const char *key, std::string &error);

SettingsReadResult settings_read_u8(const char *, const char *, uint8_t &, std::string &);
SettingsReadResult settings_read_i8(const char *, const char *, int8_t &, std::string &);
SettingsReadResult settings_read_u32(const char *, const char *, uint32_t &, std::string &);
SettingsReadResult settings_read_i32(const char *, const char *, int32_t &, std::string &);
bool settings_write_u8(const char *, const char *, uint8_t, std::string &);
bool settings_write_i8(const char *, const char *, int8_t, std::string &);
bool settings_write_u32(const char *, const char *, uint32_t, std::string &);
bool settings_write_i32(const char *, const char *, int32_t, std::string &);
// Caller-owned bounded storage avoids allocating/copying fixed-size records.
// Failed reads clear the supplied storage and actual_size. Missing is distinct.
SettingsReadResult settings_read_blob(const char *, const char *, void *output,
    size_t capacity, size_t &actual_size, std::string &error);
// Named partitions are used by recovery; format and dual-record policy stay
// with that owner. Ordinary consumers use the default-partition overloads.
SettingsReadResult settings_read_blob(const char *, const char *, size_t maximum,
    std::string &, std::string &error, const char *partition);
bool settings_write_blob(const char *, const char *, const std::string &,
    std::string &error, const char *partition);
bool settings_erase_namespace(const char *, std::string &error);

enum class SettingsValueType : uint8_t { Blob, String, U8, I8, U16, U32, I32 };
// Non-owning batch; keys and payloads live until the call returns. All entries
// are validated before opening storage. Preserve native record types and make
// one commit; this is not an atomic multi-key transaction or power-loss journal.
struct SettingsWrite {
  const char *key;
  SettingsValueType type;
  const void *data = nullptr;
  size_t size = 0;
  uint32_t integer = 0;
  static SettingsWrite blob(const char *key, const void *data, size_t size) {
    return {key, SettingsValueType::Blob, data, size};
  }
  static SettingsWrite text(const char *key, std::string_view value) {
    return {key, SettingsValueType::String, value.data(), value.size()};
  }
  static SettingsWrite i8(const char *key, int8_t value) {
    return {key, SettingsValueType::I8, nullptr, 0, static_cast<uint8_t>(value)};
  }
};
inline constexpr size_t kSettingsBatchCapacity = 32;
bool settings_write_batch(const char *, const SettingsWrite *, size_t count,
                          std::string &error);

} // namespace lunaredu::lunaros
