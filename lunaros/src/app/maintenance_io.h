#pragma once

namespace lunaredu::lunaros {
// Health is owned by the blocking-I/O worker, separate from the product loop.
bool maintenance_io_healthy();
}
