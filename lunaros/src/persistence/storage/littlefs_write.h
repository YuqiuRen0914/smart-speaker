#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>

#include "persistence/storage/littlefs_types.h"

bool littlefs_store_write_text_file(const char *path, const char *content);
bool littlefs_store_write_text_file_ex(const char *path, const char *content,
                                       size_t *written_bytes);
bool littlefs_store_write_file(const char *path, const uint8_t *data,
                               size_t size, size_t *written_bytes = nullptr);
bool littlefs_store_write_streamed_file(
    const char *path, StorageWriteStreamProducer producer,
    void *producer_context, size_t *written_bytes = nullptr,
    std::string *error = nullptr);
bool littlefs_store_delete_file(const char *path);
bool littlefs_store_move_file(const char *source_path,
                              const char *destination_path,
                              std::string *error = nullptr);
