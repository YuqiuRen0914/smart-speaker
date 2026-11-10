#include "model/device_model.h"
#include "micropython_lunaredu_module.h"
#include "native/binding_support.h"

#include <sdkconfig.h>

// A device may be present for native product code without being exposed to
// user scripts. Mask its implementation switch unless the product explicitly
// selected the matching MicroPython binding.
#if !CONFIG_LUNAREDU_MICROPYTHON_ENABLE_RGB_BINDING
#undef CONFIG_LUNAREDU_DEVICE_ENABLE_WS2812
#define CONFIG_LUNAREDU_DEVICE_ENABLE_WS2812 0
#endif
#if !CONFIG_LUNAREDU_MICROPYTHON_ENABLE_IMU_BINDING
#undef CONFIG_LUNAREDU_DEVICE_ENABLE_MPU6050
#define CONFIG_LUNAREDU_DEVICE_ENABLE_MPU6050 0
#endif
#if !CONFIG_LUNAREDU_MICROPYTHON_ENABLE_CAMERA_BINDING
#undef CONFIG_LUNAREDU_DEVICE_ENABLE_CAMERA
#define CONFIG_LUNAREDU_DEVICE_ENABLE_CAMERA 0
#endif
#if !CONFIG_LUNAREDU_MICROPYTHON_ENABLE_TFCARD_BINDING
#undef CONFIG_LUNAREDU_DEVICE_ENABLE_TFCARD
#define CONFIG_LUNAREDU_DEVICE_ENABLE_TFCARD 0
#endif
#if !CONFIG_LUNAREDU_MICROPYTHON_ENABLE_SERVO_BINDING
#undef CONFIG_LUNAREDU_DEVICE_ENABLE_SERVO_PWM
#define CONFIG_LUNAREDU_DEVICE_ENABLE_SERVO_PWM 0
#endif
#if !CONFIG_LUNAREDU_MICROPYTHON_ENABLE_ASRPRO_BINDING
#undef CONFIG_LUNAREDU_DEVICE_ENABLE_ASRPRO
#define CONFIG_LUNAREDU_DEVICE_ENABLE_ASRPRO 0
#endif
#if !CONFIG_LUNAREDU_MICROPYTHON_ENABLE_SCREEN_BINDING
#undef CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD
#define CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD 0
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string.h>
#include <memory>
#include <new>
#include <string>
#include <utility>

#if CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD
#include "lunaredu_device_spi_lcd_surface.h"
#endif
#if CONFIG_LUNAREDU_DEVICE_ENABLE_WS2812
#include "lunaredu_device_ws2812.h"
#endif
#if CONFIG_LUNAREDU_DEVICE_ENABLE_SERVO_PWM
#include "lunaredu_device_servo.h"
#endif
#if CONFIG_LUNAREDU_DEVICE_ENABLE_ASRPRO
#include "lunaredu_device_asrpro_asr.h"
#endif
#if CONFIG_LUNAREDU_DEVICE_ENABLE_CAMERA
#include "lunaredu_device_camera_media.h"
#endif
#if CONFIG_LUNAREDU_DEVICE_ENABLE_MPU6050
#include "lunaredu_device_mpu6050_imu.h"
#endif
#if CONFIG_LUNAREDU_DEVICE_ENABLE_TFCARD
#include "lunaredu_device_sdcard_tfcard_store.h"
#endif
#include "device_api/device_operation_types.h"
#include "lunaros_storage.h"

#ifndef CONFIG_LUNAREDU_MICROPYTHON_ENABLE_VISION_BINDINGS
#define CONFIG_LUNAREDU_MICROPYTHON_ENABLE_VISION_BINDINGS 0
#endif

#if CONFIG_LUNAREDU_MICROPYTHON_ENABLE_VISION_BINDINGS
#include "lunaros_face.h"
#include "image_jpeg_decoder.h"
#include "platform/cold_storage.h"
namespace {LUNAROS_COLD_BSS JpegDecoderWorkspace g_jpeg_workspace;}
#include "lunaros_vision.h"
#endif

#ifndef CONFIG_LUNAREDU_MICROPYTHON_ENABLE_LUNARLINK_BINDINGS
#define CONFIG_LUNAREDU_MICROPYTHON_ENABLE_LUNARLINK_BINDINGS 0
#endif

#ifndef CONFIG_LUNAREDU_MICROPYTHON_ENABLE_PET_BINDINGS
#define CONFIG_LUNAREDU_MICROPYTHON_ENABLE_PET_BINDINGS 0
#endif



extern "C" {
#include "py/obj.h"
#include "py/objlist.h"
#include "py/objmodule.h"
#include "py/qstr.h"
#include "py/runtime.h"
}

using namespace lunaredu::lunaros;
using lunaredu::micropython::NlrCleanup;
using lunaredu::micropython::nlr_convert;
using namespace lunaredu::micropython;

namespace {

#if CONFIG_LUNAREDU_DEVICE_ENABLE_WS2812
constexpr const char *kCapabilityRgb = "ws2812_module";
#endif
#if CONFIG_LUNAREDU_DEVICE_ENABLE_MPU6050
constexpr const char *kCapabilityImu = "mpu6050_module";
#endif
#if CONFIG_LUNAREDU_DEVICE_ENABLE_CAMERA
constexpr const char *kCapabilityCamera = "camera_media_module";
#endif
#if CONFIG_LUNAREDU_DEVICE_ENABLE_TFCARD
constexpr const char *kCapabilityTfCard = "tfcard_store";
#endif
constexpr const char *kCapabilityFilesystem = "littlefs_service";
#if CONFIG_LUNAREDU_DEVICE_ENABLE_SERVO_PWM
constexpr const char *kCapabilityServo = "servo_pwm";
#endif
#if CONFIG_LUNAREDU_DEVICE_ENABLE_ASRPRO
constexpr const char *kCapabilityAsrpro = "asrpro";
#endif
#if CONFIG_LUNAREDU_MICROPYTHON_ENABLE_VISION_BINDINGS
constexpr const char *kCapabilityVision = "vision_module";
#endif
#if CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD
constexpr const char *kCapabilityScreen = "screen_canvas";
#endif

struct CapabilityMatchContext {
  const char *name = nullptr;
  bool matched = false;
};

void match_capability_visitor(const DeviceCapabilityDescriptor &descriptor,
                              void *context) {
  CapabilityMatchContext *match = static_cast<CapabilityMatchContext *>(context);
  if (match == nullptr || match->matched || match->name == nullptr) {
    return;
  }

  if (descriptor.id != nullptr && strcmp(descriptor.id, match->name) == 0) {
    match->matched = true;
    return;
  }

  for (size_t index = 0; index < descriptor.presence_capability_count; ++index) {
    const char *tag = descriptor.presence_capabilities[index];
    if (tag != nullptr && strcmp(tag, match->name) == 0) {
      match->matched = true;
      return;
    }
  }
}

bool capability_or_presence_available(const char *name) {
  CapabilityMatchContext context;
  context.name = name;
  auto *device = lunaredu::micropython::runtime_device_context();
  if (device != nullptr) {
    device->model.capabilities.visit(match_capability_visitor, &context);
  }
  return context.matched;
}

#if CONFIG_LUNAREDU_DEVICE_ENABLE_CAMERA
struct CameraSnapshotContext {
  std::string bytes;
};

bool append_camera_snapshot_bytes(const uint8_t *data, size_t size,
                                  void *context) {
  CameraSnapshotContext *snapshot =
      static_cast<CameraSnapshotContext *>(context);
  if (snapshot == nullptr || data == nullptr) {
    return false;
  }

  snapshot->bytes.append(reinterpret_cast<const char *>(data), size);
  return true;
}
#endif

#if CONFIG_LUNAREDU_DEVICE_ENABLE_WS2812
mp_obj_t ws2812_state_dict(const lunaredu::device::Ws2812State &state) {
  mp_obj_t dict = mp_obj_new_dict(6);
  dict_store(dict, "available", py_bool(capability_available(kCapabilityRgb)));
  dict_store(dict, "effect", py_i32(static_cast<int32_t>(state.effect)));
  dict_store(dict, "manual_override", py_bool(state.manual_override));
  dict_store(dict, "red", py_u32(state.red));
  dict_store(dict, "green", py_u32(state.green));
  dict_store(dict, "blue", py_u32(state.blue));
  return dict;
}
#endif

#if CONFIG_LUNAREDU_DEVICE_ENABLE_MPU6050
mp_obj_t mpu6050_sample_dict(const lunaredu::device::Mpu6050Sample &sample) {
  mp_obj_t dict = mp_obj_new_dict(14);
  dict_store(dict, "valid", py_bool(sample.valid));
  dict_store(dict, "seq", py_u32(sample.seq));
  dict_store(dict, "t_ms", py_u32(sample.t_ms));
  dict_store(dict, "accel_raw_x", py_i32(sample.accel_raw_x));
  dict_store(dict, "accel_raw_y", py_i32(sample.accel_raw_y));
  dict_store(dict, "accel_raw_z", py_i32(sample.accel_raw_z));
  dict_store(dict, "accel_g_x", py_float(sample.accel_g_x));
  dict_store(dict, "accel_g_y", py_float(sample.accel_g_y));
  dict_store(dict, "accel_g_z", py_float(sample.accel_g_z));
  dict_store(dict, "gyro_raw_x", py_i32(sample.gyro_raw_x));
  dict_store(dict, "gyro_raw_y", py_i32(sample.gyro_raw_y));
  dict_store(dict, "gyro_raw_z", py_i32(sample.gyro_raw_z));
  dict_store(dict, "gyro_dps_x", py_float(sample.gyro_dps_x));
  dict_store(dict, "gyro_dps_y", py_float(sample.gyro_dps_y));
  dict_store(dict, "gyro_dps_z", py_float(sample.gyro_dps_z));
  dict_store(dict, "temperature_c", py_float(sample.temperature_c));
  return dict;
}

mp_obj_t mpu6050_pose_dict(const lunaredu::device::Mpu6050Pose &pose) {
  mp_obj_t dict = mp_obj_new_dict(16);
  dict_store(dict, "valid", py_bool(pose.valid));
  dict_store(dict, "enabled", py_bool(pose.enabled));
  dict_store(dict, "gyro_calibrated", py_bool(pose.gyro_calibrated));
  dict_store(dict, "seq", py_u32(pose.seq));
  dict_store(dict, "t_ms", py_u32(pose.t_ms));
  dict_store(dict, "dt_s", py_float(pose.dt_s));
  dict_store(dict, "angle_x", py_float(pose.angle_x));
  dict_store(dict, "angle_y", py_float(pose.angle_y));
  dict_store(dict, "angle_z", py_float(pose.angle_z));
  dict_store(dict, "accel_angle_x", py_float(pose.accel_angle_x));
  dict_store(dict, "accel_angle_y", py_float(pose.accel_angle_y));
  dict_store(dict, "gyro_angle_x", py_float(pose.gyro_angle_x));
  dict_store(dict, "gyro_angle_y", py_float(pose.gyro_angle_y));
  dict_store(dict, "gyro_angle_z", py_float(pose.gyro_angle_z));
  dict_store(dict, "gyro_x_dps", py_float(pose.gyro_x_dps));
  dict_store(dict, "gyro_y_dps", py_float(pose.gyro_y_dps));
  dict_store(dict, "gyro_z_dps", py_float(pose.gyro_z_dps));
  dict_store(dict, "gyro_offset_x", py_float(pose.gyro_offset_x));
  dict_store(dict, "gyro_offset_y", py_float(pose.gyro_offset_y));
  dict_store(dict, "gyro_offset_z", py_float(pose.gyro_offset_z));
  return dict;
}

mp_obj_t mpu6050_status_dict(const lunaredu::device::Mpu6050Status &status) {
  mp_obj_t dict = mp_obj_new_dict(12);
  dict_store(dict, "available", py_bool(capability_available(kCapabilityImu)));
  dict_store(dict, "ok", py_bool(status.ok));
  dict_store(dict, "initialized", py_bool(status.initialized));
  dict_store(dict, "sampling", py_bool(status.sampling));
  dict_store(dict, "i2c_address", py_u32(status.i2c_address));
  dict_store(dict, "sample_rate_hz", py_u32(status.sample_rate_hz));
  dict_store(dict, "sample_interval_ms", py_u32(status.sample_interval_ms));
  dict_store(dict, "sample_count", py_u32(status.sample_count));
  dict_store(dict, "last_sample_ms", py_u32(status.last_sample_ms));
  dict_store(dict, "last_error", py_str(status.last_error.c_str()));
  dict_store(dict, "latest", mpu6050_sample_dict(status.latest));
  dict_store(dict, "pose", mpu6050_pose_dict(status.pose));
  return dict;
}
#endif

#if CONFIG_LUNAREDU_DEVICE_ENABLE_CAMERA
mp_obj_t camera_status_dict(const lunaredu::device::CameraMediaStatus &status) {
  mp_obj_t dict = mp_obj_new_dict(13);
  dict_store(dict, "available", py_bool(capability_available(kCapabilityCamera)));
  dict_store(dict, "camera_ready", py_bool(status.camera_ready));
  dict_store(dict, "camera_service_ready", py_bool(status.camera_service_ready));
  dict_store(dict, "supports_media_profile",
             py_bool(status.supports_media_profile));
  dict_store(dict, "supports_inference_profile",
             py_bool(status.supports_inference_profile));
  dict_store(dict, "initialized", py_bool(status.initialized));
  dict_store(dict, "busy", py_bool(status.busy));
  dict_store(dict, "frame_count", py_u32(status.frame_count));
  dict_store(dict, "active_profile", py_str(status.active_profile.c_str()));
  dict_store(dict, "last_frame_size",
             mp_obj_new_int_from_uint(status.last_frame_size));
  dict_store(dict, "last_frame_width", py_u32(status.last_frame_width));
  dict_store(dict, "last_frame_height", py_u32(status.last_frame_height));
  dict_store(dict, "last_error", py_str(status.last_error.c_str()));
  return dict;
}
#endif

#if CONFIG_LUNAREDU_DEVICE_ENABLE_TFCARD
mp_obj_t tfcard_status_dict(const lunaredu::device::TfCardStoreStatus &status) {
  mp_obj_t dict = mp_obj_new_dict(7);
  dict_store(dict, "available", py_bool(capability_available(kCapabilityTfCard)));
  dict_store(dict, "ready", py_bool(status.mounted));
  dict_store(dict, "mounted", py_bool(status.mounted));
  dict_store(dict, "type", py_str("fatfs"));
  dict_store(dict, "mount_path", py_str(status.mount_path.c_str()));
  dict_store(dict, "capacity_bytes", py_u64(status.capacity_bytes));
  dict_store(dict, "last_error", py_str(status.last_error.c_str()));
  dict_store(dict, "setup",
             py_bool(lunaredu::device::tfcard_store_setup_done()));
  return dict;
}
#endif

mp_obj_t filesystem_status_dict() {
  mp_obj_t dict = mp_obj_new_dict(7);
  dict_store(dict, "available",
             py_bool(capability_available(kCapabilityFilesystem)));
  dict_store(dict, "ready", py_bool(storage_ready()));
  dict_store(dict, "type", py_str("littlefs"));
  dict_store(dict, "mount_path", py_str(storage_mount_path()));
  dict_store(dict, "partition_label", py_str(storage_partition_label()));
  dict_store(dict, "total_bytes",
             mp_obj_new_int_from_uint(storage_total_bytes()));
  dict_store(dict, "used_bytes",
             mp_obj_new_int_from_uint(storage_used_bytes()));
  dict_store(dict, "last_error", py_str(storage_last_error()));
  return dict;
}

#if CONFIG_LUNAREDU_DEVICE_ENABLE_SERVO_PWM
mp_obj_t servo_status_dict(const lunaredu::device::ServoPwmStatus &status) {
  mp_obj_t dict = mp_obj_new_dict(9);
  dict_store(dict, "available", py_bool(capability_available(kCapabilityServo)));
  dict_store(dict, "ok", py_bool(status.ok));
  dict_store(dict, "initialized", py_bool(status.initialized));
  dict_store(dict, "left_pin", py_i32(status.left_pin));
  dict_store(dict, "right_pin", py_i32(status.right_pin));
  dict_store(dict, "left_pulse_us", py_float(status.left_pulse_us));
  dict_store(dict, "right_pulse_us", py_float(status.right_pulse_us));
  dict_store(dict, "write_count", py_u32(status.write_count));
  dict_store(dict, "last_error", py_str(status.last_error.c_str()));
  return dict;
}
#endif


#if CONFIG_LUNAREDU_DEVICE_ENABLE_ASRPRO
mp_obj_t asrpro_status_dict(const lunaredu::device::AsrproStatus &status) {
  mp_obj_t dict = mp_obj_new_dict(8);
  dict_store(dict, "available", py_bool(capability_available(kCapabilityAsrpro)));
  dict_store(dict, "ok", py_bool(status.ok));
  dict_store(dict, "initialized", py_bool(status.initialized));
  dict_store(dict, "uart_port", py_i32(status.uart_port));
  dict_store(dict, "frame_count", py_u32(status.frame_count));
  dict_store(dict, "latest_code", py_u32(status.latest_code));
  dict_store(dict, "has_unread_code", py_bool(status.has_unread_code));
  dict_store(dict, "last_error", py_str(status.last_error.c_str()));
  return dict;
}
#endif

#if CONFIG_LUNAREDU_MICROPYTHON_ENABLE_VISION_BINDINGS
mp_obj_t vision_status_dict(const VisionStatus &status) {
  mp_obj_t dict = mp_obj_new_dict(10);
  dict_store(dict, "available",
             py_bool(capability_available(kCapabilityVision)));
  dict_store(dict, "ok", py_bool(status.initialized && status.available));
  dict_store(dict, "initialized", py_bool(status.initialized));
  dict_store(dict, "runtime_available", py_bool(status.available));
  dict_store(dict, "face_detection_ready",
             py_bool(status.face_detection_ready));
  dict_store(dict, "face_recognition_ready",
             py_bool(status.face_recognition_ready));
  dict_store(dict, "busy", py_bool(status.busy));
  dict_store(dict, "backend", py_str(status.backend.c_str()));
  dict_store(dict, "state", py_str(status.state.c_str()));
  dict_store(dict, "last_error", py_str(status.last_error.c_str()));
  return dict;
}
#endif

#if CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD
mp_obj_t screen_status_dict(
    const lunaredu::device::DeviceSpiLcdSurfaceStatus &status) {
  mp_obj_t dict = mp_obj_new_dict(13);
  dict_store(dict, "available",
             py_bool(capability_available(kCapabilityScreen)));
  dict_store(dict, "configured", py_bool(status.configured));
  dict_store(dict, "initialized", py_bool(status.initialized));
  dict_store(dict, "backend", py_str("spi_lcd"));
  dict_store(
      dict, "pixel_format",
      py_str(status.source_pixel_format ==
                     lunaredu::device::DeviceSpiLcdSourcePixelFormat::kRgb666U8x3
                 ? "rgb666-u8x3"
                 : "rgb565"));
  dict_store(dict, "width", py_u32(status.mirror_width));
  dict_store(dict, "height", py_u32(status.mirror_height));
  dict_store(dict, "frame_count", py_u32(status.frame_count));
  dict_store(dict, "last_operation", py_str(status.last_pattern.c_str()));
  dict_store(dict, "last_error", py_str(status.last_error.c_str()));
  dict_store(dict, "mirror_available", py_bool(status.mirror_available));
  dict_store(dict, "mirror_generation", py_u32(status.mirror_generation));
  dict_store(dict, "canvas_override_active",
             py_bool(status.canvas_override_active));
  return dict;
}
#endif


#if CONFIG_LUNAREDU_MICROPYTHON_ENABLE_VISION_BINDINGS
mp_obj_t vision_identity_dict(const VisionIdentityResult &identity) {
  mp_obj_t dict = mp_obj_new_dict(5);
  dict_store(dict, "attempted", py_bool(identity.attempted));
  dict_store(dict, "matched", py_bool(identity.matched));
  dict_store(dict, "id", py_i32(identity.id));
  dict_store(dict, "similarity", py_float(identity.similarity));
  dict_store(dict, "label", py_str(identity.label.c_str()));
  return dict;
}

mp_obj_t vision_detection_dict(const VisionDetection &detection) {
  mp_obj_t dict = mp_obj_new_dict(9);
  dict_store(dict, "label", py_str(detection.label.c_str()));
  dict_store(dict, "score", py_float(detection.score));
  dict_store(dict, "x", py_u32(detection.x));
  dict_store(dict, "y", py_u32(detection.y));
  dict_store(dict, "w", py_u32(detection.w));
  dict_store(dict, "h", py_u32(detection.h));
  dict_store(dict, "identity_id", py_i32(detection.identity_id));
  dict_store(dict, "identity_similarity",
             py_float(detection.identity_similarity));
  dict_store(dict, "identity_label",
             py_str(detection.identity_label.c_str()));
  return dict;
}

mp_obj_t vision_analysis_dict(const VisionAnalysisResult &result) {
  mp_obj_t dict = mp_obj_new_dict(20);
  dict_store(dict, "ok", py_bool(result.ok));
  dict_store(dict, "frame_id", py_u32(result.frame_id));
  dict_store(dict, "source_path", py_str(result.source_path.c_str()));
  dict_store(dict, "width", py_u32(result.width));
  dict_store(dict, "height", py_u32(result.height));
  dict_store(dict, "sample_count", py_u32(result.sample_count));
  dict_store(dict, "avg_luma", py_float(result.avg_luma));
  dict_store(dict, "dark_ratio", py_float(result.dark_ratio));
  dict_store(dict, "bright_ratio", py_float(result.bright_ratio));
  dict_store(dict, "avg_red", py_u32(result.avg_red));
  dict_store(dict, "avg_green", py_u32(result.avg_green));
  dict_store(dict, "avg_blue", py_u32(result.avg_blue));
  dict_store(dict, "dominant_color", py_str(result.dominant_color.c_str()));
  dict_store(dict, "scene_label", py_str(result.scene_label.c_str()));
  dict_store(dict, "confidence", py_float(result.confidence));
  dict_store(dict, "backend", py_str(result.backend.c_str()));
  dict_store(dict, "error", py_str(result.error.c_str()));
  dict_store(dict, "face_count", py_u32(result.face_count));
  dict_store(dict, "identity", vision_identity_dict(result.identity));

  mp_obj_t detections = mp_obj_new_list(0, nullptr);
  for (const VisionDetection &detection : result.detections) {
    mp_obj_list_append(detections, vision_detection_dict(detection));
  }
  dict_store(dict, "detections", detections);
  return dict;
}

mp_obj_t vision_job_status_dict(const VisionAnalysisJobStatus &status) {
  mp_obj_t dict = mp_obj_new_dict(9);
  dict_store(dict, "ok", py_bool(status.error.empty()));
  dict_store(dict, "worker_ready", py_bool(status.worker_ready));
  dict_store(dict, "busy", py_bool(status.busy));
  dict_store(dict, "has_result", py_bool(status.has_result));
  dict_store(dict, "active_job_id", py_u32(status.active_job_id));
  dict_store(dict, "last_job_id", py_u32(status.last_job_id));
  dict_store(dict, "state", py_str(status.state.c_str()));
  dict_store(dict, "error", py_str(status.error.c_str()));
  dict_store(dict, "result", vision_analysis_dict(status.result));
  return dict;
}

bool decode_jpeg_obj_to_vision_image(mp_obj_t jpeg_obj, Rgb565ImageBuffer &image,
                            std::string &error) {
  mp_buffer_info_t buffer;
  mp_get_buffer_raise(jpeg_obj, &buffer, MP_BUFFER_READ);
  return g_jpeg_workspace.decode(static_cast<const uint8_t *>(buffer.buf),
                              buffer.len, image, error);
}

mp_obj_t analyze_vision_image(const VisionImageView &image) {
  VisionAnalysisResult result;
  NlrCleanup result_cleanup(result);
  std::string error;
  NlrCleanup error_cleanup(error);
  if (!vision_analyze(image, result, error)) {
    raise_operation_failed(error.empty() ? "vision.analyze" : error.c_str());
  }
  return vision_analysis_dict(result);
}
#endif

struct CapabilityListContext {
  mp_obj_t list = mp_const_none;
};

void append_capability_visitor(const DeviceCapabilityDescriptor &descriptor,
                               void *context) {
  CapabilityListContext *list_context =
      static_cast<CapabilityListContext *>(context);
  if (list_context == nullptr) {
    return;
  }

  mp_obj_t item = mp_obj_new_dict(2);
  dict_store(item, "id", py_str(descriptor.id));

  mp_obj_t presence = mp_obj_new_list(0, nullptr);
  for (size_t index = 0; index < descriptor.presence_capability_count; ++index) {
    mp_obj_list_append(presence, py_str(descriptor.presence_capabilities[index]));
  }
  dict_store(item, "presence", presence);
  mp_obj_list_append(list_context->list, item);
}

mp_obj_t lunaredu_capabilities() {
  mp_obj_t list = mp_obj_new_list(0, nullptr);
  CapabilityListContext context;
  context.list = list;
  auto *device = lunaredu::micropython::runtime_device_context();
  if (device != nullptr) {
    device->model.capabilities.visit(append_capability_visitor, &context);
  }
  return list;
}
MP_DEFINE_CONST_FUN_OBJ_0(lunaredu_capabilities_obj, lunaredu_capabilities);

mp_obj_t lunaredu_has(mp_obj_t name_obj) {
  const char *name = mp_obj_str_get_str(name_obj);
  return py_bool(capability_or_presence_available(name));
}
MP_DEFINE_CONST_FUN_OBJ_1(lunaredu_has_obj, lunaredu_has);

mp_obj_t lunaredu_status() {
  mp_obj_t dict = mp_obj_new_dict(12);
  dict_store(dict, "capabilities", lunaredu_capabilities());
  append_app_binding_status(dict);
#if CONFIG_LUNAREDU_DEVICE_ENABLE_WS2812
  dict_store(dict, "rgb",
             nlr_convert(lunaredu::device::ws2812_state(), ws2812_state_dict));
#endif
#if CONFIG_LUNAREDU_DEVICE_ENABLE_MPU6050
  dict_store(dict, "imu",
             nlr_convert(lunaredu::device::mpu6050_status(), mpu6050_status_dict));
#endif
#if CONFIG_LUNAREDU_DEVICE_ENABLE_CAMERA
  dict_store(dict, "camera",
             nlr_convert(lunaredu::device::camera_media_status(), camera_status_dict));
#endif
#if CONFIG_LUNAREDU_DEVICE_ENABLE_TFCARD
  dict_store(dict, "tfcard",
             nlr_convert(lunaredu::device::tfcard_store_status(), tfcard_status_dict));
#endif
  dict_store(dict, "filesystem", filesystem_status_dict());
#if CONFIG_LUNAREDU_DEVICE_ENABLE_SERVO_PWM
  dict_store(dict, "servo",
             nlr_convert(lunaredu::device::servo_pwm_status(), servo_status_dict));
#endif
#if CONFIG_LUNAREDU_DEVICE_ENABLE_ASRPRO
  dict_store(dict, "asrpro",
             nlr_convert(lunaredu::device::asrpro_status(), asrpro_status_dict));
#endif
#if CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD
  dict_store(dict, "screen",
             nlr_convert(lunaredu::device::device_spi_lcd_surface_status(), screen_status_dict));
#endif
#if CONFIG_LUNAREDU_MICROPYTHON_ENABLE_VISION_BINDINGS
  dict_store(dict, "vision", nlr_convert(vision_status(), vision_status_dict));
#endif
  return dict;
}
MP_DEFINE_CONST_FUN_OBJ_0(lunaredu_status_obj, lunaredu_status);

#if CONFIG_LUNAREDU_DEVICE_ENABLE_WS2812
mp_obj_t rgb_available() { return py_bool(capability_available(kCapabilityRgb)); }
MP_DEFINE_CONST_FUN_OBJ_0(rgb_available_obj, rgb_available);

mp_obj_t rgb_state() {
  return nlr_convert(lunaredu::device::ws2812_state(), ws2812_state_dict);
}
MP_DEFINE_CONST_FUN_OBJ_0(rgb_state_obj, rgb_state);

mp_obj_t rgb_set(mp_obj_t red_obj, mp_obj_t green_obj, mp_obj_t blue_obj) {
  require_capability(kCapabilityRgb);
  const int red = mp_obj_get_int(red_obj);
  const int green = mp_obj_get_int(green_obj);
  const int blue = mp_obj_get_int(blue_obj);
  if (red < 0 || red > 255 || green < 0 || green > 255 || blue < 0 ||
      blue > 255) {
    mp_raise_ValueError(MP_ERROR_TEXT("rgb channels must be 0..255"));
  }

  lunaredu::device::set_ws2812_manual_color(static_cast<uint8_t>(red),
                                            static_cast<uint8_t>(green),
                                            static_cast<uint8_t>(blue));
  return nlr_convert(lunaredu::device::ws2812_state(), ws2812_state_dict);
}
MP_DEFINE_CONST_FUN_OBJ_3(rgb_set_obj, rgb_set);

mp_obj_t rgb_clear() {
  require_capability(kCapabilityRgb);
  lunaredu::device::clear_ws2812_manual_override();
  return nlr_convert(lunaredu::device::ws2812_state(), ws2812_state_dict);
}
MP_DEFINE_CONST_FUN_OBJ_0(rgb_clear_obj, rgb_clear);
#endif

#if CONFIG_LUNAREDU_DEVICE_ENABLE_MPU6050
mp_obj_t imu_available() { return py_bool(capability_available(kCapabilityImu)); }
MP_DEFINE_CONST_FUN_OBJ_0(imu_available_obj, imu_available);

mp_obj_t imu_status() {
  return nlr_convert(lunaredu::device::mpu6050_status(), mpu6050_status_dict);
}
MP_DEFINE_CONST_FUN_OBJ_0(imu_status_obj, imu_status);

mp_obj_t imu_pose() {
  return nlr_convert(lunaredu::device::mpu6050_pose(), mpu6050_pose_dict);
}
MP_DEFINE_CONST_FUN_OBJ_0(imu_pose_obj, imu_pose);

mp_obj_t imu_start() {
  require_capability(kCapabilityImu);
  std::string error;
  NlrCleanup error_cleanup(error);
  if (!lunaredu::device::mpu6050_start_sampling(error)) {
    raise_operation_failed(error.empty() ? "imu.start" : error.c_str());
  }
  return nlr_convert(lunaredu::device::mpu6050_status(), mpu6050_status_dict);
}
MP_DEFINE_CONST_FUN_OBJ_0(imu_start_obj, imu_start);

mp_obj_t imu_stop() {
  require_capability(kCapabilityImu);
  lunaredu::device::mpu6050_stop_sampling();
  return nlr_convert(lunaredu::device::mpu6050_status(), mpu6050_status_dict);
}
MP_DEFINE_CONST_FUN_OBJ_0(imu_stop_obj, imu_stop);

mp_obj_t imu_set_rate(mp_obj_t rate_obj) {
  require_capability(kCapabilityImu);
  const int rate = mp_obj_get_int(rate_obj);
  if (rate <= 0 || rate > 1000) {
    mp_raise_ValueError(MP_ERROR_TEXT("sample rate must be 1..1000"));
  }

  std::string error;
  NlrCleanup error_cleanup(error);
  if (!lunaredu::device::mpu6050_set_sample_rate_hz(
          static_cast<uint16_t>(rate), error)) {
    raise_operation_failed(error.empty() ? "imu.set_rate" : error.c_str());
  }
  return nlr_convert(lunaredu::device::mpu6050_status(), mpu6050_status_dict);
}
MP_DEFINE_CONST_FUN_OBJ_1(imu_set_rate_obj, imu_set_rate);
#endif

#if CONFIG_LUNAREDU_DEVICE_ENABLE_CAMERA
mp_obj_t camera_available() {
  return py_bool(capability_available(kCapabilityCamera));
}
MP_DEFINE_CONST_FUN_OBJ_0(camera_available_obj, camera_available);

mp_obj_t camera_status() {
  return nlr_convert(lunaredu::device::camera_media_status(), camera_status_dict);
}
MP_DEFINE_CONST_FUN_OBJ_0(camera_status_obj, camera_status);

mp_obj_t camera_snapshot() {
  require_capability(kCapabilityCamera);
  CameraSnapshotContext context;
  NlrCleanup context_cleanup(context);
  std::string error;
  NlrCleanup error_cleanup(error);
  if (!lunaredu::device::camera_media_capture_jpeg(
          append_camera_snapshot_bytes, &context, error)) {
    raise_operation_failed(error.empty() ? "camera.snapshot" : error.c_str());
  }

  return mp_obj_new_bytes(
      reinterpret_cast<const byte *>(context.bytes.data()), context.bytes.size());
}
MP_DEFINE_CONST_FUN_OBJ_0(camera_snapshot_obj, camera_snapshot);
#endif

#if CONFIG_LUNAREDU_DEVICE_ENABLE_TFCARD
mp_obj_t tfcard_available() {
  return py_bool(capability_available(kCapabilityTfCard));
}
MP_DEFINE_CONST_FUN_OBJ_0(tfcard_available_obj, tfcard_available);

mp_obj_t tfcard_status() {
  return nlr_convert(lunaredu::device::tfcard_store_status(), tfcard_status_dict);
}
MP_DEFINE_CONST_FUN_OBJ_0(tfcard_status_obj, tfcard_status);

mp_obj_t tfcard_mount() {
  if (!lunaredu::device::tfcard_store_mount()) {
    raise_operation_failed("tfcard.mount");
  }
  return nlr_convert(lunaredu::device::tfcard_store_status(), tfcard_status_dict);
}
MP_DEFINE_CONST_FUN_OBJ_0(tfcard_mount_obj, tfcard_mount);

mp_obj_t tfcard_unmount() {
  if (!lunaredu::device::tfcard_store_unmount()) {
    raise_operation_failed("tfcard.unmount");
  }
  return nlr_convert(lunaredu::device::tfcard_store_status(), tfcard_status_dict);
}
MP_DEFINE_CONST_FUN_OBJ_0(tfcard_unmount_obj, tfcard_unmount);
#endif

mp_obj_t filesystem_available() {
  return py_bool(capability_available(kCapabilityFilesystem));
}
MP_DEFINE_CONST_FUN_OBJ_0(filesystem_available_obj, filesystem_available);

mp_obj_t filesystem_status() { return filesystem_status_dict(); }
MP_DEFINE_CONST_FUN_OBJ_0(filesystem_status_obj, filesystem_status);

#if CONFIG_LUNAREDU_DEVICE_ENABLE_SERVO_PWM
mp_obj_t servo_available() {
  return py_bool(capability_available(kCapabilityServo));
}
MP_DEFINE_CONST_FUN_OBJ_0(servo_available_obj, servo_available);

mp_obj_t servo_status() {
  return nlr_convert(lunaredu::device::servo_pwm_status(), servo_status_dict);
}
MP_DEFINE_CONST_FUN_OBJ_0(servo_status_obj, servo_status);

mp_obj_t servo_write(mp_obj_t left_obj, mp_obj_t right_obj) {
  require_capability(kCapabilityServo);
  const float left =
      left_obj == mp_const_none ? -1.0f : mp_obj_get_float_to_f(left_obj);
  const float right =
      right_obj == mp_const_none ? -1.0f : mp_obj_get_float_to_f(right_obj);
  lunaredu::device::servo_pwm_write(left, right);
  return nlr_convert(lunaredu::device::servo_pwm_status(), servo_status_dict);
}
MP_DEFINE_CONST_FUN_OBJ_2(servo_write_obj, servo_write);
#endif


#if CONFIG_LUNAREDU_DEVICE_ENABLE_ASRPRO
mp_obj_t asrpro_available() {
  return py_bool(capability_available(kCapabilityAsrpro));
}
MP_DEFINE_CONST_FUN_OBJ_0(asrpro_available_obj, asrpro_available);

mp_obj_t py_asrpro_status() {
  return nlr_convert(lunaredu::device::asrpro_status(), asrpro_status_dict);
}
MP_DEFINE_CONST_FUN_OBJ_0(asrpro_status_obj, py_asrpro_status);

mp_obj_t asrpro_take() {
  require_capability(kCapabilityAsrpro);
  uint8_t code = 0;
  const bool has_code = lunaredu::device::asrpro_take_code(&code);
  mp_obj_t dict = mp_obj_new_dict(2);
  dict_store(dict, "has_code", py_bool(has_code));
  dict_store(dict, "code", py_u32(code));
  return dict;
}
MP_DEFINE_CONST_FUN_OBJ_0(asrpro_take_obj, asrpro_take);
#endif

#if CONFIG_LUNAREDU_MICROPYTHON_ENABLE_VISION_BINDINGS
mp_obj_t vision_face_template_dict(const VisionFaceTemplate &face_template) {
  mp_obj_t dict = mp_obj_new_dict(4);
  dict_store(dict, "extracted", py_bool(face_template.extracted));
  dict_store(dict, "backend", py_str(face_template.backend.c_str()));
  dict_store(dict, "encoding", py_str(face_template.encoding.c_str()));
  mp_obj_t values = mp_obj_new_list(0, nullptr);
  for (float value : face_template.values) {
    mp_obj_list_append(values, py_float(value));
  }
  dict_store(dict, "values", values);
  return dict;
}
#endif

#if CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD
mp_obj_t screen_available() {
  return py_bool(capability_available(kCapabilityScreen));
}
MP_DEFINE_CONST_FUN_OBJ_0(screen_available_obj, screen_available);

mp_obj_t screen_status() {
  return nlr_convert(lunaredu::device::device_spi_lcd_surface_status(), screen_status_dict);
}
MP_DEFINE_CONST_FUN_OBJ_0(screen_status_obj, screen_status);

uint32_t screen_color(mp_obj_t color_obj) {
  return py_rgb888_color(color_obj);
}

uint16_t screen_coordinate(mp_obj_t value_obj, const char *message,
                           bool allow_zero) {
  const mp_int_t value = mp_obj_get_int(value_obj);
  if (value < (allow_zero ? 0 : 1) || value > UINT16_MAX) {
    mp_raise_ValueError(message);
  }
  return static_cast<uint16_t>(value);
}

mp_obj_t screen_fill(mp_obj_t color_obj) {
  require_capability(kCapabilityScreen);
  std::string error;
  NlrCleanup error_cleanup(error);
  if (!lunaredu::device::device_spi_lcd_surface_fill_rgb666(
          screen_color(color_obj), &error)) {
    raise_operation_failed(error.empty() ? "screen.fill" : error.c_str());
  }
  return screen_status();
}
MP_DEFINE_CONST_FUN_OBJ_1(screen_fill_obj, screen_fill);

mp_obj_t screen_fill_rect(size_t n_args, const mp_obj_t *args) {
  (void)n_args;
  require_capability(kCapabilityScreen);
  const uint16_t x = screen_coordinate(
      args[0], MP_ERROR_TEXT("x must be 0..65535"), true);
  const uint16_t y = screen_coordinate(
      args[1], MP_ERROR_TEXT("y must be 0..65535"), true);
  const uint16_t width = screen_coordinate(
      args[2], MP_ERROR_TEXT("width must be 1..65535"), false);
  const uint16_t height = screen_coordinate(
      args[3], MP_ERROR_TEXT("height must be 1..65535"), false);
  const uint32_t color = screen_color(args[4]);

  std::string error;
  NlrCleanup error_cleanup(error);
  if (!lunaredu::device::device_spi_lcd_surface_fill_rect_rgb666(
          x, y, width, height, color, &error)) {
    raise_operation_failed(error.empty() ? "screen.fill_rect"
                                         : error.c_str());
  }
  return screen_status();
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(screen_fill_rect_obj, 5, 5,
                                    screen_fill_rect);

mp_obj_t screen_draw_rgb565(size_t n_args, const mp_obj_t *args) {
  require_capability(kCapabilityScreen);
  if (lunaredu::device::device_spi_lcd_surface_source_pixel_format() !=
      lunaredu::device::DeviceSpiLcdSourcePixelFormat::kRgb565) {
    mp_raise_ValueError(
        MP_ERROR_TEXT("draw_rgb565 is unavailable on this display"));
  }
  const uint16_t x = screen_coordinate(
      args[0], MP_ERROR_TEXT("x must be 0..65535"), true);
  const uint16_t y = screen_coordinate(
      args[1], MP_ERROR_TEXT("y must be 0..65535"), true);
  const uint16_t width = screen_coordinate(
      args[2], MP_ERROR_TEXT("width must be 1..65535"), false);
  const uint16_t height = screen_coordinate(
      args[3], MP_ERROR_TEXT("height must be 1..65535"), false);

  mp_buffer_info_t buffer;
  mp_get_buffer_raise(args[4], &buffer, MP_BUFFER_READ);
  const size_t pixel_count = static_cast<size_t>(width) * height;
  const size_t expected_bytes = pixel_count * sizeof(uint16_t);
  if (buffer.len != expected_bytes) {
    mp_raise_ValueError(MP_ERROR_TEXT("RGB565 buffer size mismatch"));
  }

  bool big_endian = false;
  if (n_args == 6) {
    const char *byte_order = mp_obj_str_get_str(args[5]);
    if (strcmp(byte_order, "little") == 0) {
      big_endian = false;
    } else if (strcmp(byte_order, "big") == 0) {
      big_endian = true;
    } else {
      mp_raise_ValueError(MP_ERROR_TEXT("byte_order must be little or big"));
    }
  }

  const auto *bytes = static_cast<const uint8_t *>(buffer.buf);
  std::unique_ptr<uint16_t[]> pixels(
      new (std::nothrow) uint16_t[pixel_count]);
  NlrCleanup pixels_cleanup(pixels);
  if (pixels == nullptr) {
    mp_raise_msg(&mp_type_MemoryError,
                 MP_ERROR_TEXT("RGB565 frame allocation failed"));
  }
  for (size_t index = 0; index < pixel_count; ++index) {
    const uint8_t first = bytes[index * 2];
    const uint8_t second = bytes[index * 2 + 1];
    pixels[index] = big_endian
                        ? static_cast<uint16_t>((first << 8) | second)
                        : static_cast<uint16_t>(first | (second << 8));
  }

  std::string error;
  NlrCleanup error_cleanup(error);
  if (!lunaredu::device::device_spi_lcd_surface_draw_rgb565_bitmap(
          x, y, width, height, pixels.get(), false, &error)) {
    raise_operation_failed(error.empty() ? "screen.draw_rgb565"
                                         : error.c_str());
  }
  return screen_status();
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(screen_draw_rgb565_obj, 5, 6,
                                    screen_draw_rgb565);

mp_obj_t screen_draw_rgb666(size_t n_args, const mp_obj_t *args) {
  (void)n_args;
  require_capability(kCapabilityScreen);
  if (lunaredu::device::device_spi_lcd_surface_source_pixel_format() !=
      lunaredu::device::DeviceSpiLcdSourcePixelFormat::kRgb666U8x3) {
    mp_raise_ValueError(
        MP_ERROR_TEXT("draw_rgb666 is unavailable on this display"));
  }
  const uint16_t x = screen_coordinate(
      args[0], MP_ERROR_TEXT("x must be 0..65535"), true);
  const uint16_t y = screen_coordinate(
      args[1], MP_ERROR_TEXT("y must be 0..65535"), true);
  const uint16_t width = screen_coordinate(
      args[2], MP_ERROR_TEXT("width must be 1..65535"), false);
  const uint16_t height = screen_coordinate(
      args[3], MP_ERROR_TEXT("height must be 1..65535"), false);

  mp_buffer_info_t buffer;
  mp_get_buffer_raise(args[4], &buffer, MP_BUFFER_READ);
  const size_t pixel_count = static_cast<size_t>(width) * height;
  const size_t expected_bytes = pixel_count * 3U;
  if (buffer.len != expected_bytes) {
    mp_raise_ValueError(MP_ERROR_TEXT("RGB888 buffer size mismatch"));
  }

  std::string error;
  NlrCleanup error_cleanup(error);
  if (!lunaredu::device::device_spi_lcd_surface_draw_rgb666_bitmap(
          x, y, width, height, static_cast<const uint8_t *>(buffer.buf),
          lunaredu::device::DeviceSpiLcdRgb666Order::kRgb, &error)) {
    raise_operation_failed(error.empty() ? "screen.draw_rgb666"
                                         : error.c_str());
  }
  return screen_status();
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(screen_draw_rgb666_obj, 5, 5,
                                    screen_draw_rgb666);
#endif




#if CONFIG_LUNAREDU_MICROPYTHON_ENABLE_VISION_BINDINGS
mp_obj_t py_vision_available() {
  return py_bool(capability_available(kCapabilityVision));
}
MP_DEFINE_CONST_FUN_OBJ_0(vision_available_obj, py_vision_available);

mp_obj_t py_vision_status() {
  return nlr_convert(vision_status(), vision_status_dict);
}
MP_DEFINE_CONST_FUN_OBJ_0(vision_status_obj, py_vision_status);

mp_obj_t py_vision_job_status() {
  return nlr_convert(vision_analysis_job_status(), vision_job_status_dict);
}
MP_DEFINE_CONST_FUN_OBJ_0(vision_job_status_obj, py_vision_job_status);

mp_obj_t py_vision_analyze(mp_obj_t jpeg_obj) {
  require_capability(kCapabilityVision);
  Rgb565ImageBuffer image;
  NlrCleanup image_cleanup(image);
  std::string error;
  NlrCleanup error_cleanup(error);
  if (!decode_jpeg_obj_to_vision_image(jpeg_obj, image, error)) {
    raise_operation_failed(error.empty() ? "image_codec.decode_jpeg" : error.c_str());
  }
  return analyze_vision_image(VisionImageView{image.data.data(), image.data.size(), image.width, image.height, VisionImageFormat::Rgb565});
}
MP_DEFINE_CONST_FUN_OBJ_1(vision_analyze_obj, py_vision_analyze);

#if CONFIG_LUNAREDU_DEVICE_ENABLE_CAMERA
mp_obj_t py_vision_analyze_snapshot() {
  require_capability(kCapabilityVision);
  require_capability(kCapabilityCamera);

  CameraSnapshotContext context;
  NlrCleanup context_cleanup(context);
  std::string error;
  NlrCleanup error_cleanup(error);
  if (!lunaredu::device::camera_media_capture_jpeg(
          append_camera_snapshot_bytes, &context, error)) {
    raise_operation_failed(error.empty() ? "camera.snapshot" : error.c_str());
  }

  Rgb565ImageBuffer image;
  NlrCleanup image_cleanup(image);
  if (!g_jpeg_workspace.decode(reinterpret_cast<const uint8_t *>(context.bytes.data()),
                          context.bytes.size(), image, error)) {
    raise_operation_failed(error.empty() ? "image_codec.decode_jpeg" : error.c_str());
  }
  return analyze_vision_image(VisionImageView{image.data.data(), image.data.size(), image.width, image.height, VisionImageFormat::Rgb565});
}
MP_DEFINE_CONST_FUN_OBJ_0(vision_analyze_snapshot_obj,
                          py_vision_analyze_snapshot);
#endif

mp_obj_t py_vision_extract_face_template(mp_obj_t jpeg_obj) {
  require_capability(kCapabilityVision);
  Rgb565ImageBuffer image;
  NlrCleanup image_cleanup(image);
  std::string error;
  NlrCleanup error_cleanup(error);
  if (!decode_jpeg_obj_to_vision_image(jpeg_obj, image, error)) {
    raise_operation_failed(error.empty() ? "image_codec.decode_jpeg" : error.c_str());
  }

  VisionFaceTemplate face_template;
  NlrCleanup face_template_cleanup(face_template);
  if (!vision_extract_face_template(VisionImageView{image.data.data(), image.data.size(), image.width, image.height, VisionImageFormat::Rgb565}, face_template, error)) {
    raise_operation_failed(error.empty() ? "vision.extract_face_template"
                                         : error.c_str());
  }
  return vision_face_template_dict(face_template);
}
MP_DEFINE_CONST_FUN_OBJ_1(vision_extract_face_template_obj,
                          py_vision_extract_face_template);
#endif

} // namespace

void setup_micropython_lunaredu_module() {
  mp_obj_t root = make_module("lunaredu");
  register_app_binding_modules(root);
  module_store(root, "capabilities", MP_OBJ_FROM_PTR(&lunaredu_capabilities_obj));
  module_store(root, "has", MP_OBJ_FROM_PTR(&lunaredu_has_obj));
  module_store(root, "status", MP_OBJ_FROM_PTR(&lunaredu_status_obj));

#if CONFIG_LUNAREDU_DEVICE_ENABLE_WS2812
  mp_obj_t rgb = make_module("lunaredu.rgb");
  module_store(rgb, "available", MP_OBJ_FROM_PTR(&rgb_available_obj));
  module_store(rgb, "state", MP_OBJ_FROM_PTR(&rgb_state_obj));
  module_store(rgb, "set", MP_OBJ_FROM_PTR(&rgb_set_obj));
  module_store(rgb, "clear", MP_OBJ_FROM_PTR(&rgb_clear_obj));
  register_submodule(root, "rgb", rgb);
#endif

#if CONFIG_LUNAREDU_DEVICE_ENABLE_MPU6050
  mp_obj_t imu = make_module("lunaredu.imu");
  module_store(imu, "available", MP_OBJ_FROM_PTR(&imu_available_obj));
  module_store(imu, "status", MP_OBJ_FROM_PTR(&imu_status_obj));
  module_store(imu, "pose", MP_OBJ_FROM_PTR(&imu_pose_obj));
  module_store(imu, "start", MP_OBJ_FROM_PTR(&imu_start_obj));
  module_store(imu, "stop", MP_OBJ_FROM_PTR(&imu_stop_obj));
  module_store(imu, "set_rate", MP_OBJ_FROM_PTR(&imu_set_rate_obj));
  register_submodule(root, "imu", imu);
#endif

#if CONFIG_LUNAREDU_DEVICE_ENABLE_CAMERA
  mp_obj_t camera = make_module("lunaredu.camera");
  module_store(camera, "available", MP_OBJ_FROM_PTR(&camera_available_obj));
  module_store(camera, "status", MP_OBJ_FROM_PTR(&camera_status_obj));
  module_store(camera, "snapshot", MP_OBJ_FROM_PTR(&camera_snapshot_obj));
  register_submodule(root, "camera", camera);
#endif

#if CONFIG_LUNAREDU_DEVICE_ENABLE_TFCARD
  mp_obj_t tfcard = make_module("lunaredu.tfcard");
  module_store(tfcard, "available", MP_OBJ_FROM_PTR(&tfcard_available_obj));
  module_store(tfcard, "status", MP_OBJ_FROM_PTR(&tfcard_status_obj));
  module_store(tfcard, "mount", MP_OBJ_FROM_PTR(&tfcard_mount_obj));
  module_store(tfcard, "unmount", MP_OBJ_FROM_PTR(&tfcard_unmount_obj));
  register_submodule(root, "tfcard", tfcard);
#endif

  mp_obj_t filesystem = make_module("lunaredu.filesystem");
  module_store(filesystem, "available",
               MP_OBJ_FROM_PTR(&filesystem_available_obj));
  module_store(filesystem, "status", MP_OBJ_FROM_PTR(&filesystem_status_obj));
  register_submodule(root, "filesystem", filesystem);

#if CONFIG_LUNAREDU_DEVICE_ENABLE_SERVO_PWM
  mp_obj_t servo = make_module("lunaredu.servo");
  module_store(servo, "available", MP_OBJ_FROM_PTR(&servo_available_obj));
  module_store(servo, "status", MP_OBJ_FROM_PTR(&servo_status_obj));
  module_store(servo, "write", MP_OBJ_FROM_PTR(&servo_write_obj));
  register_submodule(root, "servo", servo);
#endif


#if CONFIG_LUNAREDU_DEVICE_ENABLE_ASRPRO
  mp_obj_t asrpro = make_module("lunaredu.asrpro");
  module_store(asrpro, "available", MP_OBJ_FROM_PTR(&asrpro_available_obj));
  module_store(asrpro, "status", MP_OBJ_FROM_PTR(&asrpro_status_obj));
  module_store(asrpro, "take", MP_OBJ_FROM_PTR(&asrpro_take_obj));
  register_submodule(root, "asrpro", asrpro);
#endif

#if CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD
  mp_obj_t screen = make_module("lunaredu.screen");
  module_store(screen, "available", MP_OBJ_FROM_PTR(&screen_available_obj));
  module_store(screen, "status", MP_OBJ_FROM_PTR(&screen_status_obj));
  module_store(screen, "fill", MP_OBJ_FROM_PTR(&screen_fill_obj));
  module_store(screen, "fill_rect", MP_OBJ_FROM_PTR(&screen_fill_rect_obj));
  if (lunaredu::device::device_spi_lcd_surface_source_pixel_format() ==
      lunaredu::device::DeviceSpiLcdSourcePixelFormat::kRgb666U8x3) {
    module_store(screen, "draw_rgb666",
                 MP_OBJ_FROM_PTR(&screen_draw_rgb666_obj));
  } else {
    module_store(screen, "draw_rgb565",
                 MP_OBJ_FROM_PTR(&screen_draw_rgb565_obj));
  }
  register_submodule(root, "screen", screen);
#endif



#if CONFIG_LUNAREDU_MICROPYTHON_ENABLE_VISION_BINDINGS
  mp_obj_t vision = make_module("lunaredu.vision");
  module_store(vision, "available", MP_OBJ_FROM_PTR(&vision_available_obj));
  module_store(vision, "status", MP_OBJ_FROM_PTR(&vision_status_obj));
  module_store(vision, "job_status", MP_OBJ_FROM_PTR(&vision_job_status_obj));
  module_store(vision, "analyze", MP_OBJ_FROM_PTR(&vision_analyze_obj));
#if CONFIG_LUNAREDU_DEVICE_ENABLE_CAMERA
  module_store(vision, "analyze_snapshot",
               MP_OBJ_FROM_PTR(&vision_analyze_snapshot_obj));
#endif
  module_store(vision, "extract_face_template",
               MP_OBJ_FROM_PTR(&vision_extract_face_template_obj));
  register_submodule(root, "vision", vision);
#endif

}

void retire_micropython_lunaredu_module(){
#if CONFIG_LUNAREDU_MICROPYTHON_ENABLE_VISION_BINDINGS
  g_jpeg_workspace.retire();
#endif
}
