#pragma once

#include <cstddef>
#include <cstdint>

#include "lunaredu_local_web_portal.h"

namespace lunaredu::lunaros::device_web
{

  enum class Page : uint8_t
  {
    Application,
    Network,
    Binding,
    Maintenance
  };
  struct PageView
  {
    const uint8_t *data;
    size_t size;
    bool gzip = false;
  };
  enum class DeviceWebAsset : uint8_t
  {
    Style,
    Script,
    FactoryPage,
    ApplicationStyle,
    Client
  };
  struct DeviceWebAssetView
  {
    const uint8_t *data;
    size_t size;
    bool gzip = false;
  };

  // Only the existing HTTP owner reads pages. Freeze configuration before routes
  // are exposed; returned bytes remain valid until that owner's next page read.
  void freeze();
  const char *product_name();
  const char *application_name();
  bool has_page_override(Page page);
  PageView page(Page page, bool recovery = false);
  DeviceWebAssetView asset(DeviceWebAsset asset);

} // namespace lunaredu::lunaros::device_web
