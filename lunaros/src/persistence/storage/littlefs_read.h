#pragma once

#include <cstddef>
#include <cstdint>
bool littlefs_store_read_range(const char *path, size_t offset, uint8_t *data, size_t size);

#include <stdint.h>
#include <string>
#include <vector>

#include "persistence/storage/littlefs_types.h"

bool littlefs_store_read_text_file(const char *path, std::string &content);
bool littlefs_store_read_file(const char *path, std::vector<uint8_t> &content);
bool littlefs_store_stream_file(const char *path,
                                StorageFileChunkWriter writer, void *context,
                                std::string *error = nullptr);
