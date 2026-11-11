#include "pet_frame_api.h"
#include "product/desktop_resources.h"
#include "lunaredu_lunaros/device_api/device_projection_json.h"

#include <algorithm>
#include <cstring>

namespace smart_speaker {
using namespace lunaredu::lunaros;
DeviceOperationResult handle_pet_frame_upload(const DeviceBulkRequest &request, JsonObject response) {
  const auto clip = request.arg ? request.arg("clip", request.context) : "";
  const auto frame_arg = request.arg ? request.arg("frame", request.context) : "";
  char *end = nullptr;
  const unsigned long index = std::strtoul(frame_arg.c_str(), &end, 10);
  if (frame_arg.empty() || end == frame_arg.c_str() || *end != '\0' || index > 255)
    return device_operation_error("invalid_pet_frame", 400);
  product::PetFrame frame;
  if (!product::pet_frame_parse(clip, static_cast<unsigned>(index), frame))
    return device_operation_error("invalid_pet_frame", 400);
  const size_t size = std::max(request.body_size, request.content_length);
  if (size != product::kPetFramePixels * sizeof(uint16_t))
    return device_operation_error("pet_frame_size_invalid", 413);
  product::WallpaperBmp bytes;
  if (!bytes.resize(size)) return device_operation_error("pet_frame_memory_unavailable", 503);
  if (request.body_data && request.body_size == size) {
    std::memcpy(bytes.data(), request.body_data, size);
  } else if (request.read_chunk && request.content_length == size) {
    size_t received = 0;
    while (received < size) {
      const size_t wanted = std::min<size_t>(2048, size - received);
      const int got = request.read_chunk(bytes.data() + received, wanted, request.context);
      if (got <= 0 || static_cast<size_t>(got) > wanted)
        return device_operation_error("pet_frame_read_failed", 400);
      received += static_cast<size_t>(got);
    }
  } else {
    return device_operation_error("pet_frame_read_failed", 400);
  }
  std::string error;
  if (!product::pet_frame_upload(frame, bytes.data(), size, error))
    return device_operation_error(error.c_str(), 409);
  response["clip"] = clip;
  response["frame"] = static_cast<unsigned>(index);
  response["stored"] = true;
  return device_operation_ack();
}
}
