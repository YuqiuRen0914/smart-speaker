#pragma once
#include <cstddef>
#include <cstdint>
namespace lunaredu::lunaros::device_web {
enum class Page : uint8_t { Home, Network, Binding, Maintenance };
struct PageView { const uint8_t *data; size_t size; };
// Only the existing HTTP owner reads pages. Freeze configuration before routes
// are exposed; returned bytes remain valid until that owner's next page read.
void freeze();
const char *product_name();
PageView page(Page page, bool recovery = false);
} // namespace lunaredu::lunaros::device_web
