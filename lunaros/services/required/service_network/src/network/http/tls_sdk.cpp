#include "lunaros_network_tls.h"
#include <esp_err.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

namespace lunaredu::lunaros {
bool network_tls_validate_ca(const std::string &pem) {
  if (pem.empty() || pem.size() > kNetworkTlsCaMaxBytes || pem.find('\0') != std::string::npos ||
      pem.find("PRIVATE KEY") != std::string::npos || pem.find("-----BEGIN CERTIFICATE-----") == std::string::npos) return false;
  mbedtls_x509_crt chain;
  mbedtls_x509_crt_init(&chain);
  bool valid = mbedtls_x509_crt_parse(&chain, reinterpret_cast<const unsigned char *>(pem.c_str()), pem.size() + 1) == 0;
  for (auto *cert = &chain; valid && cert != nullptr; cert = cert->next) valid = mbedtls_x509_crt_get_ca_istrue(cert) != 0;
  mbedtls_x509_crt_free(&chain);
  return valid;
}
int network_tls_skip_verification_attach(void *config) {
  if (config == nullptr) return ESP_ERR_INVALID_ARG;
  mbedtls_ssl_conf_authmode(static_cast<mbedtls_ssl_config *>(config), MBEDTLS_SSL_VERIFY_NONE);
  return ESP_OK;
}
} // namespace lunaredu::lunaros
