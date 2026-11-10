#include "official_protocol.h"
#include "lunaros_provisioning.h"
#include "lunaros_network.h"
#include "lunaredu_device_profiles.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <new>
#include <mbedtls/base64.h>
#include <mbedtls/platform_util.h>
#include <protocomm.h>
#include <protocomm_security1.h>
#include <network_provisioning/network_config.h>
#include <network_config.pb-c.h>
namespace lunaredu::lunaros {
using namespace lunaredu::protocol::profiles;
namespace {
template<class T> void wipe(T &value) {
  mbedtls_platform_zeroize(&value,sizeof(value));
}
struct Scrub {void *data;size_t size;~Scrub(){mbedtls_platform_zeroize(data,size);}};
std::string base64(const uint8_t *bytes, size_t length) {
  std::array<unsigned char, 1400> buffer{}; size_t written = 0;
  const Scrub scrub{buffer.data(),buffer.size()};
  if (mbedtls_base64_encode(buffer.data(), buffer.size(), &written, bytes, length) != 0) return {};
  return {reinterpret_cast<const char *>(buffer.data()), written};
}
}
struct OfficialProvisioning::Impl {
  KernelRuntime *runtime = nullptr;
  protocomm_t *pc = nullptr;
  uint32_t nonce = 0;
  std::string proof;
  protocomm_security1_params_t security{};
  network_prov_config_handlers_t handlers{};
  network_prov_config_set_wifi_data_t pending{};
  bool staged = false;
  static Impl &self(network_prov_ctx_t **context) { return *reinterpret_cast<Impl *>(*context); }
  static esp_err_t set(const network_prov_config_set_wifi_data_t *input, network_prov_ctx_t **context) {
    auto &s = self(context);
    wipe(s.pending); s.staged = false;
    const auto ssid = std::strlen(input->ssid), password = std::strlen(input->password);
    if (!ssid || ssid > 32 || (password && (password < 8 || password > 63)) || input->channel != 0 ||
        std::any_of(std::begin(input->bssid), std::end(input->bssid), [](char b) { return b != 0; })) return ESP_ERR_INVALID_ARG;
    s.pending = *input; s.staged = true; return ESP_OK;
  }
  static esp_err_t apply(network_prov_ctx_t **context) {
    auto &s = self(context);
    if (!s.staged) return ESP_ERR_INVALID_STATE;
    const auto result = submit_provisioning_wifi_credentials(*s.runtime, s.pending.ssid, s.pending.password);
    wipe(s.pending); s.staged = false;
    return result == NetworkWifiCredentialResult::Ok ? ESP_OK : ESP_FAIL;
  }
  static esp_err_t status(network_prov_config_get_wifi_data_t *out, network_prov_ctx_t **) {
    *out = {};
    const auto progress = provisioning_progress();
    if (progress.phase == "connected" && !progress.ip.empty()) {
      out->wifi_state = NETWORK_PROV_WIFI_STA_CONNECTED;
      std::snprintf(out->conn_info.ip_addr, sizeof(out->conn_info.ip_addr), "%s", progress.ip.c_str());
      std::snprintf(out->conn_info.ssid, sizeof(out->conn_info.ssid), "%s", progress.ssid.c_str());
    } else if (progress.phase == "failed" || progress.phase == "cancelled") {
      out->wifi_state = NETWORK_PROV_WIFI_STA_DISCONNECTED;
      out->fail_reason = progress.error == "wifi_auth_failed" ? NETWORK_PROV_WIFI_STA_AUTH_ERROR : NETWORK_PROV_WIFI_STA_AP_NOT_FOUND;
    } else out->wifi_state = NETWORK_PROV_WIFI_STA_CONNECTING;
    return ESP_OK;
  }
  static esp_err_t config(uint32_t session, const uint8_t *data, ssize_t length, uint8_t **out, ssize_t *outlen, void *context) {
    auto &s = *static_cast<Impl *>(context);
    if (session != s.nonce || length <= 0 || length > 512) return ESP_ERR_INVALID_ARG;
    // Validate the exact supported oneof before dispatch. In particular, binary
    // SSIDs containing NUL must never silently become another saved network.
    auto *packet = network_config_payload__unpack(nullptr, size_t(length), data);
    if (!packet) return ESP_ERR_INVALID_ARG;
    bool valid = false;
    if (packet->msg == NETWORK_CONFIG_MSG_TYPE__TypeCmdSetWifiConfig && packet->payload_case == NETWORK_CONFIG_PAYLOAD__PAYLOAD_CMD_SET_WIFI_CONFIG && packet->cmd_set_wifi_config) {
      const auto &c = *packet->cmd_set_wifi_config;
      valid = c.ssid.len > 0 && c.ssid.len <= 32 && c.passphrase.len <= 63 && c.bssid.len == 0 && c.channel == 0 &&
        !std::memchr(c.ssid.data, 0, c.ssid.len) && (!c.passphrase.len || !std::memchr(c.passphrase.data, 0, c.passphrase.len));
    } else valid = (packet->msg == NETWORK_CONFIG_MSG_TYPE__TypeCmdApplyWifiConfig && packet->payload_case == NETWORK_CONFIG_PAYLOAD__PAYLOAD_CMD_APPLY_WIFI_CONFIG) ||
                  (packet->msg == NETWORK_CONFIG_MSG_TYPE__TypeCmdGetWifiStatus && packet->payload_case == NETWORK_CONFIG_PAYLOAD__PAYLOAD_CMD_GET_WIFI_STATUS);
    network_config_payload__free_unpacked(packet, nullptr);
    return valid ? network_prov_config_data_handler(session, data, length, out, outlen, &s.handlers) : ESP_ERR_INVALID_ARG;
  }
  ~Impl() {
    if (pc) { (void)protocomm_close_session(pc, nonce); protocomm_delete(pc); }
    wipe(pending);
    if (!proof.empty())mbedtls_platform_zeroize(proof.data(),proof.size());
  }
};
OfficialProvisioning::~OfficialProvisioning() { reset(); }
void OfficialProvisioning::reset() { delete impl_; impl_ = nullptr; }
bool OfficialProvisioning::exchange(KernelRuntime &runtime, uint32_t nonce, const std::string &proof, const char *endpoint, const std::string &input, std::string &response, std::string &error) {
  response.clear();
  auto fail = [&] { reset(); error = "official_provisioning_failed"; return false; };
  if (!nonce || proof.empty() || !endpoint || input.empty() || input.size() > 1024 ||
      (std::strcmp(endpoint, provisioning_session_endpoint) && std::strcmp(endpoint, provisioning_config_endpoint))) return fail();
  std::array<unsigned char, 768> decoded{}; size_t length = 0;
  const Scrub scrub{decoded.data(),decoded.size()};
  if (mbedtls_base64_decode(decoded.data(), decoded.size(), &length, reinterpret_cast<const unsigned char *>(input.data()), input.size()) != 0 ||
      !length || base64(decoded.data(), length) != input) return fail();
  if (!impl_) {
    if (std::strcmp(endpoint, provisioning_session_endpoint)) return fail();
    impl_ = new (std::nothrow) Impl;
    if (!impl_) return fail();
    auto &s = *impl_; s.runtime = &runtime; s.nonce = nonce; s.proof = proof;
    s.security.data = reinterpret_cast<const uint8_t *>(s.proof.data()); s.security.len = s.proof.size();
    s.handlers = {Impl::status, Impl::set, Impl::apply, reinterpret_cast<network_prov_ctx_t *>(&s)};
    s.pc = protocomm_new();
    if (!s.pc || protocomm_set_security(s.pc, provisioning_session_endpoint, &protocomm_security1, &s.security) != ESP_OK ||
        protocomm_add_endpoint(s.pc, provisioning_config_endpoint, Impl::config, &s) != ESP_OK || protocomm_open_session(s.pc, nonce) != ESP_OK) return fail();
  }
  if (impl_->nonce != nonce || impl_->runtime != &runtime) return fail();
  uint8_t *output = nullptr; ssize_t output_length = 0;
  const auto result = protocomm_req_handle(impl_->pc, endpoint, nonce, decoded.data(), length, &output, &output_length);
  if (result == ESP_OK && output && output_length > 0 && output_length <= 768) response = base64(output, output_length);
  if (output) { if(output_length>0)mbedtls_platform_zeroize(output,size_t(output_length));std::free(output); }
  if (response.empty()) return fail();
  error.clear(); return true;
}
}
