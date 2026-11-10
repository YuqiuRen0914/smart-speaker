#pragma once

#include <string>

std::string littlefs_store_normalize_path(const char *path);
bool littlefs_store_is_safe_path(const char *path);
bool littlefs_store_path_exists(const char *path);
bool littlefs_store_ensure_directory(const char *path);
