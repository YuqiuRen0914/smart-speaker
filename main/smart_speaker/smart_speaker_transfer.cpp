#include "transfer_internal.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>
#include <cctype>
#include <esp_log.h>
namespace smart_speaker {
using namespace lunaredu::lunaros;
namespace detail {
constexpr const char *kTag="speaker_transfer";
std::string lower_copy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  return value;
}
bool ends_with_mp3(const std::string &name) {
  const std::string lower = lower_copy(name);
  return lower.size() > 4 &&
         lower.compare(lower.size() - 4, 4, ".mp3") == 0;
}
constexpr size_t kUploadChunkBytes=2048;
constexpr size_t kMaxUploadNameLength=96;
struct UploadNormalizationState {
  bool first_chunk = true;
  bool frame_found = false;
  size_t skip_remaining = 0;
  size_t stripped_prefix_bytes = 0;
  std::vector<uint8_t> sync_pending;
  std::vector<uint8_t> tail;
};
TransferStatus g_transfer;
std::mutex g_transfer_mutex;
static std::atomic<uint32_t> g_transfer_dismissed{0};
bool safe_upload_name(const std::string &name) {
  if (name.empty() || name.size() > kMaxUploadNameLength ||
      !ends_with_mp3(name)) {
    return false;
  }
  for (unsigned char ch : name) if (ch < 32 || ch == 127) return false;
  return name.find('/') == std::string::npos &&
         name.find('\\') == std::string::npos &&
         name.find_first_of(":*?\"<>|") == std::string::npos &&
         name.find("..") == std::string::npos;
}

size_t id3v2_skip_length(const uint8_t *data, size_t size) {
  if (data == nullptr || size < 10 || data[0] != 'I' || data[1] != 'D' ||
      data[2] != '3') {
    return 0;
  }
  for (size_t index = 6; index < 10; ++index) {
    if ((data[index] & 0x80) != 0) {
      return 0;
    }
  }
  const size_t tag_size = (static_cast<size_t>(data[6]) << 21) |
                          (static_cast<size_t>(data[7]) << 14) |
                          (static_cast<size_t>(data[8]) << 7) |
                          static_cast<size_t>(data[9]);
  const bool has_footer = (data[5] & 0x10) != 0;
  return 10 + tag_size + (has_footer ? 10 : 0);
}

bool looks_like_mpeg_frame_header(const uint8_t *data, size_t size,
                                  size_t offset) {
  if (data == nullptr || offset + 4 > size || data[offset] != 0xFF ||
      (data[offset + 1] & 0xE0) != 0xE0) {
    return false;
  }
  const uint8_t version = data[offset + 1] & 0x18;
  const uint8_t layer = data[offset + 1] & 0x06;
  const uint8_t bitrate = data[offset + 2] & 0xF0;
  const uint8_t sample_rate = data[offset + 2] & 0x0C;
  return version != 0x08 && layer != 0x00 && bitrate != 0x00 &&
         bitrate != 0xF0 && sample_rate != 0x0C;
}

size_t find_mpeg_frame_header(const uint8_t *data, size_t size) {
  if (data == nullptr || size < 4) {
    return size;
  }
  for (size_t offset = 0; offset + 4 <= size; ++offset) {
    if (looks_like_mpeg_frame_header(data, size, offset)) {
      return offset;
    }
  }
  return size;
}

bool write_normalized_payload(FILE *file, const uint8_t *data, size_t size,
                              UploadNormalizationState &state,
                              size_t &written) {
  if (file == nullptr || data == nullptr || size == 0) {
    return true;
  }

  std::vector<uint8_t> combined;
  combined.reserve(state.tail.size() + size);
  combined.insert(combined.end(), state.tail.begin(), state.tail.end());
  combined.insert(combined.end(), data, data + size);

  if (combined.size() <= 128) {
    state.tail = std::move(combined);
    return true;
  }

  const size_t write_size = combined.size() - 128;
  if (fwrite(combined.data(), 1, write_size, file) != write_size) {
    return false;
  }
  written += write_size;
  state.tail.assign(combined.begin() + write_size, combined.end());
  return true;
}

bool write_normalized_upload_chunk(FILE *file, const uint8_t *data, size_t size,
                                   UploadNormalizationState &state,
                                   size_t &written) {
  if (file == nullptr || data == nullptr || size == 0) {
    return true;
  }
  if (state.first_chunk) {
    state.first_chunk = false;
    state.skip_remaining = id3v2_skip_length(data, size);
    if (state.skip_remaining > 0) {
      ESP_LOGI(kTag, "upload normalization stripping id3v2 bytes=%u",
               static_cast<unsigned>(state.skip_remaining));
    }
  }

  size_t offset = 0;
  if (state.skip_remaining > 0) {
    const size_t drop = std::min(state.skip_remaining, size);
    state.skip_remaining -= drop;
    offset = drop;
  }
  if (offset >= size) {
    return true;
  }

  if (!state.frame_found) {
    std::vector<uint8_t> search;
    search.reserve(state.sync_pending.size() + size - offset);
    search.insert(search.end(), state.sync_pending.begin(),
                  state.sync_pending.end());
    search.insert(search.end(), data + offset, data + size);

    const size_t frame_offset = find_mpeg_frame_header(search.data(),
                                                       search.size());
    if (frame_offset >= search.size()) {
      const size_t keep = std::min<size_t>(3, search.size());
      state.stripped_prefix_bytes += search.size() - keep;
      state.sync_pending.assign(search.end() - keep, search.end());
      return true;
    }

    state.stripped_prefix_bytes += frame_offset;
    state.sync_pending.clear();
    state.frame_found = true;
    if (state.stripped_prefix_bytes > 0) {
      ESP_LOGI(kTag, "upload normalization stripped prefix bytes=%u",
               static_cast<unsigned>(state.stripped_prefix_bytes));
    }
    return write_normalized_payload(file, search.data() + frame_offset,
                                    search.size() - frame_offset, state,
                                    written);
  }

  return write_normalized_payload(file, data + offset, size - offset, state,
                                  written);
}

bool finish_normalized_upload(FILE *file, UploadNormalizationState &state,
                              size_t &written) {
  if (!state.frame_found) {
    ESP_LOGW(kTag, "upload normalization failed: no mp3 frame found");
    return false;
  }

  if (state.tail.size() == 128 && state.tail[0] == 'T' &&
      state.tail[1] == 'A' && state.tail[2] == 'G') {
    ESP_LOGI(kTag, "upload normalization stripped id3v1 tail bytes=128");
    state.tail.clear();
    return true;
  }

  if (!state.tail.empty()) {
    if (fwrite(state.tail.data(), 1, state.tail.size(), file) !=
        state.tail.size()) {
      return false;
    }
    written += state.tail.size();
    state.tail.clear();
  }
  return true;
}

bool stream_upload_to_file(const DeviceBulkRequest &request,
                           const std::string &path, size_t &written) {
  FILE *file = fopen(path.c_str(), "wb");
  if (file == nullptr) {
    return false;
  }

  bool ok = true;
  UploadNormalizationState normalization;
  if (request.body_data != nullptr && request.body_size > 0) {
    ok = write_normalized_upload_chunk(
        file, static_cast<const uint8_t *>(request.body_data),
        request.body_size, normalization, written);
  } else if (request.read_chunk != nullptr && request.content_length > 0) {
    uint8_t buffer[kUploadChunkBytes];
    size_t remaining = request.content_length;
    while (remaining > 0) {
      const size_t wanted = std::min(sizeof(buffer), remaining);
      const int read = request.read_chunk(buffer, wanted, request.context);
      if (read <= 0 || static_cast<size_t>(read) > wanted) {
        ok = false;
        break;
      }
      const size_t read_size = static_cast<size_t>(read);
      if (!write_normalized_upload_chunk(file, buffer, read_size, normalization,
                                         written)) {
        ok = false;
        break;
      }
      remaining -= read_size;
      {
        std::lock_guard<std::mutex> lock(g_transfer_mutex);
        g_transfer.received += read_size;
        g_transfer.updated_ms = now_ms();
      }
    }
  } else {
    ok = false;
  }

  if (request.body_data != nullptr && request.body_size > 0) {
    std::lock_guard<std::mutex> lock(g_transfer_mutex);
    g_transfer.received = static_cast<uint32_t>(request.body_size);
  }
  if (ok) {
    ok = finish_normalized_upload(file, normalization, written);
  }
  if (fclose(file) != 0) {
    ok = false;
  }
  if (ok && written == 0) {
    ok = false;
  }
  return ok;
}
}
using namespace detail;
TransferStatus transfer_snapshot() {
  std::lock_guard<std::mutex> lock(g_transfer_mutex);
  // A closed/disconnected browser must not leave the screen trapped forever.
  if (!g_transfer.receiving && g_transfer.batch_open && now_ms() - g_transfer.updated_ms > 15000) {
    g_transfer.batch_open = false;
    g_transfer.updated_ms = now_ms();
  }
  return g_transfer;
}

bool transfer_dismissed(uint32_t generation) { return g_transfer_dismissed.load() == generation; }

bool dismiss_transfer() {
  const auto t = transfer_snapshot();
  if (!t.generation || transfer_dismissed(t.generation) ||
      (!t.receiving && !t.batch_open && now_ms() - t.updated_ms > 3000)) return false;
  g_transfer_dismissed.store(t.generation);
  return true;
}
}
