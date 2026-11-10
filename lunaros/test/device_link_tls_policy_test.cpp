#include "device_link/device_link_tls_policy.h"

using lunaredu::lunaros::device_link::DeviceLinkTlsAdmission;
using lunaredu::lunaros::device_link::device_link_tls_admission;
using lunaredu::lunaros::kNetworkMinimumTlsBlockBytes;

// Keep these checks compile-time so the policy can also be validated with the
// ESP cross compiler on hosts that do not have a native C++ toolchain.
static_assert(device_link_tls_admission(false, 0, false) ==
              DeviceLinkTlsAdmission::Direct);

// Production work with the measured TLS scratch block must preserve the
// resident XiaoZhi session instead of suspending it on every poll.
static_assert(
    device_link_tls_admission(true, kNetworkMinimumTlsBlockBytes, false) ==
    DeviceLinkTlsAdmission::Direct);
static_assert(
    device_link_tls_admission(true, kNetworkMinimumTlsBlockBytes + 1, true) ==
    DeviceLinkTlsAdmission::Direct);

// Low-memory periodic work backs off; an explicit interactive operation may
// temporarily enter product cloud maintenance to complete recovery.
static_assert(
    device_link_tls_admission(true, kNetworkMinimumTlsBlockBytes - 1, false) ==
    DeviceLinkTlsAdmission::Defer);
static_assert(
    device_link_tls_admission(true, kNetworkMinimumTlsBlockBytes - 1, true) ==
    DeviceLinkTlsAdmission::Maintenance);

int main() {}
