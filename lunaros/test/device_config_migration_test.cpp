#include <cassert>
#include <cstdint>
#include <atomic>
#include <chrono>
#ifdef _WIN32
#include <windows.h>
#else
#include <thread>
#endif
#include "persistence/settings/device_config_record.h"
#include <iostream>
#include <map>
#include <string>

#include "lunaredu_lunaros/persistence/settings.h"
#include "persistence/settings/device_config_defaults.h"
#include "persistence/settings/device_config_mutation.h"
#include "persistence/settings/nvs_store.h"
#include "kernel/system_primitives.h"

namespace {

std::map<std::string, std::string> g_values;
bool fail_record_write = false;
bool fail_record_read = false;
std::string fail_legacy_key;
unsigned record_writes = 0;
std::atomic<bool> pause_record_write{false};
std::atomic<bool> record_write_entered{false};
std::atomic<bool> release_record_write{false};
void yield_test_thread() {
#ifdef _WIN32
  Sleep(1);
#else
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
}
void await_flag(const std::atomic<bool> &flag) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (!flag.load()) {
    assert(std::chrono::steady_clock::now() < deadline);
    yield_test_thread();
  }
}
class TestThread {
  void (*run_)();
#ifdef _WIN32
  HANDLE handle_;
  static DWORD WINAPI entry(void *context) {
    static_cast<TestThread *>(context)->run_(); return 0;
  }
#else
  std::thread thread_;
#endif
public:
  explicit TestThread(void (*run)()) : run_(run)
#ifndef _WIN32
      , thread_(run)
#endif
  {
#ifdef _WIN32
    handle_ = CreateThread(nullptr, 0, entry, this, 0, nullptr);
    assert(handle_ != nullptr);
#endif
  }
  void join() {
#ifdef _WIN32
    assert(WaitForSingleObject(handle_, 10000) == WAIT_OBJECT_0);
    CloseHandle(handle_);
#else
    thread_.join();
#endif
  }
};
void concurrent_wifi_writer() {
  const lunaredu::lunaros::DeviceWifiCredential credentials[] = {{"concurrent-wifi", "concurrent-password"}};
  assert(lunaredu::lunaros::save_device_wifi_credentials(credentials, 1));
}
void concurrent_cloud_writer() {
  assert(lunaredu::lunaros::save_device_cloud_config("production", "concurrent-credential"));
}

lunaredu::lunaros::DeviceConfig persisted_config() {
  lunaredu::lunaros::DeviceConfig config;
  assert(lunaredu::lunaros::decode_device_config_record(g_values.at("device_cfg:snapshot"), config));
  return config;
}

std::string storage_key(const char *namespace_name, const char *key) {
  return std::string(namespace_name) + ":" + key;
}

void seed_previous_schema(const char *schema) {
  g_values.clear();
  g_values["device_cfg:schema_ver"] = schema;
  g_values["device_cfg:wifi_count"] = "1";
  g_values["device_cfg:wifi_last"] = "0";
  g_values["device_cfg:ssid0"] = "preserved-wifi";
  g_values["device_cfg:pass0"] = "preserved-password";
  g_values["device_cfg:dev_uuid"] =
      lunaredu::lunaros::derive_local_device_uuid();
  g_values["device_cfg:dev_name"] = "Preserved Device";
  g_values["device_cfg:prov_ap"] = "Preserved AP";
  g_values["device_cfg:cloud_env"] = "development";
  g_values["device_cfg:cloud_cred"] = std::string(64, 'c');
}

} // namespace

namespace lunaredu::lunaros {

bool setup_nvs_store() { return true; }
bool nvs_store_ready() { return true; }
const char *nvs_store_last_error() { return ""; }

bool nvs_store_read_string(const char *namespace_name, const char *key,
                           std::string &value) {
  const auto found = g_values.find(storage_key(namespace_name, key));
  if (found == g_values.end()) {
    value.clear();
    return false;
  }
  value = found->second;
  return true;
}

bool nvs_store_write_string(const char *namespace_name, const char *key,
                            const std::string &value) {
  g_values[storage_key(namespace_name, key)] = value;
  return true;
}

NvsReadResult nvs_store_read_string_bounded(const char *ns, const char *key,
                                           size_t maximum, std::string &value) {
  value.clear();
  if (key == fail_legacy_key) return NvsReadResult::Failed;
  if (!nvs_store_read_string(ns, key, value)) return NvsReadResult::Missing;
  if (value.size() > maximum) { value.clear(); return NvsReadResult::Failed; }
  return NvsReadResult::Found;
}

NvsReadResult nvs_store_read_blob(const char *ns, const char *key,
                                     size_t maximum, std::string &value) {
  value.clear();
  if (fail_record_read) return NvsReadResult::Failed;
  auto it = g_values.find(storage_key(ns, key));
  if (it == g_values.end()) return NvsReadResult::Missing;
  if (it->second.size() > maximum) return NvsReadResult::Failed;
  value = it->second;
  return NvsReadResult::Found;
}
bool nvs_store_write_blob(const char *ns, const char *key, const std::string &value) {
  if (pause_record_write.exchange(false)) {
    record_write_entered.store(true);
    await_flag(release_record_write);
  }
  ++record_writes;
  if (fail_record_write) return false;
  g_values[storage_key(ns, key)] = value;
  return true;
}

bool nvs_store_read_bool(const char *, const char *, bool &) { return false; }
bool nvs_store_write_bool(const char *, const char *, bool) { return true; }

bool nvs_store_erase_key(const char *namespace_name, const char *key) {
  g_values.erase(storage_key(namespace_name, key));
  return true;
}

} // namespace lunaredu::lunaros

namespace lunaredu::lunaros::internal {

bool system_read_factory_mac(uint8_t *buffer, size_t size) {
  if (buffer == nullptr || size < 6) {
    return false;
  }
  const uint8_t mac[6] = {0xE0, 0x72, 0xA1, 0xD6, 0xB7, 0xD0};
  for (size_t index = 0; index < 6; ++index) {
    buffer[index] = mac[index];
  }
  return true;
}

} // namespace lunaredu::lunaros::internal

int main() {
  using namespace lunaredu::lunaros;

  for (const char *key : {"schema_ver", "ssid0", "pass0", "cloud_cred", "dev_name"}) {
    seed_previous_schema("7");
    const auto before = g_values;
    fail_legacy_key = key;
    assert(!setup_device_config_service());
    assert(g_values == before);
    fail_legacy_key.clear();
    assert(setup_device_config_service());
    assert(device_config().wifi.credentials[0].password == "preserved-password");
    assert(device_cloud_credential_ready(64));
    assert(!device_cloud_credential_ready(65));
  }

  initialize_device_config();
  assert(!device_config().wifi.configured);
  assert(device_config().wifi.credential_count == 0);
  assert(device_config_name_is_default());
  assert(save_device_identity_config(device_config_default_device_base_name(), nullptr));
  assert(device_config_name_is_default());
  assert(save_device_identity_config("Teacher's Pet", nullptr));
  assert(!device_config_name_is_default());

  g_values.clear();
  assert(load_device_config());
  assert(!device_config().wifi.configured);
  assert(device_config().wifi.credential_count == 0);
  assert(g_values.count("device_cfg:snapshot") == 1);
  assert(persisted_config().wifi.credential_count == 0);
  assert(!persisted_config().wifi.configured);

  initialize_device_config();
  seed_previous_schema("4");

  assert(load_device_config());
  assert(device_config().wifi.configured);
  assert(device_config().wifi.credential_count == 1);
  assert(device_config().wifi.credentials[0].ssid == "preserved-wifi");
  assert(device_config().wifi.last_success_index == 0);
  assert(device_config().identity.name == "Preserved Device");
  assert(!device_config_name_is_default());
  assert(device_config().cloud.configured);
  assert(device_config().cloud.environment == "development");
  assert(device_config().cloud.credential == std::string(64, 'c'));
  assert(g_values.count("device_cfg:snapshot") == 1);

  initialize_device_config();
  seed_previous_schema("");
  g_values.erase("device_cfg:schema_ver");
  assert(load_device_config());
  assert(device_config().cloud.configured);
  assert(device_config().cloud.credential == std::string(64, 'c'));
  assert(device_config().wifi.credential_count == 1);
  assert(device_config().wifi.credentials[0].ssid == "preserved-wifi");
  assert(g_values.count("device_cfg:snapshot") == 1);

  initialize_device_config();
  seed_previous_schema("3");
  g_values["device_cfg:ssid0"] = "lunaredu";
  g_values["device_cfg:pass0"] = "lunaredu";
  assert(load_device_config());
  assert(!device_config().wifi.configured);
  assert(device_config().wifi.credential_count == 0);
  assert(g_values.find("device_cfg:ssid0") == g_values.end());

  // Schema 6 allowed lunaredu/lunaredu as a user-submitted network but still
  // appended lunaredu-student/lunaredu. Schema 7 must preserve the former and
  // remove only the firmware-owned fallback.
  initialize_device_config();
  seed_previous_schema("6");
  g_values["device_cfg:wifi_count"] = "2";
  g_values["device_cfg:ssid0"] = DEVICE_CONFIG_LEGACY_DEFAULT_WIFI_SSID;
  g_values["device_cfg:pass0"] = DEVICE_CONFIG_LEGACY_DEFAULT_WIFI_PASSWORD;
  g_values["device_cfg:ssid1"] = DEVICE_CONFIG_RETIRED_DEFAULT_WIFI_SSID;
  g_values["device_cfg:pass1"] =
      DEVICE_CONFIG_RETIRED_DEFAULT_WIFI_PASSWORD;
  assert(load_device_config());
  assert(device_config().wifi.configured);
  assert(device_config().wifi.credential_count == 1);
  assert(device_config().wifi.credentials[0].ssid ==
         DEVICE_CONFIG_LEGACY_DEFAULT_WIFI_SSID);
  assert(g_values.count("device_cfg:snapshot") == 1);
  assert(persisted_config().wifi.credential_count == 1);
  assert(g_values.find("device_cfg:ssid1") == g_values.end());

  const DeviceWifiCredential explicit_legacy_network[] = {
      {DEVICE_CONFIG_LEGACY_DEFAULT_WIFI_SSID,
       DEVICE_CONFIG_LEGACY_DEFAULT_WIFI_PASSWORD}};
  assert(save_device_wifi_credentials(explicit_legacy_network, 1));
  assert(device_config().wifi.credential_count == 1);
  assert(device_config().wifi.credentials[0].ssid ==
         DEVICE_CONFIG_LEGACY_DEFAULT_WIFI_SSID);
  assert(g_values.count("device_cfg:snapshot") == 1);
  assert(persisted_config().wifi.credentials[0].ssid ==
         DEVICE_CONFIG_LEGACY_DEFAULT_WIFI_SSID);
  initialize_device_config();
  assert(load_device_config());
  assert(device_config().wifi.credential_count == 1);
  assert(device_config().wifi.credentials[0].ssid ==
         DEVICE_CONFIG_LEGACY_DEFAULT_WIFI_SSID);

  const DeviceWifiCredential user_credentials[] = {
      {"user-1", "password-1"}, {"user-2", "password-2"},
      {"user-3", "password-3"}, {"user-4", "password-4"},
      {"user-5", "password-5"}};
  assert(save_device_wifi_credentials(user_credentials, 5));
  assert(device_config().wifi.credential_count == kDeviceWifiCredentialMax);
  for (uint8_t index = 0; index < kDeviceWifiCredentialMax; ++index) {
    assert(device_config().wifi.credentials[index].ssid ==
           user_credentials[index].ssid);
  }

  assert(save_device_wifi_last_success_index(2));
  assert(device_config().wifi.last_success_index == 2);
  assert(save_device_wifi_last_success_index(4));
  assert(device_config().wifi.last_success_index == 4);

  assert(clear_device_wifi_credentials());
  assert(!device_config().wifi.configured);
  assert(device_config().wifi.credential_count == 0);
  assert(persisted_config().wifi.credential_count == 0);
  assert(g_values.find("device_cfg:ssid0") == g_values.end());

  assert(clear_device_wifi_credentials());
  assert(!device_config().wifi.configured);
  assert(device_config().wifi.credential_count == 0);
  assert(persisted_config().wifi.credential_count == 0);
  assert(!persisted_config().wifi.configured);
  assert(g_values.find("device_cfg:ssid0") == g_values.end());

  initialize_device_config();
  assert(load_device_config());
  assert(!device_config().wifi.configured);
  assert(device_config().wifi.credential_count == 0);
  assert(!persisted_config().wifi.configured);

  assert(save_device_wifi_credentials(user_credentials, 1));
  assert(device_config().wifi.configured);
  assert(device_config().wifi.credential_count == 1);
  assert(g_values.find("device_cfg:wifi_factory") == g_values.end());
  assert(save_device_identity_config("Student 42", "Student Pet"));
  const std::string retained_uuid = device_config().identity.local_device_uuid;
  assert(device_config().cloud.configured);
  assert(!device_config().cloud.credential.empty());
  assert(factory_reset_device_local_config());
  assert(!device_config().wifi.configured);
  assert(device_config().identity.local_device_uuid == retained_uuid);
  assert(device_config_name_is_default());
  assert(device_config().identity.provision_ap_name ==
         device_config_default_provision_ap_base_name());
  assert(!device_config().cloud.configured);
  assert(device_config().cloud.environment.empty());
  assert(device_config().cloud.credential.empty());
  assert(g_values.find("device_cfg:cloud_env") == g_values.end());
  assert(g_values.find("device_cfg:cloud_cred") == g_values.end());

  // A failed replacement cannot persist half of a Wi-Fi pair or change RAM.
  assert(save_device_wifi_credentials(user_credentials, 2));
  assert(save_device_cloud_config("development", "retained-cloud-credential"));
  const DeviceConfig held = device_config();
  const auto committed = g_values.at("device_cfg:snapshot");
  fail_record_write = true;
  const DeviceWifiCredential replacement[] = {{"replacement", "new-password"}};
  assert(!save_device_wifi_credentials(replacement, 1));
  assert(!save_device_wifi_last_success_index(1));
  assert(!factory_reset_device_local_config());
  assert(g_values.at("device_cfg:snapshot") == committed);
  assert(device_config().wifi.last_success_index == held.wifi.last_success_index);
  fail_record_write = false;
  initialize_device_config();
  assert(load_device_config());
  assert(device_config().wifi.credentials[0].ssid == "user-1");
  assert(device_config().wifi.credentials[0].password == "password-1");
  assert(device_config().cloud.credential == held.cloud.credential);
  assert(save_device_wifi_credentials(replacement, 1));
  assert(held.wifi.credentials[0].ssid == "user-1"); // snapshots do not alias
  initialize_device_config();
  assert(load_device_config());
  assert(device_config().wifi.credentials[0].ssid == "replacement");
  assert(device_config().wifi.credentials[0].password == "new-password");

  // Failed first migration leaves the old schema intact and is retryable.
  initialize_device_config();
  seed_previous_schema("6");
  const auto legacy = g_values;
  fail_record_write = true;
  assert(!load_device_config());
  assert(g_values == legacy);
  fail_record_write = false;
  assert(load_device_config());
  const auto good_record = g_values.at("device_cfg:snapshot");
  const auto writes_before = record_writes;
  fail_record_read = true;
  assert(!load_device_config());
  fail_record_read = false;
  assert(record_writes == writes_before);
  // Corrupt records fail closed, even if stale legacy credentials exist.
  g_values["device_cfg:ssid0"] = "stale-wifi";
  g_values["device_cfg:snapshot"][12] ^= 1;
  assert(!load_device_config());
  assert(record_writes == writes_before);
  g_values["device_cfg:snapshot"] = good_record;
  assert(load_device_config());
  assert(device_config().wifi.credentials[0].ssid == "preserved-wifi");
  assert(g_values.count("device_cfg:ssid0") == 0);

  // Slow Flash must not block readers or expose the uncommitted candidate.
  const DeviceConfig before_concurrent = device_config();
  pause_record_write.store(true);
  TestThread wifi_thread(concurrent_wifi_writer);
  await_flag(record_write_entered);
  assert(device_config().wifi.credentials[0].ssid == before_concurrent.wifi.credentials[0].ssid);
  TestThread cloud_thread(concurrent_cloud_writer);
  release_record_write.store(true);
  wifi_thread.join();
  cloud_thread.join();
  assert(device_config().wifi.credentials[0].ssid == "concurrent-wifi");
  assert(device_config().cloud.credential == "concurrent-credential");
  initialize_device_config();
  assert(load_device_config());
  assert(device_config().wifi.credentials[0].ssid == "concurrent-wifi");
  assert(device_config().cloud.credential == "concurrent-credential");

  // Credentials are byte-exact across save, reload and legacy migration.
  const DeviceWifiCredential exact[] = {
      {std::string(32, 's'), std::string(64, 'a')},
      {" Lab ", " secret "}, {"Lab", "other"}, {" ", ""}};
  assert(save_device_wifi_credentials(exact, 4));
  initialize_device_config(); assert(load_device_config());
  for (unsigned i = 0; i < 4; ++i) {
    assert(device_config().wifi.credentials[i].ssid == exact[i].ssid);
    assert(device_config().wifi.credentials[i].password == exact[i].password);
  }
  const DeviceWifiCredential oversized[] = {{std::string(33, 's'), ""}};
  assert(!save_device_wifi_credentials(oversized, 1));
  seed_previous_schema("7");
  g_values["device_cfg:ssid0"] = " Legacy ";
  assert(load_device_config());
  assert(device_config().wifi.credentials[0].ssid == " Legacy ");

  // Maximum supported record, including non-ASCII and escaping characters.
  DeviceConfig maximum = device_config();
  maximum.wifi.credential_count = kDeviceWifiCredentialMax;
  maximum.wifi.configured = true;
  for (auto &entry : maximum.wifi.credentials) {
    entry.ssid = std::string(kDeviceWifiSsidMax, 's');
    entry.password = std::string(kDeviceWifiPasswordMax, 'p');
  }
  maximum.identity.name = std::string(kDeviceNameMax, 'n');
  maximum.identity.provision_ap_name = std::string(kDeviceProvisionApNameMax, 'a');
  maximum.cloud = {std::string(kDeviceCloudEnvironmentMax, 'e'),
                   std::string(kDeviceCloudCredentialMax, 'c'), true};
  std::string record;
  assert(encode_device_config_record(maximum, record));
  assert(record.size() <= kDeviceConfigRecordMaxBytes);
  DeviceConfig decoded;
  assert(decode_device_config_record(record, decoded));
  assert(decoded.cloud.credential == maximum.cloud.credential);
  for (size_t length = 0; length < record.size(); ++length)
    assert(!decode_device_config_record(record.substr(0, length), decoded));
  auto wrong_version = record; wrong_version[3] = '9';
  assert(!decode_device_config_record(wrong_version, decoded));
  assert(!decode_device_config_record(std::string(kDeviceConfigRecordMaxBytes + 1, 'x'), decoded));
  maximum.identity.name = "明月\n\"设备\"";
  assert(encode_device_config_record(maximum, record));
  assert(decode_device_config_record(record, decoded));
  assert(decoded.identity.name == maximum.identity.name);
  maximum.cloud.credential.push_back('x');
  assert(!encode_device_config_record(maximum, record));

  std::cout << "Device config migration, atomic failure/reload, owned snapshots and bounded codec passed\n";
  return 0;
}
