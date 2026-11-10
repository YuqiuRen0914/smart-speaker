#pragma once
#include <cstddef>
#include <memory>
#include <string>

namespace lunaredu::lunaros {
inline constexpr size_t kNetworkTlsCaMaxBytes = 4096;
struct NetworkTlsPolicy {
  std::string origin;
  std::string mode = "standard";
  std::string ca_pem;
};
// One explicit HTTPS origin; other destinations retain system trust. Returned
// snapshots own certificate storage for the complete SDK connection lifetime.
// Reads return the last committed policy without waiting for concurrent I/O;
// cold contention/storage errors use standard verification. Invalid records
// are cached fail-closed, transient storage faults retry no faster than 5 s.
bool network_tls_normalize_origin(const std::string &, std::string &);
std::shared_ptr<const NetworkTlsPolicy> network_tls_saved_policy();
std::shared_ptr<const NetworkTlsPolicy> network_tls_policy_for_url(
    const std::string &url, const std::string &service_origin = {});
// Commit before publication; competing/reentrant writes return tls_policy_busy.
bool network_tls_save_policy(NetworkTlsPolicy, std::string &error);
bool network_tls_validate_ca(const std::string &pem);
// SDK bundle callback for this connection only; no insecure global Kconfig.
int network_tls_skip_verification_attach(void *config);
} // namespace lunaredu::lunaros
