#pragma once

#include <stddef.h>

bool setup_littlefs_store();
bool littlefs_store_ready();
const char *littlefs_store_mount_path();
const char *littlefs_store_partition_label();
size_t littlefs_store_total_bytes();
size_t littlefs_store_used_bytes();
bool littlefs_store_refresh_usage();
bool littlefs_store_format();
const char *littlefs_store_last_error();
