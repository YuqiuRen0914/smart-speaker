#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace lunaredu::lunaros {
bool ota_deployment_validate_header(const uint8_t *data, size_t size, std::string &error);
bool ota_boot_validate_header(const uint8_t *data, size_t size, std::string &error);
}
