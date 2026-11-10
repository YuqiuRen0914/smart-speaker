#include <type_traits>

#include "lunaredu_lunaros/model/device_model.h"

using lunaredu::lunaros::DeviceModuleSpec;
using lunaredu::lunaros::DeviceOperationKind;
using lunaredu::lunaros::DeviceOperationSpec;

static_assert(std::is_trivially_copyable_v<DeviceOperationSpec>);
static_assert(std::is_trivially_copyable_v<DeviceModuleSpec>);

constexpr DeviceOperationSpec kQuery = {
    "status.get",
    DeviceOperationKind::Query,
    false,
    "lunar.main",
    nullptr,
    "device.status.v1",
    lunaredu::lunaros::DeviceOperationExecution::OwnerExecutor,
};

static_assert(kQuery.kind == DeviceOperationKind::Query);
static_assert(!kQuery.mutating);

int main() { return 0; }
