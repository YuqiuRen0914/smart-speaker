#include "app/pairing_guidance.h"
#include "kernel/retry_schedule.h"
#include "lunaros_network.h"
#include "lunaros_platform_link_pairing_activity.h"
#include "lunaros_platform_link_connection.h"

namespace lunaredu::pet {
namespace {
struct PairingGuidance {
  lunaros::PlatformLinkPairingActivityHandle activity;
  lunaros::RetrySchedule retry{5000, 60000};
  std::string caption;
  uint64_t next_poll = 0, code_expires = 0, rate_limit_until = 0;
  int binding_epoch = -1;
  bool attempt_pending = false, blocked = false, cancelled = false;

  void failed(const std::string &error, uint64_t now) {
    retry.failed(static_cast<uint32_t>(now));
    if (error == "pairing_rate_limited") rate_limit_until = now + 60000;
    blocked = error == "device_already_bound" || error == "device_retired" ||
        error == "device_credential_invalid" || error == "identity_save_failed";
    caption = blocked ? "请在平台检查设备绑定" : "绑定码申请失败，稍后重试";
    attempt_pending = false;
  }
};
PairingGuidance guidance;
}

bool stop_pairing_guidance(std::string &error) {
  if (guidance.activity.generation &&
      !lunaros::cancel_platform_link_pairing_activity("pet", guidance.activity, error) &&
      error != "stale_pairing_activity") return false;
  guidance = {};
  error.clear();
  return true;
}

const std::string &pairing_guidance_caption(bool enabled, uint64_t now_ms) {
  using namespace lunaros;
  if (!enabled) {
    guidance = {};
    return guidance.caption;
  }
  const auto binding = platform_link_binding_status();
  if (guidance.binding_epoch >= 0 && guidance.binding_epoch != binding.binding_epoch) {
    guidance = {};
  }
  guidance.binding_epoch = binding.binding_epoch;
  if (binding.persistence_pending) {
    guidance.caption = "正在保存绑定状态";
    guidance.code_expires = 0;
    return guidance.caption;
  }
  if (binding.revocation_requested) {
    guidance = {};
    return guidance.caption = "正在同步解绑";
  }
  if (binding.state == PlatformLinkBindingState::Bound) {
    guidance = {};
    return guidance.caption;
  }
  const auto network = network_fast_snapshot();
  if (network.link != NetworkLinkState::Up || !network.station_has_address) {
    guidance.caption = "正在连接 Wi-Fi";
    guidance.code_expires = 0;
    guidance.next_poll = 0;
    return guidance.caption;
  }
  if (guidance.code_expires && now_ms >= guidance.code_expires) {
    guidance.caption = "绑定码已过期，正在重新申请";
    guidance.code_expires = 0;
    guidance.next_poll = 0;
  }
  if (now_ms < guidance.next_poll) return guidance.caption;
  guidance.next_poll = now_ms + 500;
  const auto connection = platform_link_connection_status();
  if (guidance.cancelled) {
    if (!platform_link_pairing_pending() && binding.state != PlatformLinkBindingState::Pairing)
      return guidance.caption = "绑定操作已取消";
    guidance.activity = {};
    guidance.cancelled = false;
    guidance.retry.reset();
  }
  PlatformLinkPairingActivitySnapshot observed;
  std::string error;
  if (guidance.activity.generation &&
      read_platform_link_pairing_activity("pet", guidance.activity, observed, error)) {
    if (observed.phase == PlatformLinkPairingActivityPhase::Waiting &&
        observed.remaining_ms && !observed.pairing.code.empty()) {
      if (connection.phase != PlatformLinkConnectionPhase::Connected) {
        guidance.code_expires = 0;
        return guidance.caption = platform_link_connection_status_text(connection);
      }
      guidance.caption = "绑定码 " + observed.pairing.code;
      guidance.code_expires = now_ms + observed.remaining_ms;
      guidance.attempt_pending = false;
      guidance.blocked = false;
      guidance.retry.reset();
      return guidance.caption;
    }
    if (observed.phase == PlatformLinkPairingActivityPhase::Preparing) {
      if (connection.failure != PlatformLinkConnectionFailure::None)
        return guidance.caption = platform_link_connection_status_text(connection);
      guidance.caption = "正在申请绑定码";
      return guidance.caption;
    }
    if (observed.phase == PlatformLinkPairingActivityPhase::Cancelling)
      return guidance.caption = "正在结束绑定操作";
    if (observed.phase == PlatformLinkPairingActivityPhase::Succeeded) {
      guidance.caption.clear();
      return guidance.caption;
    }
    if (observed.phase == PlatformLinkPairingActivityPhase::Cancelled) {
      guidance.cancelled = true;
      guidance.attempt_pending = false;
      guidance.code_expires = 0;
      return guidance.caption = "绑定操作已取消";
    }
    if (observed.error == "pairing_code_expired") {
      guidance.retry.reset();
      guidance.attempt_pending = false;
      guidance.caption = "绑定码已过期，正在重新申请";
    } else if (guidance.attempt_pending)
      guidance.failed(observed.error, now_ms);
  } else if (guidance.activity.generation) {
    guidance.activity = {};
    if (guidance.attempt_pending) guidance.failed(error, now_ms);
  }
  if (guidance.blocked && binding.state != PlatformLinkBindingState::Pairing)
    return guidance.caption;
  if (connection.phase != PlatformLinkConnectionPhase::Connected)
    return guidance.caption = platform_link_connection_status_text(connection);
  if (!platform_link_local_authority_ready())
    return guidance.caption = "正在同步设备状态";
  if (!guidance.retry.ready(static_cast<uint32_t>(now_ms)) || now_ms < guidance.rate_limit_until)
    return guidance.caption = "绑定码申请失败，稍后重试";
  if (begin_platform_link_pairing_activity("pet", guidance.activity, error, true)) {
    guidance.attempt_pending = true;
    guidance.caption = "正在申请绑定码";
    guidance.code_expires = 0;
  } else guidance.failed(error, now_ms);
  return guidance.caption;
}
}
