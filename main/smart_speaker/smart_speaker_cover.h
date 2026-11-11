#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <memory>
#include <cstdlib>

namespace smart_speaker::cover {
constexpr size_t kSide = 240;
constexpr size_t kBmpBytes = 54 + kSide * kSide * 3;
struct FreeBuffer { void operator()(void *p) const { std::free(p); } };
using Bmp = std::unique_ptr<uint8_t, FreeBuffer>;
struct Image { std::unique_ptr<uint16_t, FreeBuffer> pixels; };
Bmp allocate_bmp();
std::shared_ptr<const Image> decode(const uint8_t *data, size_t size);
std::shared_ptr<const Image> load(const std::string &track_path, std::string &error);

bool valid_bmp(const uint8_t *data, size_t size);
std::string path_for_track(const std::string &track_path);
bool exists(const std::string &track_path);
bool save(const std::string &track_path, const uint8_t *data, size_t size,
          std::string &error);
bool read(const std::string &track_path, uint8_t *data, size_t size,
          std::string &error);
bool remove(const std::string &track_path, std::string &error);
} // namespace smart_speaker::cover
