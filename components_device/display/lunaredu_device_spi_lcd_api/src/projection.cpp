#include "device_api/device_operation_builders.h"
#include "model/device_model.h"
#include "lunaredu_device_spi_lcd_projection.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>

#include "sdkconfig.h"

#include "device_api/device_projection.h"
#include <esp_log.h>
#include "lunaredu_device_spi_lcd_surface.h"

namespace lunaredu::device {

using lunaredu::lunaros::DeviceBulkRequest;
using lunaredu::lunaros::DeviceCapabilityDescriptor;
using lunaredu::lunaros::DeviceApiContext;
using lunaredu::lunaros::DeviceMetadataProjection;
using lunaredu::lunaros::DeviceModuleProjection;
using lunaredu::lunaros::DeviceModuleProjectionMetadata;
using lunaredu::lunaros::DeviceOperationDefinition;
using lunaredu::lunaros::DeviceOperationResult;
using lunaredu::lunaros::DeviceProjectionBinding;
using lunaredu::lunaros::DeviceProjectionRegistry;
using lunaredu::lunaros::device_operation_ack;
using lunaredu::lunaros::device_operation_error;
using lunaredu::lunaros::make_device_json_command;
using lunaredu::lunaros::make_device_json_provider;
using lunaredu::lunaros::make_device_json_query;
using lunaredu::lunaros::make_device_media;
using lunaredu::lunaros::make_device_upload;
using lunaredu::lunaros::DeviceMediaRequest;
using lunaredu::lunaros::DeviceMediaWriter;
namespace {
DeviceProjectionBinding g_projection;

constexpr const char *kCapabilityId = "screen_canvas";
constexpr const char *kModuleId = "screen";
constexpr size_t kUploadChunkBytes = 1024;
constexpr size_t kMaxFrameUploadBytes = 512 * 1024;

constexpr const char *kPresenceCapabilities[] = {
    "screen",
    "display",
    "rgb_canvas",
};

const DeviceCapabilityDescriptor kScreenCapability = {
    kCapabilityId,
    kPresenceCapabilities,
    sizeof(kPresenceCapabilities) / sizeof(kPresenceCapabilities[0]),
};

bool parse_u16(JsonVariantConst value, uint16_t &result) {
  if (!value.is<int64_t>()) {
    return false;
  }
  const int64_t parsed = value.as<int64_t>();
  if (parsed < 0 || parsed > UINT16_MAX) {
    return false;
  }
  result = static_cast<uint16_t>(parsed);
  return true;
}

bool parse_hex_color(const std::string &value, bool rgb666_source,
                     uint32_t &result) {
  const char *digits = value.c_str();
  if (value.size() == 7 && value[0] == '#') {
    digits += 1;
  } else if (value.size() == 8 && value.rfind("0x", 0) == 0) {
    digits += 2;
  } else if (!rgb666_source && value.size() == 6 &&
             value.rfind("0x", 0) == 0) {
    digits += 2;
  } else if (value.size() == 6) {
  } else {
    return false;
  }

  errno = 0;
  char *end = nullptr;
  const unsigned long parsed = std::strtoul(digits, &end, 16);
  if (errno != 0 || end == digits || *end != '\0') {
    return false;
  }
  if (parsed > 0xFFFFFFUL) return false;
  if (rgb666_source) {
    result = static_cast<uint32_t>(parsed) & 0xFCFCFCU;
  } else {
    const uint8_t red = static_cast<uint8_t>((parsed >> 16) & 0xFFU);
    const uint8_t green = static_cast<uint8_t>((parsed >> 8) & 0xFFU);
    const uint8_t blue = static_cast<uint8_t>(parsed & 0xFFU);
    result = static_cast<uint16_t>(((red & 0xF8U) << 8) |
                                   ((green & 0xFCU) << 3) | (blue >> 3));
  }
  return true;
}

bool parse_color(JsonVariantConst value, bool rgb666_source,
                 uint32_t &result) {
  if (value.is<int64_t>()) {
    const int64_t parsed = value.as<int64_t>();
    const int64_t maximum = rgb666_source ? 0xFFFFFF : UINT16_MAX;
    if (parsed < 0 || parsed > maximum) return false;
    result = rgb666_source
                 ? static_cast<uint32_t>(parsed) & 0xFCFCFCU
                 : static_cast<uint32_t>(parsed);
    return true;
  }
  if (value.is<const char *>()) {
    return parse_hex_color(value.as<std::string>(), rgb666_source, result);
  }
  return false;
}

const char *source_pixel_format_name(DeviceSpiLcdSourcePixelFormat format) {
  return format == DeviceSpiLcdSourcePixelFormat::kRgb666U8x3 ? "rgb666-u8x3"
                                                          : "rgb565";
}

void write_screen_status_payload(const DeviceSpiLcdSurfaceStatus &status, JsonObject payload) {
  payload["busy"]=false;payload["snapshot_available"]=true;
  payload["ok"] = status.initialized && status.last_error.empty();
  payload["available"] = status.initialized;
  payload["configured"] = status.configured;
  payload["initialized"] = status.initialized;
  payload["backend"] = "spi_lcd";
  payload["pixel_format"] =
      source_pixel_format_name(status.source_pixel_format);
  payload["transfer_mode"] =
      status.pixel_mode == DeviceSpiLcdPixelMode::kRgb565 ? "rgb565"
                                                          : "rgb666";
  payload["width"] = status.mirror_width;
  payload["height"] = status.mirror_height;
  payload["frame_count"] = status.frame_count;
  const auto &transfer = status.rgb565_transfer;
  JsonObject rgb565 = payload["rgb565_transfer"].to<JsonObject>();
  rgb565["buffers"] = transfer.buffer_count;
  rgb565["buffer_bytes"] = transfer.buffer_bytes;
  rgb565["paired_allocation"] = transfer.paired_allocation;
  if (transfer.allocation_start) rgb565["allocation_start"] = uint64_t(transfer.allocation_start);
  else rgb565["allocation_start"] = nullptr;
  rgb565["frames"] = transfer.frames;
  rgb565["failures"] = transfer.failures;
  rgb565["submitted"] = transfer.submitted;
  rgb565["completed"] = transfer.completed;
  rgb565["bytes"] = transfer.bytes;
  rgb565["drains"] = transfer.drains;
  rgb565["recycles"] = transfer.recycles;
  rgb565["rolling_reuse"] = transfer.rolling_reuse;
  rgb565["window_us"] = transfer.window_us;
  rgb565["copy_us"] = transfer.copy_us;
  rgb565["send_us"] = transfer.send_us;
  rgb565["drain_us"] = transfer.drain_us;
  rgb565["recycle_us"] = transfer.recycle_us;
  rgb565["task_basis"]="monitor_only_scheduler_completed_slices";
  rgb565["profiled_frames"]=transfer.profiled_frames;
  rgb565["window_task_us"]=transfer.window_task_us;
  rgb565["copy_task_us"]=transfer.copy_task_us;
  rgb565["send_task_us"]=transfer.send_task_us;
  rgb565["recycle_task_us"]=transfer.recycle_task_us;
  rgb565["drain_task_us"]=transfer.drain_task_us;
  rgb565["last_draw_us"] = transfer.last_draw_us;
  rgb565["max_draw_us"] = transfer.max_draw_us;
  rgb565["faulted"] = transfer.faulted;
  payload["last_operation"] = status.last_pattern;
  payload["last_error"] = status.last_error;
  payload["mirror_available"] = status.mirror_available;
  payload["mirror_generation"] = status.mirror_generation;
  payload["canvas_override_active"] = status.canvas_override_active;
  payload["hardware_scroll_supported"] = status.hardware_scroll_supported;
  payload["hardware_scroll_axis"] =
      status.hardware_scroll_axis ==
              DeviceSpiLcdHardwareScrollAxis::kHorizontal
          ? "horizontal"
          : status.hardware_scroll_axis ==
                    DeviceSpiLcdHardwareScrollAxis::kVertical
                ? "vertical"
                : "none";
  payload["hardware_scroll_active"] = status.hardware_scroll_active;
  payload["hardware_scroll_offset"] = status.hardware_scroll_offset;
  payload["dma_pipeline_enabled"] = status.dma_pipeline_enabled;
  payload["dma_pipeline_buffers"] = status.dma_pipeline_buffer_count;
  payload["dma_pipeline_buffer_rows"] = status.dma_pipeline_buffer_rows;
  payload["dma_pipeline_buffer_pixels"] = status.dma_pipeline_buffer_pixels;
  payload["dma_pipeline_max_in_flight"] =
      status.dma_pipeline_max_in_flight;
  payload["dma_pipeline_submitted"] = status.dma_pipeline_submitted;
  payload["dma_pipeline_completed"] = status.dma_pipeline_completed;
  payload["dma_pipeline_buffer_waits"] =
      status.dma_pipeline_buffer_waits;
}

void fill_screen_status_payload(DeviceApiContext &,JsonObject payload) {
  DeviceSpiLcdSurfaceStatus status;
  if(!device_spi_lcd_surface_try_status(status)) {
    payload["busy"]=true;payload["snapshot_available"]=false;return;
  }
  write_screen_status_payload(status,payload);
}
DeviceOperationResult read_screen_status(DeviceApiContext &,JsonObject payload) {
  DeviceSpiLcdSurfaceStatus status;
  if(!device_spi_lcd_surface_try_status(status))
    return device_operation_error("screen_status_busy",503,"screen_busy");
  write_screen_status_payload(status,payload);return device_operation_ack();
}

void fill_screen_capabilities_payload(DeviceApiContext &, JsonObject payload) {
  const DeviceSpiLcdSurfaceStatus status = device_spi_lcd_surface_status();
  payload["contract_version"] = 2;
  payload["backend"] = "spi_lcd";
  payload["width"] = status.mirror_width;
  payload["height"] = status.mirror_height;
  payload["pixel_format"] =
      source_pixel_format_name(status.source_pixel_format);
  JsonArray transfer_modes = payload["transfer_modes"].to<JsonArray>();
  transfer_modes.add(status.source_pixel_format ==
                             DeviceSpiLcdSourcePixelFormat::kRgb666U8x3
                         ? "rgb666"
                         : "rgb565");
  payload["hardware_scroll_supported"] = status.hardware_scroll_supported;
  payload["hardware_scroll_axis"] =
      status.hardware_scroll_axis ==
              DeviceSpiLcdHardwareScrollAxis::kHorizontal
          ? "horizontal"
          : status.hardware_scroll_axis ==
                    DeviceSpiLcdHardwareScrollAxis::kVertical
                ? "vertical"
                : "none";
  payload["hardware_scroll_native_lines"] =
      status.hardware_scroll_native_lines;
  payload["dma_pipeline_enabled"] = status.dma_pipeline_enabled;
  payload["dma_pipeline_buffers"] = status.dma_pipeline_buffer_count;
  payload["dma_pipeline_buffer_rows"] = status.dma_pipeline_buffer_rows;
  payload["dma_pipeline_buffer_pixels"] = status.dma_pipeline_buffer_pixels;
  if (status.source_pixel_format == DeviceSpiLcdSourcePixelFormat::kRgb666U8x3) {
    payload["frame_upload_channel_order"] = "rgb";
  } else {
    JsonArray byte_orders =
        payload["frame_upload_byte_orders"].to<JsonArray>();
    byte_orders.add("little");
    byte_orders.add("big");
  }
  JsonArray operations = payload["supported_ops"].to<JsonArray>();
  operations.add("status");
  operations.add("fill");
  operations.add("fill_rect");
  operations.add("frame_upload");
  operations.add("frame_capture");
  operations.add("canvas_override");
}

struct CaptureWriterContext {
  const DeviceMediaWriter *writer = nullptr;
};

bool capture_begin(uint16_t status, const char *content_type, void *context) {
  auto *capture = static_cast<CaptureWriterContext *>(context);
  return capture != nullptr && capture->writer != nullptr &&
         capture->writer->begin_stream != nullptr &&
         capture->writer->begin_stream(status, content_type,
                                       capture->writer->context);
}

bool capture_chunk(const uint8_t *data, size_t size, void *context) {
  auto *capture = static_cast<CaptureWriterContext *>(context);
  return capture != nullptr && capture->writer != nullptr &&
         capture->writer->send_chunk != nullptr &&
         capture->writer->send_chunk(data, size, capture->writer->context);
}

void capture_end(void *context) {
  auto *capture = static_cast<CaptureWriterContext *>(context);
  if (capture != nullptr && capture->writer != nullptr &&
      capture->writer->end_stream != nullptr) {
    capture->writer->end_stream(capture->writer->context);
  }
}

DeviceOperationResult handle_frame_capture(DeviceApiContext &,
                                           const DeviceMediaRequest &,
                                           const DeviceMediaWriter &writer) {
  CaptureWriterContext context{&writer};
  DeviceSpiLcdSurfaceBmpWriter bmp;
  bmp.context = &context;
  bmp.begin_stream = capture_begin;
  bmp.send_chunk = capture_chunk;
  bmp.end_stream = capture_end;
  std::string error;
  const esp_err_t result = device_spi_lcd_surface_write_bmp(bmp, &error);
  return result == ESP_OK
             ? device_operation_ack()
             : device_operation_error(
                   error.empty() ? "display_frame_not_available" : error.c_str(),
                   error == "display_frame_not_available" ? 503 : 500,
                   "screen_capture_failed");
}

void begin_canvas_override() {
  device_spi_lcd_surface_set_canvas_override(true);
}

void restore_canvas_override_after_failure(bool was_active) {
  if (!was_active) {
    device_spi_lcd_surface_set_canvas_override(false);
  }
}

DeviceOperationResult draw_error(const std::string &error) {
  const char *message = error.empty() ? "screen_draw_failed" : error.c_str();
  const uint16_t status_code =
      error == "display_not_initialized" || error == "disabled" ? 503 : 400;
  return device_operation_error(message, status_code, "screen_draw_failed");
}

DeviceOperationResult handle_screen_fill(DeviceApiContext &device,
                                         JsonVariantConst request,
                                            JsonObject response) {
  const DeviceSpiLcdSourcePixelFormat format =
      device_spi_lcd_surface_source_pixel_format();
  uint32_t color = 0;
  if (!parse_color(request["color"],
                   format == DeviceSpiLcdSourcePixelFormat::kRgb666U8x3, color)) {
    return device_operation_error("invalid_color", 400,
                                     "screen_draw_failed");
  }

  const bool was_active = device_spi_lcd_surface_canvas_override_active();
  begin_canvas_override();
  std::string error;
  const bool drawn = format == DeviceSpiLcdSourcePixelFormat::kRgb666U8x3
                         ? device_spi_lcd_surface_fill_rgb666(color, &error)
                         : device_spi_lcd_surface_fill(
                               static_cast<uint16_t>(color), &error);
  if (!drawn) {
    restore_canvas_override_after_failure(was_active);
    fill_screen_status_payload(device, response);
    return draw_error(error);
  }
  fill_screen_status_payload(device, response);
  return device_operation_ack();
}

DeviceOperationResult handle_screen_fill_rect(DeviceApiContext &device,
                                              JsonVariantConst request,
                                                 JsonObject response) {
  uint16_t x = 0;
  uint16_t y = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  const DeviceSpiLcdSourcePixelFormat format =
      device_spi_lcd_surface_source_pixel_format();
  uint32_t color = 0;
  if (!parse_u16(request["x"], x) || !parse_u16(request["y"], y) ||
      !parse_u16(request["width"], width) ||
      !parse_u16(request["height"], height) || width == 0 || height == 0 ||
      !parse_color(request["color"],
                   format == DeviceSpiLcdSourcePixelFormat::kRgb666U8x3, color)) {
    return device_operation_error("invalid_rectangle", 400,
                                     "screen_draw_failed");
  }

  const DeviceSpiLcdSurfaceStatus status = device_spi_lcd_surface_status();
  if (!status.initialized) {
    return device_operation_error("screen_unavailable", 503,
                                     "screen_draw_failed");
  }
  if (x >= status.mirror_width || y >= status.mirror_height ||
      static_cast<uint32_t>(x) + width > status.mirror_width ||
      static_cast<uint32_t>(y) + height > status.mirror_height) {
    return device_operation_error("rectangle_out_of_bounds", 400,
                                     "screen_draw_failed");
  }

  const bool was_active = device_spi_lcd_surface_canvas_override_active();
  begin_canvas_override();
  std::string error;
  const bool drawn = format == DeviceSpiLcdSourcePixelFormat::kRgb666U8x3
                         ? device_spi_lcd_surface_fill_rect_rgb666(
                               x, y, width, height, color, &error)
                         : device_spi_lcd_surface_fill_rect(
                               x, y, width, height,
                               static_cast<uint16_t>(color), &error);
  if (!drawn) {
    restore_canvas_override_after_failure(was_active);
    fill_screen_status_payload(device, response);
    return draw_error(error);
  }
  fill_screen_status_payload(device, response);
  return device_operation_ack();
}

DeviceOperationResult handle_canvas_override_set(DeviceApiContext &device,
                                                 JsonVariantConst request,
                                                    JsonObject response) {
  const JsonVariantConst active = request["active"];
  if (!active.is<bool>()) {
    return device_operation_error("missing_or_invalid_active", 400,
                                     "screen_override_failed");
  }
  device_spi_lcd_surface_set_canvas_override(active.as<bool>());
  fill_screen_status_payload(device, response);
  return device_operation_ack();
}

bool request_arg(const DeviceBulkRequest &request, const char *name,
                 std::string &value) {
  if (request.arg == nullptr || request.has_arg == nullptr ||
      !request.has_arg(name, request.context)) {
    return false;
  }
  value = request.arg(name, request.context);
  return true;
}

bool parse_u16_arg(const DeviceBulkRequest &request, const char *name,
                   uint16_t &result, bool required, uint16_t fallback = 0) {
  std::string value;
  if (!request_arg(request, name, value)) {
    result = fallback;
    return !required;
  }
  if (value.empty()) {
    return false;
  }

  errno = 0;
  char *end = nullptr;
  const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
  if (errno != 0 || end == value.c_str() || *end != '\0' ||
      parsed > UINT16_MAX) {
    return false;
  }
  result = static_cast<uint16_t>(parsed);
  return true;
}

bool read_frame_body(const DeviceBulkRequest &request, uint8_t *target,
                     size_t expected_size, std::string &error) {
  if (target == nullptr || expected_size == 0) {
    error = "invalid_frame_buffer";
    return false;
  }
  if (request.body_data != nullptr && request.body_size > 0) {
    if (request.body_size != expected_size) {
      error = "frame_size_mismatch";
      return false;
    }
    std::memcpy(target, request.body_data, expected_size);
    return true;
  }
  if (request.read_chunk == nullptr || request.content_length != expected_size) {
    error = "frame_size_mismatch";
    return false;
  }

  size_t offset = 0;
  while (offset < expected_size) {
    const size_t wanted =
        std::min(kUploadChunkBytes, expected_size - offset);
    const int read =
        request.read_chunk(target + offset, wanted, request.context);
    if (read <= 0 || static_cast<size_t>(read) > wanted) {
      error = "frame_body_read_failed";
      return false;
    }
    offset += static_cast<size_t>(read);
  }
  return true;
}

DeviceOperationResult handle_screen_frame_upload(
    DeviceApiContext &device, const DeviceBulkRequest &request,
    JsonObject response) {
  uint16_t x = 0;
  uint16_t y = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  if (!parse_u16_arg(request, "x", x, false) ||
      !parse_u16_arg(request, "y", y, false) ||
      !parse_u16_arg(request, "width", width, true) ||
      !parse_u16_arg(request, "height", height, true) || width == 0 ||
      height == 0) {
    return device_operation_error("invalid_frame_geometry", 400,
                                     "screen_upload_failed");
  }

  const DeviceSpiLcdSurfaceStatus status = device_spi_lcd_surface_status();
  if (!status.initialized) {
    return device_operation_error("screen_unavailable", 503,
                                     "screen_upload_failed");
  }
  if (x >= status.mirror_width || y >= status.mirror_height ||
      static_cast<uint32_t>(x) + width > status.mirror_width ||
      static_cast<uint32_t>(y) + height > status.mirror_height) {
    return device_operation_error("frame_out_of_bounds", 400,
                                     "screen_upload_failed");
  }

  const bool rgb666_source =
      status.source_pixel_format == DeviceSpiLcdSourcePixelFormat::kRgb666U8x3;
  std::string byte_order = "little";
  if (!rgb666_source) {
    (void)request_arg(request, "byte_order", byte_order);
    if (byte_order != "little" && byte_order != "big") {
      return device_operation_error("invalid_byte_order", 400,
                                    "screen_upload_failed");
    }
  }

  const size_t pixel_count = static_cast<size_t>(width) * height;
  const size_t bytes_per_pixel = rgb666_source ? 3U : sizeof(uint16_t);
  if (pixel_count > SIZE_MAX / bytes_per_pixel) {
    return device_operation_error("frame_too_large", 413,
                                     "screen_upload_failed");
  }
  const size_t frame_bytes = pixel_count * bytes_per_pixel;
  if (frame_bytes > kMaxFrameUploadBytes) {
    return device_operation_error("frame_too_large", 413,
                                     "screen_upload_failed");
  }
  std::unique_ptr<uint8_t[]> pixels(new (std::nothrow) uint8_t[frame_bytes]);
  if (pixels == nullptr) {
    return device_operation_error("frame_buffer_allocation_failed", 503,
                                     "screen_upload_failed");
  }
  std::string error;
  if (!read_frame_body(request, pixels.get(), frame_bytes, error)) {
    return device_operation_error(error.c_str(), 400,
                                     "screen_upload_failed");
  }

  const bool was_active = device_spi_lcd_surface_canvas_override_active();
  begin_canvas_override();
  const bool drawn = rgb666_source
                         ? device_spi_lcd_surface_draw_rgb666_bitmap(
                               x, y, width, height, pixels.get(),
                               DeviceSpiLcdRgb666Order::kRgb, &error)
                         : device_spi_lcd_surface_draw_rgb565_bitmap(
                               x, y, width, height,
                               reinterpret_cast<const uint16_t *>(pixels.get()),
                               byte_order == "big", &error);
  if (!drawn) {
    restore_canvas_override_after_failure(was_active);
    fill_screen_status_payload(device, response);
    return draw_error(error);
  }

  response["x"] = x;
  response["y"] = y;
  response["width"] = width;
  response["height"] = height;
  response["bytes"] = frame_bytes;
  response["pixel_format"] = source_pixel_format_name(status.source_pixel_format);
  if (rgb666_source) {
    response["channel_order"] = "rgb";
  } else {
    response["byte_order"] = byte_order;
  }
  fill_screen_status_payload(device, response["screen"].to<JsonObject>());
  return device_operation_ack();
}

void fill_screen_contract(JsonObject contract) {
  contract["pixel_format"] = source_pixel_format_name(
      device_spi_lcd_surface_source_pixel_format());
  contract["frame_upload_transport"] = "http_bulk";
  contract["frame_upload_contract_version"] = 2;
  contract["draws_activate_canvas_override"] = true;
}

constexpr DeviceOperationDefinition kScreenOperations[] = {
    make_device_json_provider("capabilities.get", fill_screen_capabilities_payload),
    make_device_json_query("status.get", read_screen_status),
    make_device_json_command("fill", handle_screen_fill),
    make_device_json_command("rect.draw", handle_screen_fill_rect),
    make_device_json_command("canvas.override.set", handle_canvas_override_set),
    make_device_upload("frame.upload", handle_screen_frame_upload),
    // Capture requests a new frame from Main and waits for physical completion.
    // Run the stream on the I/O owner so a static UI can produce that frame.
    make_device_media("frame.capture", handle_frame_capture,
                      lunaredu::lunaros::kNetworkIoServiceExecutor),
};

const DeviceModuleProjectionMetadata kScreenModule = {
    kModuleId,
    "screen_canvas",
    fill_screen_contract,
};

const DeviceModuleProjection kScreenLocalApi = {
    kCapabilityId,
    &kScreenModule,
    kScreenOperations,
    sizeof(kScreenOperations) / sizeof(kScreenOperations[0]),
};

const DeviceMetadataProjection kScreenMetadata = {
    "screen",
    "screen",
    fill_screen_status_payload,
    nullptr,
};

} // namespace

namespace {
void surface_availability(void *, bool ready) {
  std::string error;
  if (!ready) g_projection.deactivate(kCapabilityId);
  else if (!g_projection.activate(kScreenCapability, &error))
    ESP_LOGE("screen_api", "screen availability failed: %s", error.c_str());
}
}
bool register_device_spi_lcd_surface_projection(DeviceProjectionRegistry &registry,
                                               std::string &error) {
  if (!g_projection.declare(registry,
      {&kScreenCapability, &kScreenLocalApi, &kScreenMetadata, false}, error)) return false;
  if (!device_spi_lcd_surface_bind_availability(&g_projection, surface_availability)) {
    error = "screen_availability_owner_conflict"; return false;
  }
  return true;
}

} // namespace lunaredu::device
