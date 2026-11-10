#pragma once

#include <string>

namespace lunaredu::pet::conversation {

// Stable client ID used only by the XiaoZhi protocol. It is intentionally
// separate from the hardware serial and Local Device API UUID.
const std::string &assistant_client_id();

} // namespace lunaredu::pet::conversation
