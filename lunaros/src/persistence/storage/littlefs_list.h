#pragma once

#include <stdint.h>

#include "persistence/storage/littlefs_types.h"

bool littlefs_store_list_entries(const char *path,
                                 StorageEntryVisitor visitor,
                                 void *context);
bool littlefs_store_list_entries_recursive(const char *path, uint8_t max_depth,
                                           StorageEntryVisitor visitor,
                                           void *context);
