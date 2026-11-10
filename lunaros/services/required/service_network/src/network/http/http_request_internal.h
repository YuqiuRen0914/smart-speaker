#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "lunaros_network_transport_policy.h"
#include "lunaros_network_request_stream.h"


namespace lunaredu::lunaros {

std::unique_ptr<NetworkRequestStream>
create_esp_http_request_stream(NetworkRequest request, int *failure_error,
                               std::string *failure_error_detail,
                               bool *failure_deferred = nullptr);

} // namespace lunaredu::lunaros
