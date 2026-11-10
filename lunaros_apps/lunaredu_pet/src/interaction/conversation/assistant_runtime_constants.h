#pragma once

#include <cstddef>
#include <cstdint>

namespace lunaredu::pet::conversation::internal {

inline constexpr const char *kActivationNamespace = "assistant";
inline constexpr const char *kPersistMqttEndpointKey = "mqtt_ep";
inline constexpr const char *kPersistMqttClientIdKey = "mqtt_cid";
inline constexpr const char *kPersistMqttUserKey = "mqtt_user";
inline constexpr const char *kPersistMqttPasswordKey = "mqtt_pwd";
inline constexpr const char *kPersistMqttPublishKey = "mqtt_pub";
inline constexpr const char *kPersistMqttSubscribeKey = "mqtt_sub";
inline constexpr const char *kPersistCodeKey = "act_code";
inline constexpr const char *kPersistChallengeKey = "act_chal";
inline constexpr const char *kPersistPendingKey = "act_pending";
inline constexpr const char *kPersistUserBlockedKey = "act_blocked";
inline constexpr const char *kPersistFreshPairingRequiredKey = "pair_req";
inline constexpr const char *kPersistFreshPairingObservedKey = "pair_seen";
inline constexpr const char *kPersistRemoteBindingActiveKey = "remote_bound";
inline constexpr const char *kPersistTimeoutKey = "act_timeout";
inline constexpr const char *kPersistServerTimeKey = "srv_time";
inline constexpr const char *kPersistTimezoneOffsetKey = "tz_offset";
inline constexpr const char *kPersistStandbyModeKey = "standby_mode";
inline constexpr const char *kDefaultAssistantOtaUrl =
    "https://api.tenclass.net/xiaozhi/ota/";
inline constexpr const char *kRemoteBindingStillActiveError =
    "xiaozhi_remote_binding_still_active";

inline constexpr uint32_t kBrowserSampleRate = 16000;
inline constexpr uint16_t kBrowserChannels = 1;
inline constexpr uint32_t kFrameDurationMs = 60;
inline constexpr uint32_t kActivationTimeoutMsDefault = 30000;
inline constexpr uint32_t kConnectTimeoutMs = 10000;
inline constexpr uint32_t kSystemTimeSyncTimeoutMs = 12000;
inline constexpr int kOpusComplexity = 0;
inline constexpr uint32_t kUplinkAudioQueueDepth = 12;
inline constexpr uint32_t kUplinkAudioTaskStackSize = 2048 * 12;
// Keep several hundred milliseconds (and, for the usual longer Opus frames,
// close to one second) of compressed audio available so a short scheduler or
// Wi-Fi burst does not turn into a mid-sentence hole. Queue admission is also
// allowed to wait briefly; the UDP callback releases the runtime mutex before
// waiting, so the playback worker can drain concurrently.
inline constexpr uint32_t kDownlinkAudioQueueDepth = 16;
inline constexpr uint32_t kDownlinkAudioQueueWaitMs = 20;
// Capture and playback are mutually exclusive product states and execute
// sequentially on one worker. Match the official XiaoZhi Opus worker stack
// scale while keeping the stack in internal SRAM.
inline constexpr uint32_t kAudioTaskStackSize = 2048 * 12;
inline constexpr uint32_t kAudioTaskPriority = 5;
inline constexpr uint32_t kConnectTaskStackSize = 2048 * 4;
// ESP-SR can freeze the flash cache while loading models, so this transient
// initializer must use an internal-RAM stack. Its AFE data buffers live in
// PSRAM, but the model discovery/loading path itself needs more call-stack
// headroom than the rest of the runtime.
inline constexpr uint32_t kWakeWordInitTaskStackSize = 8192;
// ESP-SR unmaps model pages during teardown, which must run on an internal-RAM
// task stack.  Keep this transient task deliberately small; it only performs
// the release call and status handoff.
inline constexpr uint32_t kWakeWordReleaseTaskStackSize = 2048 * 2;
// Keep enough contiguous SRAM for the transient internal stack plus the ESP-SR
// control allocations that remain on internal memory; otherwise a retry can
// consume the last block and permanently fragment the heap.
inline constexpr size_t kWakeWordInitMinimumInternalLargestBlock =
    kWakeWordInitTaskStackSize + 4096;
inline constexpr uint32_t kWakeWordInitRetryMs = 5000;
inline constexpr uint32_t kLocalAudioReadTimeoutMs = 120;
inline constexpr uint32_t kLocalAudioCaptureStartDelayMs = 200;
inline constexpr uint32_t kServiceMqttTaskStackSize = 2048 * 3;
inline constexpr uint32_t kServiceMqttReconnectBackoffMinMs = 1000;
inline constexpr uint32_t kServiceMqttReconnectBackoffMaxMs = 10000;
inline constexpr uint32_t kServiceUdpReconnectBackoffMinMs = 1000;
inline constexpr uint32_t kServiceUdpReconnectBackoffMaxMs = 10000;
inline constexpr uint32_t kServiceIdleKeepaliveMs = 45000;
inline constexpr uint32_t kServiceIdleKeepaliveMinGapMs = 10000;
inline constexpr uint32_t kWakeWordTriggerCooldownMs = 500;
inline constexpr uint32_t kWakeWordResumeDelayMs = 300;
inline constexpr uint32_t kSessionCloseGraceMs = 500;
inline constexpr uint32_t kAssistantResponseTimeoutMs = 30000;
inline constexpr uint32_t kTtsStopPlaybackDrainGraceMs = 500;
inline constexpr uint32_t kVadFrameEnergyThreshold = 500;  // Lower threshold for better sensitivity
inline constexpr uint32_t kVadSpeechStartMs = 180;  // 3 frames (180ms) of continuous speech to trigger
inline constexpr uint32_t kVadSpeechEndMs = 800;  // Shorter silence to end speech
inline constexpr uint32_t kVadAutoStopSilenceMs = 1000;  // 1 second silence to auto-stop
inline constexpr uint32_t kVadMinAutoStopSpeechMs = 300;  // Minimum speech before auto-stop
inline constexpr uint32_t kAutoConversationNoSpeechTimeoutMs = 15000;
// Continuous mode is a listening preference, not a permanent microphone
// lease. After a quiet period it falls back to wake-word standby so the user
// must intentionally wake the device again.
inline constexpr uint32_t kContinuousConversationIdleTimeoutMs = 60000;
inline constexpr uint32_t kMaxListeningDurationMs = 30000;  // 30 seconds max listening
inline constexpr uint32_t kEsp32ListeningNormalizeMinPeak = 256U;
inline constexpr uint32_t kEsp32ListeningTargetPeak = 12000U;
inline constexpr int kEsp32ListeningMaxGainNumerator = 16;
inline constexpr int kEsp32ListeningGainDenominator = 1;
inline constexpr const char *kRequestedWakeWord = "你好小智";

} // namespace lunaredu::pet::conversation::internal
