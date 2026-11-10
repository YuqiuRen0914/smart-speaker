#include "interaction/conversation/assistant_lifecycle.h"
#include "interaction/conversation/assistant_loop.h"
#include "interaction/conversation/assistant_core.h"
#include "interaction/conversation/assistant_client_id.h"
#include "interaction/transport/assistant_transport.h"
#include "interaction/conversation/assistant_session.h"
#include "interaction/voice/assistant_audio.h"
#include "bsp/lunaredu_bsp.h"
#include "lunaredu_audio_bridge.h"
#include "lunaredu_lunaros/kernel/managed_task.h"
#include "lunaredu_lunaros/network/network_jobs.h"
#include "lunaredu_lunaros/persistence/system_info.h"
#include "lunaredu_lunaros/kernel/task_registry.h"
#include "interaction/conversation/session_policy.h"
#include "interaction/conversation/host_events.h"

#include <algorithm>
#include <cstring>
#include <esp_heap_caps.h>

namespace lunaredu::pet::conversation::internal {

namespace {

constexpr uint32_t kActivationPendingRetryMs = 5000;
constexpr uint32_t kActivationFailureRetryMinMs = 2000;
constexpr uint32_t kActivationFailureRetryMaxMs = 30000;
constexpr uint32_t kWorkerSetupRetryMs = 2000;
int64_t g_worker_setup_retry_at_ms = 0;

bool managed_worker_present(
    const lunaredu::lunaros::ManagedTaskRegistrySnapshot &snapshot,
    const char *id, bool require_started) {
  for (const auto &task : snapshot.tasks) {
    if (!task.active ||
        std::strcmp(task.owner_service_id.data(), "assistant.xiaozhi") != 0 ||
        std::strcmp(task.id.data(), id) != 0) {
      continue;
    }
    return !require_started || task.native_handle != 0;
  }
  return false;
}

bool worker_registry_ready(
    const lunaredu::lunaros::ManagedTaskRegistrySnapshot &snapshot) {
  return managed_worker_present(snapshot, "assistant_audio", true) &&
         managed_worker_present(snapshot, "connect_task", true);
}

bool ensure_assistant_worker_tasks(bool force) {
  auto task_snapshot = lunaredu::lunaros::managed_task_registry_snapshot();
  bool queue_ready = false;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    queue_ready = g_runtime.downlink_audio_queue != nullptr;
  }
  if (queue_ready && worker_registry_ready(task_snapshot)) {
    g_worker_setup_retry_at_ms = 0;
    return true;
  }

  const int64_t current_ms = now_ms();
  if (!force && g_worker_setup_retry_at_ms > current_ms) {
    return false;
  }
  g_worker_setup_retry_at_ms = current_ms + kWorkerSetupRetryMs;

  // A task can exit after create_managed_task returned successfully.  Treat
  // the registry as the authority and clear the stale opaque FreeRTOS handle
  // before attempting a self-healing restart.
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (g_runtime.audio_task != nullptr &&
        !managed_worker_present(task_snapshot, "assistant_audio", false)) {
      g_runtime.audio_task = nullptr;
    }
    if (g_runtime.connect_task != nullptr &&
        !managed_worker_present(task_snapshot, "connect_task", false)) {
      g_runtime.connect_task = nullptr;
    }
  }

  bool create_audio = false;
  bool create_connect = false;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    create_audio = g_runtime.audio_task == nullptr;
    if (g_runtime.downlink_audio_queue == nullptr) {
      g_runtime.downlink_audio_queue = xQueueCreate(
          kDownlinkAudioQueueDepth, sizeof(DownlinkAudioWorkItem *));
      if (g_runtime.downlink_audio_queue == nullptr) {
        ESP_LOGE(kTag, "Failed to create downlink audio queue");
      }
    }
    create_connect = g_runtime.connect_task == nullptr;
  }

  auto create_worker = [](const lunaredu::lunaros::ManagedTaskDefinition &definition,
                          lunaredu::lunaros::ManagedTaskFunction function,
                          TaskHandle_t &destination) {
    TaskHandle_t created_task = nullptr;
    std::string task_error;
    if (!lunaredu::lunaros::create_managed_task(
            definition, function, nullptr, &created_task, task_error)) {
      ESP_LOGE(kTag, "Failed to create %s worker: %s", definition.id,
               task_error.c_str());
      return false;
    }
    {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      destination = created_task;
    }
    ESP_LOGI(kTag, "Created %s worker", definition.id);
    return true;
  };

  if (create_audio) {
    ESP_LOGI(kTag, "audio_task create begin stack=%u task_caps=PSRAM",
             static_cast<unsigned>(kAudioTaskStackSize));
    const lunaredu::lunaros::ManagedTaskDefinition definition{
        .id = "assistant_audio",
        .owner_service_id = "assistant.xiaozhi",
        .role = "audio_io",
        .stack_bytes = kAudioTaskStackSize,
        .priority = kAudioTaskPriority,
        .core_affinity = -1,
        .lifetime = lunaredu::lunaros::ManagedTaskLifetime::Resident,
        .memory = lunaredu::lunaros::ManagedTaskMemory::External,
        .allow_internal_fallback = false,
    };
    (void)create_worker(definition, audio_task, g_runtime.audio_task);
  }

  if (create_connect) {
    const lunaredu::lunaros::ManagedTaskDefinition definition{
        .id = "connect_task",
        .owner_service_id = "assistant.xiaozhi",
        .role = "session_connect",
        .stack_bytes = kConnectTaskStackSize,
        .priority = 2,
        .core_affinity = -1,
        .lifetime = lunaredu::lunaros::ManagedTaskLifetime::Resident,
        .memory = lunaredu::lunaros::ManagedTaskMemory::External,
        .allow_internal_fallback = false,
    };
    (void)create_worker(definition, connect_task, g_runtime.connect_task);
  }

  task_snapshot = lunaredu::lunaros::managed_task_registry_snapshot();
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    queue_ready = g_runtime.downlink_audio_queue != nullptr;
  }
  const bool ready = queue_ready && worker_registry_ready(task_snapshot);
  if (ready) {
    g_worker_setup_retry_at_ms = 0;
  }
  return ready;
}

uint32_t activation_retry_delay_ms(uint32_t attempts) {
  const uint32_t shift = std::min<uint32_t>(attempts, 4);
  return std::min<uint32_t>(kActivationFailureRetryMaxMs,
                            kActivationFailureRetryMinMs << shift);
}

void apply_pending_activation_result_locked() {
  if (!g_runtime.activation.result_available) {
    return;
  }

  if (g_runtime.activation.user_blocked) {
    g_runtime.activation.result = XiaozhiActivationResult();
    g_runtime.activation.result_available = false;
    g_runtime.activation.result_ok = false;
    g_runtime.activation.request_inflight = false;
    ESP_LOGI(kTag, "activation result discarded after local unbind");
    return;
  }

  const bool ok = g_runtime.activation.result_ok;
  XiaozhiActivationResult result = std::move(g_runtime.activation.result);
  g_runtime.activation.result = XiaozhiActivationResult();
  g_runtime.activation.result_available = false;
  g_runtime.activation.result_ok = false;

  if (ok) {
    const bool pairing_evidence = !result.activation_code.empty() ||
                                  !result.activation_challenge.empty();
    if (g_runtime.activation.fresh_pairing_required && pairing_evidence) {
      g_runtime.activation.fresh_pairing_observed = true;
      g_runtime.activation.remote_binding_still_active = false;
    }
    if (g_runtime.activation.fresh_pairing_required && result.has_mqtt &&
        !g_runtime.activation.fresh_pairing_observed) {
      // The official service still recognizes this hardware Device-Id and
      // returned the former account's transport credentials without issuing a
      // new pairing challenge. Never persist or use those credentials after a
      // factory reset/local unbind.
      g_runtime.activation.pending = false;
      g_runtime.activation.configured = false;
      g_runtime.activation.ready = false;
      g_runtime.activation.user_blocked = true;
      g_runtime.activation.remote_binding_still_active = true;
      g_runtime.activation.next_attempt_at_ms = 0;
      g_runtime.diagnostics.last_error = kRemoteBindingStillActiveError;
      g_runtime.diagnostics.last_transport_error = result.transport_error;
      g_runtime.activation.request_inflight = false;
      persist_state_locked();
      conversation_emit_status("小智后台仍有旧授权",
                             "控制台删除不等于后台已释放",
                             "稍后重试或联系小智官方", nullptr);
      ESP_LOGW(kTag,
               "rejected XiaoZhi credentials without fresh pairing proof; remote binding still active");
      return;
    }
    apply_activation_result_locked(result);
    if (g_runtime.activation.ready &&
        g_runtime.activation.fresh_pairing_required &&
        g_runtime.activation.fresh_pairing_observed) {
      g_runtime.activation.fresh_pairing_required = false;
      g_runtime.activation.fresh_pairing_observed = false;
      g_runtime.activation.remote_binding_still_active = false;
      persist_state_locked();
    }
    g_runtime.diagnostics.last_transport_error = result.transport_error;
    g_runtime.diagnostics.last_error = result.error;
    if (g_runtime.activation.ready) {
      g_runtime.activation.attempts = 0;
      g_runtime.activation.next_attempt_at_ms = 0;
    } else {
      g_runtime.activation.next_attempt_at_ms =
          now_ms() + static_cast<int64_t>(kActivationPendingRetryMs);
    }
  } else {
    g_runtime.activation.pending = false;
    g_runtime.diagnostics.last_error =
        result.error.empty() ? "activation_request_failed" : result.error;
    g_runtime.diagnostics.last_transport_error = result.transport_error;
    g_runtime.activation.next_attempt_at_ms =
        now_ms() + static_cast<int64_t>(
                       activation_retry_delay_ms(g_runtime.activation.attempts));
  }

  g_runtime.activation.request_inflight = false;
  ESP_LOGI(kTag,
           "activation result applied ok=%d ready=%d pending=%d configured=%d code_present=%d next_retry_ms=%" PRId64 " error=%s transport=%s",
           ok, g_runtime.activation.ready, g_runtime.activation.pending,
           g_runtime.activation.configured, !g_runtime.activation.code.empty(),
           g_runtime.activation.next_attempt_at_ms > 0
               ? (g_runtime.activation.next_attempt_at_ms - now_ms())
               : 0,
           g_runtime.diagnostics.last_error.empty()
               ? "-"
               : g_runtime.diagnostics.last_error.c_str(),
           g_runtime.diagnostics.last_transport_error.empty()
               ? "-"
               : g_runtime.diagnostics.last_transport_error.c_str());
}

lunaredu::lunaros::NetworkJobRunResult run_activation_job(
    lunaredu::lunaros::NetworkJobHandle, void *) {
  XiaozhiActivationRequest request;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (!g_runtime.service_enabled || g_runtime.service_suspended ||
        g_runtime.activation.ready || g_runtime.activation.user_blocked ||
        !g_runtime.activation.request_inflight) {
      XiaozhiActivationResult result;
      result.error = "activation_no_longer_required";
      g_runtime.activation.result = std::move(result);
      g_runtime.activation.result_ok = false;
      g_runtime.activation.result_available = true;
      return {false, false, "activation_no_longer_required"};
    }
    const bsp_board_info_t *board_info = bsp_board_get_info();
    request.ota_url = kDefaultAssistantOtaUrl;
    request.board_name =
        (board_info != nullptr && board_info->board_type != nullptr)
            ? board_info->board_type
            : "lunaredu_s3_n16r8_baseboard";
    request.device_id = g_runtime.device_id;
    request.client_id = g_runtime.client_id;
    request.activation_timeout_ms = g_runtime.activation.timeout_ms;
  }

  XiaozhiActivationResult result;
  std::string time_sync_error;
  const bool time_synchronized = assistant_synchronize_network_clock(
      kSystemTimeSyncTimeoutMs, time_sync_error);
  bool ok = false;
  if (!time_synchronized) {
    result.error = "system_time_sync_failed";
    result.transport_error = time_sync_error.empty() ? "clock_sync_failed"
                                                      : time_sync_error;
    ESP_LOGW(kTag, "activation postponed until system time is valid: %s",
             result.transport_error.c_str());
  } else {
    ok = xiaozhi_activate_device(request, result);
  }

  std::string detail = ok ? "activation_request_complete"
                          : (result.error.empty() ? "activation_request_failed"
                                                  : result.error);
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (g_runtime.activation.user_blocked) {
      g_runtime.activation.request_inflight = false;
      return {false, false, "activation_cancelled_by_local_unbind"};
    }
    g_runtime.activation.result = std::move(result);
    g_runtime.activation.result_ok = ok;
    g_runtime.activation.result_available = true;
    ESP_LOGI(kTag,
             "activation request done ok=%d ready=%d pending=%d configured=%d code_present=%d result_queued=1 error=%s transport=%s",
             ok, g_runtime.activation.ready, g_runtime.activation.pending,
             g_runtime.activation.configured,
             !g_runtime.activation.code.empty(),
             g_runtime.activation.result.error.empty()
                 ? "-"
                 : g_runtime.activation.result.error.c_str(),
             g_runtime.activation.result.transport_error.empty()
                 ? "-"
                 : g_runtime.activation.result.transport_error.c_str());
  }
  return {ok, false, std::move(detail)};
}

void maybe_submit_activation_locked(int64_t now) {
  if (!assistant_network_connected() || g_runtime.activation.user_blocked ||
      g_runtime.activation.ready ||
      g_runtime.activation.request_inflight ||
      g_runtime.activation.result_available ||
      (g_runtime.activation.next_attempt_at_ms > 0 &&
       now < g_runtime.activation.next_attempt_at_ms)) {
    return;
  }

  g_runtime.activation.pending = true;
  g_runtime.activation.request_inflight = true;
  ++g_runtime.activation.attempts;
  std::string error;
  lunaredu::lunaros::NetworkJobHandle handle;
  if (!lunaredu::lunaros::submit_network_job(
          {"assistant.xiaozhi", "activation",
           lunaredu::lunaros::kNetworkPriorityInteractive, 1, 0,
           run_activation_job, nullptr, false},
          handle, error)) {
    g_runtime.activation.pending = false;
    g_runtime.activation.request_inflight = false;
    g_runtime.activation.next_attempt_at_ms =
        now + static_cast<int64_t>(
                  activation_retry_delay_ms(g_runtime.activation.attempts));
    g_runtime.diagnostics.last_error =
        error.empty() ? "activation_queue_failed" : error;
    ESP_LOGW(kTag, "failed to queue activation request: %s",
             g_runtime.diagnostics.last_error.c_str());
    return;
  }
  ESP_LOGI(kTag,
           "activation request queued job=%" PRIu32 " attempt=%" PRIu32
           " ready=%d pending=%d configured=%d code_present=%d",
           handle.id, g_runtime.activation.attempts, g_runtime.activation.ready,
           g_runtime.activation.pending, g_runtime.activation.configured,
           !g_runtime.activation.code.empty());
}

void wake_word_init_task(void *argument) {
  (void)argument;
  bool wake_word_enabled = false;
  std::string requested_wake_word;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    wake_word_enabled = g_runtime.wake_word.enabled;
    requested_wake_word = g_runtime.wake_word.requested_word;
  }

  ESP_LOGI(kTag,
           "wake frontend init begin wake_word=%d requested=%s heap=%s",
           wake_word_enabled,
           requested_wake_word.empty() ? "-" : requested_wake_word.c_str(),
           heap_snapshot_text().c_str());

  bool initialized = false;
  lunaredu::pet::conversation::WakeWordFrontendStatus status;
  {
    std::lock_guard<std::mutex> lock(g_runtime.wake_word_mutex);
    initialized = g_runtime.wake_word_frontend.ensure_initialized(
        wake_word_enabled, requested_wake_word);
    status = g_runtime.wake_word_frontend.status();
  }

  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    refresh_wake_word_status_locked(status);
    g_runtime.wake_word.afe_init_in_progress = false;
    g_runtime.wake_word.afe_init_attempts += 1;
    g_runtime.wake_word.afe_next_init_at_ms =
        initialized ? 0 : now_ms() + static_cast<int64_t>(kWakeWordInitRetryMs);
    g_runtime.wake_word_init_task = nullptr;
    if (!initialized && g_runtime.wake_word.afe_last_error.empty()) {
      g_runtime.wake_word.afe_last_error = "wake_word_frontend_init_failed";
    }
  }

  ESP_LOGI(kTag,
           "wake frontend init done ok=%d initialized=%d wake_word=%d matched=%d error=%s heap=%s",
           initialized, status.initialized, status.wake_word_enabled,
           status.requested_wake_word_available,
           status.last_error.empty() ? "-" : status.last_error.c_str(),
           heap_snapshot_text().c_str());
}

void wake_word_release_task(void *argument) {
  (void)argument;
  ESP_LOGI(kTag, "wake frontend release begin heap=%s",
           heap_snapshot_text().c_str());

  lunaredu::pet::conversation::WakeWordFrontendStatus status;
  {
    std::lock_guard<std::mutex> lock(g_runtime.wake_word_mutex);
    g_runtime.wake_word_frontend.release();
    status = g_runtime.wake_word_frontend.status();
  }

  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    refresh_wake_word_status_locked(status);
    g_runtime.wake_word.afe_release_in_progress = false;
    g_runtime.wake_word_release_task = nullptr;
  }
  ESP_LOGI(kTag, "wake frontend release done initialized=%d heap=%s",
           status.initialized, heap_snapshot_text().c_str());
}

bool request_wake_word_frontend_release_impl() {
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (!g_runtime.wake_word.afe_initialized &&
        !g_runtime.wake_word.afe_release_in_progress) {
      return true;
    }
    if (g_runtime.wake_word.afe_release_in_progress ||
        g_runtime.wake_word_release_task != nullptr) {
      return true;
    }
    g_runtime.wake_word.afe_release_in_progress = true;
  }

  std::string task_error;
  const lunaredu::lunaros::ManagedTaskDefinition definition{
      .id = "wake_word_release",
      .owner_service_id = "assistant.xiaozhi",
      .role = "wake_word_release",
      .stack_bytes = kWakeWordReleaseTaskStackSize,
      .priority = 1,
      .core_affinity = -1,
      .lifetime = lunaredu::lunaros::ManagedTaskLifetime::Transient,
      .memory = lunaredu::lunaros::ManagedTaskMemory::Internal,
      .allow_internal_fallback = false,
  };
  lunaredu::lunaros::ManagedTaskHandle created_task = nullptr;
  if (lunaredu::lunaros::create_managed_task(
          definition, wake_word_release_task, nullptr,
          &created_task, task_error)) {
    {
      std::lock_guard<std::mutex> lock(g_runtime.mutex);
      if (g_runtime.wake_word.afe_release_in_progress) {
        g_runtime.wake_word_release_task = created_task;
      }
    }
    ESP_LOGI(kTag, "wake frontend release task queued");
    return true;
  }

  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.wake_word.afe_release_in_progress = false;
    g_runtime.wake_word_release_task = nullptr;
    g_runtime.wake_word.afe_last_error =
        task_error.empty() ? "wake_word_release_task_failed" : task_error;
  }
  ESP_LOGE(kTag, "failed to create wake frontend release task error=%s",
           task_error.empty() ? "unknown" : task_error.c_str());
  return false;
}

void maybe_start_wake_word_init_locked() {
  const int64_t now = now_ms();
  const bool service_available =
      g_runtime.service_enabled && !g_runtime.service_suspended &&
      (g_runtime.transport.mqtt_session == nullptr ||
       session_idle_available_locked());
  const lunaredu::pet::conversation::WakeFrontendInitializationPolicyInput input{
      .service_available = service_available,
      .initialized = g_runtime.wake_word.afe_initialized,
      .initialization_in_progress = g_runtime.wake_word.afe_init_in_progress,
      .initialization_task_running = g_runtime.wake_word_init_task != nullptr,
      .release_in_progress = g_runtime.wake_word.afe_release_in_progress,
      .release_task_running = g_runtime.wake_word_release_task != nullptr,
      .retry_due = g_runtime.wake_word.afe_next_init_at_ms <= now,
  };
  if (!lunaredu::pet::conversation::wake_frontend_initialization_allowed(input)) {
    return;
  }

  // Do not create a transient ESP-SR task when the heap is already too
  // fragmented to hold the model. The task stack itself consumes an 8 KiB
  // internal block; a lower guard lets the task start, then makes the
  // frontend fail inside its initializer and repeats the allocation/free
  // cycle until the heap is fragmented beyond recovery.
  // ESP-SR may freeze the flash cache while it maps or unmaps its model.  That
  // path requires the current task stack to be in internal SRAM, so this
  // transient initializer must not use the PSRAM-backed task mode. Keep the
  // stack deliberately small: the AFE data buffers themselves are configured
  // for PSRAM, while the remaining internal block is needed by ESP-SR control
  // allocations.
  const size_t internal_largest =
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (internal_largest < kWakeWordInitMinimumInternalLargestBlock) {
    g_runtime.wake_word.afe_next_init_at_ms =
        now + static_cast<int64_t>(kWakeWordInitRetryMs * 6);
    g_runtime.wake_word.afe_last_error =
        "wake_word_insufficient_internal_memory";
    ESP_LOGW(kTag,
             "wake frontend init deferred before task creation: internal largest block=%u need=%u",
             static_cast<unsigned>(internal_largest),
             static_cast<unsigned>(kWakeWordInitMinimumInternalLargestBlock));
    return;
  }

  g_runtime.wake_word.afe_init_in_progress = true;
  ESP_LOGI(kTag,
           "wake frontend init task create begin attempts=%" PRIu32
           " task_caps=INTERNAL %s",
           g_runtime.wake_word.afe_init_attempts, heap_snapshot_text().c_str());
  std::string task_error;
  const lunaredu::lunaros::ManagedTaskDefinition definition{
      .id = "wake_word_init",
      .owner_service_id = "assistant.xiaozhi",
      .role = "wake_word_init",
      .stack_bytes = kWakeWordInitTaskStackSize,
      .priority = 1,
      .core_affinity = -1,
      .lifetime = lunaredu::lunaros::ManagedTaskLifetime::Transient,
      .memory = lunaredu::lunaros::ManagedTaskMemory::Internal,
      .allow_internal_fallback = false,
  };
  if (!lunaredu::lunaros::create_managed_task(
          definition, wake_word_init_task, nullptr,
          &g_runtime.wake_word_init_task, task_error)) {
    g_runtime.wake_word.afe_init_in_progress = false;
    g_runtime.wake_word_init_task = nullptr;
    g_runtime.wake_word.afe_next_init_at_ms =
        now + static_cast<int64_t>(kWakeWordInitRetryMs);
    g_runtime.wake_word.afe_last_error = "wake_word_init_task_failed";
    ESP_LOGE(kTag, "Failed to create wake_word_init task error=%s %s",
             task_error.c_str(), heap_snapshot_text().c_str());
  }
}

} // namespace

bool request_wake_word_frontend_release() {
  return request_wake_word_frontend_release_impl();
}

void setup_assistant_minimal_app() {
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    g_runtime.boot_id = esp_random();
    g_runtime.started_at_ms = now_ms();
    g_runtime.device_id = lunaredu::system::SystemInfo::GetMacAddress();
    g_runtime.client_id = lunaredu::pet::conversation::assistant_client_id();
    g_runtime.activation.timeout_ms = kActivationTimeoutMsDefault;
    load_state_locked();
    ESP_LOGI(kTag,
             "activation state loaded ready=%d pending=%d configured=%d blocked=%d fresh_required=%d fresh_seen=%d remote_bound=%d code_present=%d mqtt_present=%d standby=%s",
             g_runtime.activation.ready, g_runtime.activation.pending,
             g_runtime.activation.configured, g_runtime.activation.user_blocked,
             g_runtime.activation.fresh_pairing_required,
             g_runtime.activation.fresh_pairing_observed,
             g_runtime.activation.remote_binding_still_active,
             !g_runtime.activation.code.empty(),
             !g_runtime.transport.mqtt_endpoint.empty(),
             standby_listen_mode_name(g_runtime.conversation.standby_mode));
    g_runtime.wake_word.requested_word = kRequestedWakeWord;
    apply_standby_listen_mode_locked(g_runtime.conversation.standby_mode, false);
    warmup_opus_codec_locked();
  }
  setup_assistant_loop();
  (void)ensure_assistant_worker_tasks(true);
  bool show_status = false;
  bool activation_pending = false;
  bool activation_ready = false;
  std::string activation_code;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    show_status = g_runtime.service_enabled && !g_runtime.service_suspended;
    activation_pending = g_runtime.activation.pending;
    activation_ready = g_runtime.activation.ready;
    activation_code = g_runtime.activation.code;
  }
  if (show_status) {
    if (activation_pending && !activation_code.empty()) {
      conversation_emit_status("等待激活", "请先完成小智激活",
                             activation_code.c_str(), nullptr);
    } else if (!activation_ready) {
      conversation_emit_status("连接中", "正在获取小智配置", "", nullptr);
    } else {
      conversation_emit_status("已就绪", "板载语音待命",
                             lunaredu::audio::local_pcm_audio_bridge_ready()
                                 ? "local audio ready"
                                 : "local audio unavailable",
                             conversation_host_last_error().c_str());
    }
  }
  ESP_LOGI(kTag, "assistant minimal app ready");
}

void tick_assistant_minimal_app() {
  (void)ensure_assistant_worker_tasks(false);
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    if (!g_runtime.service_enabled || g_runtime.service_suspended) {
      return;
    }
    apply_pending_activation_result_locked();
    const int64_t now = now_ms();
    maybe_submit_activation_locked(now);
  }
  tick_assistant_loop();
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    maybe_start_wake_word_init_locked();
  }
}

bool assistant_worker_tasks_ready() {
  const auto task_snapshot =
      lunaredu::lunaros::managed_task_registry_snapshot();
  bool queue_ready = false;
  {
    std::lock_guard<std::mutex> lock(g_runtime.mutex);
    queue_ready = g_runtime.downlink_audio_queue != nullptr;
  }
  return queue_ready && worker_registry_ready(task_snapshot);
}

} // namespace lunaredu::pet::conversation::internal
