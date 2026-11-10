#pragma once
// Generated from hardware protocol/wire/v1/contract.json; SHA256 7349d84409872ad905f4577f7719e3d01ddb8e3fc2bfdfc4e209147102e9e8ac. Do not edit.
#include <cstddef>
#include <cstdint>
#include <string_view>
namespace lunaredu::protocol::contract {
inline constexpr std::string_view name = "lunaredu.wire";
inline constexpr uint8_t version = 1;
inline constexpr std::string_view family = "device";
inline constexpr std::string_view profile_for(std::string_view type) {
 return type.substr(0,10)=="operation."?"operation":type.substr(0,6)=="media."?"media":"activity";
}
inline constexpr std::string_view builtin_module = "platform_runtime";
inline constexpr uint8_t lp_version = 1;
inline constexpr size_t lp_header_bytes = 10, lp_crc_bytes = 4, lp_max_payload_bytes = 2048;
inline constexpr size_t operation_max_request_bytes = 2048;
inline constexpr size_t operation_max_sequence_install_request_bytes = 14336;
inline constexpr size_t operation_max_result_bytes = 8192;
inline constexpr size_t operation_max_batch_size = 10;
inline constexpr size_t operation_max_realtime_bytes = 1536;
inline constexpr size_t operation_max_realtime_result_bytes = 3900;
inline constexpr size_t operation_recent_request_count = 64;
inline constexpr std::string_view session_open = "session.open";
inline constexpr std::string_view session_opened = "session.opened";
inline constexpr std::string_view session_close = "session.close";
inline constexpr std::string_view session_closed = "session.closed";
inline constexpr std::string_view capability_list = "capability.list";
inline constexpr std::string_view capability_describe = "capability.describe";
inline constexpr std::string_view capability_result = "capability.result";
inline constexpr std::string_view activity_submit = "activity.submit";
inline constexpr std::string_view activity_accepted = "activity.accepted";
inline constexpr std::string_view activity_progress = "activity.progress";
inline constexpr std::string_view activity_completed = "activity.completed";
inline constexpr std::string_view activity_failed = "activity.failed";
inline constexpr std::string_view activity_rejected = "activity.rejected";
inline constexpr std::string_view activity_cancel = "activity.cancel";
inline constexpr std::string_view activity_canceled = "activity.canceled";
inline constexpr std::string_view media_open = "media.open";
inline constexpr std::string_view media_opened = "media.opened";
inline constexpr std::string_view media_chunk = "media.chunk";
inline constexpr std::string_view media_close = "media.close";
inline constexpr std::string_view media_closed = "media.closed";
inline constexpr std::string_view event_emit = "event.emit";
inline constexpr std::string_view protocol_error = "protocol.error";
inline constexpr std::string_view operation_invoke = "operation.invoke";
inline constexpr std::string_view operation_result = "operation.result";
inline constexpr bool message_type(std::string_view value) {
  return value == session_open ||
         value == session_opened ||
         value == session_close ||
         value == session_closed ||
         value == capability_list ||
         value == capability_describe ||
         value == capability_result ||
         value == activity_submit ||
         value == activity_accepted ||
         value == activity_progress ||
         value == activity_completed ||
         value == activity_failed ||
         value == activity_rejected ||
         value == activity_cancel ||
         value == activity_canceled ||
         value == media_open ||
         value == media_opened ||
         value == media_chunk ||
         value == media_close ||
         value == media_closed ||
         value == event_emit ||
         value == protocol_error ||
         value == operation_invoke ||
         value == operation_result;
}
inline constexpr uint8_t get_info = 1;
inline constexpr uint8_t open_session = 2;
inline constexpr uint8_t confirm_session = 3;
inline constexpr uint8_t set_wifi = 4;
inline constexpr uint8_t get_status = 5;
inline constexpr uint8_t close_session = 6;
inline constexpr uint8_t get_binding = 7;
inline constexpr uint8_t factory_status = 16;
inline constexpr uint8_t factory_start = 17;
inline constexpr uint8_t factory_confirm = 18;
inline constexpr uint8_t factory_reset = 19;
inline constexpr uint8_t factory_unit_id = 20;
inline constexpr uint8_t device_command = 32;
inline constexpr uint8_t error = 127;
}
