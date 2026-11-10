#include "lunaros_platform_link_connection.h"
#include "lunaros_network.h"
#include "platform_link_scheduler.h"
#include "platform_link_state.h"

namespace lunaredu::lunaros {
namespace {
PlatformLinkConnectionFailure failure_for(const PlatformLinkPlatformConnectionSnapshot &observed) {
  using Failure = PlatformLinkConnectionFailure;
  const int status = observed.status_code;
  if (status == 401 || status == 403) return Failure::Authentication;
  if (status == 409) return Failure::IdentityConflict;
  if (status == 404) return Failure::EndpointNotFound;
  if (status == 429) return Failure::RateLimited;
  if (status >= 500 && status <= 599) return Failure::ServiceUnavailable;
  if (status >= 400 && status <= 499) return Failure::RequestRejected;
  if (observed.error_code == "platform_response_invalid") return Failure::InvalidResponse;
  if (observed.error.find("ESP_ERR_TIMEOUT") != std::string::npos ||
      observed.error.find("timeout") != std::string::npos) return Failure::Timeout;
  if (observed.error.find("certificate") != std::string::npos ||
      observed.error.find("X509") != std::string::npos ||
      observed.error.find("ESP_ERR_MBEDTLS") != std::string::npos)
    return Failure::Security;
  if (!observed.error.empty() || !observed.error_code.empty() ||
      observed.state == PlatformLinkPlatformConnectionState::Error) return Failure::Transport;
  return Failure::None;
}
}

PlatformLinkConnectionStatus platform_link_connection_status() {
  using Phase = PlatformLinkConnectionPhase;
  PlatformLinkConnectionStatus status;
  const auto network = network_fast_snapshot();
  if (network.link != NetworkLinkState::Up || !network.station_has_address) {
    status.phase = Phase::Offline;
    return status;
  }
  const auto observed = platform_link::platform_connection_last_observation();
  status.http_status = observed.status_code;
  if (observed.state == PlatformLinkPlatformConnectionState::Connected) {
    status.phase = platform_link::platform_connection_state() == PlatformLinkPlatformConnectionState::Connected
        ? Phase::Connected : Phase::Connecting;
    return status;
  }
  status.failure = failure_for(observed);
  if (status.failure == PlatformLinkConnectionFailure::None) return status;
  const auto retry = platform_link::connection_retry_snapshot();
  status.retry_wait_ms = retry.wait_ms;
  status.phase = retry.running ? Phase::Retrying : retry.queued ? Phase::RetryQueued
      : retry.wait_ms ? Phase::RetryWaiting : Phase::Failed;
  return status;
}

const char *platform_link_connection_phase_name(PlatformLinkConnectionPhase phase) {
  using Phase = PlatformLinkConnectionPhase;
  switch (phase) {
  case Phase::Offline: return "offline";
  case Phase::Connecting: return "connecting";
  case Phase::Connected: return "connected";
  case Phase::Failed: return "failed";
  case Phase::RetryWaiting: return "retry_waiting";
  case Phase::RetryQueued: return "retry_queued";
  case Phase::Retrying: return "retrying";
  }
  return "connecting";
}

const char *platform_link_connection_failure_name(PlatformLinkConnectionFailure failure) {
  using Failure = PlatformLinkConnectionFailure;
  switch (failure) {
  case Failure::None: return "";
  case Failure::Timeout: return "timeout";
  case Failure::Security: return "security";
  case Failure::Authentication: return "authentication";
  case Failure::IdentityConflict: return "identity_conflict";
  case Failure::EndpointNotFound: return "endpoint_not_found";
  case Failure::RateLimited: return "rate_limited";
  case Failure::ServiceUnavailable: return "service_unavailable";
  case Failure::RequestRejected: return "request_rejected";
  case Failure::InvalidResponse: return "invalid_response";
  case Failure::Transport: return "transport";
  }
  return "transport";
}

std::string platform_link_connection_status_text(const PlatformLinkConnectionStatus &status) {
  using Phase = PlatformLinkConnectionPhase;
  using Failure = PlatformLinkConnectionFailure;
  if (status.phase == Phase::Offline) return "正在连接 Wi-Fi";
  if (status.phase == Phase::Connected) return "平台已连接";
  if (status.failure == Failure::None) return "正在连接平台";
  std::string text;
  switch (status.failure) {
  case Failure::Timeout: text = "平台连接超时"; break;
  case Failure::Security: text = "平台安全连接失败"; break;
  case Failure::Authentication: text = "平台认证失败"; break;
  case Failure::IdentityConflict: text = "平台设备身份冲突"; break;
  case Failure::EndpointNotFound: text = "平台接口不可用"; break;
  case Failure::RateLimited: text = "平台请求过于频繁"; break;
  case Failure::ServiceUnavailable: text = "平台服务异常"; break;
  case Failure::RequestRejected: text = "平台请求被拒绝"; break;
  case Failure::InvalidResponse: text = "平台响应异常"; break;
  default: text = "平台连接失败"; break;
  }
  if (status.http_status >= 400 && status.http_status <= 599)
    text += "（HTTP " + std::to_string(status.http_status) + "）";
  if (status.phase == Phase::Retrying) return text + "\n正在重试";
  if (status.phase == Phase::RetryQueued) return text + "\n等待重试执行";
  if (status.phase == Phase::RetryWaiting && status.retry_wait_ms)
    return text + "\n重试等待 " + std::to_string((uint64_t(status.retry_wait_ms) + 999) / 1000) + " 秒";
  return text + "\n等待重试";
}

}
