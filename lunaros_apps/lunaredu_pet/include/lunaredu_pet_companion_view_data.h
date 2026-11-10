#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace lunaredu::pet {
inline constexpr size_t kCompanionAnimationCapacity = 5;
struct CompanionAnimation {
    std::string key;
    uint8_t frame_count = 0;
    uint16_t frame_duration_ms = 0;
};
}
