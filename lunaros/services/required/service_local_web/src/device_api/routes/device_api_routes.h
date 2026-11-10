#pragma once
namespace lunaredu::lunaros { class DeviceProjectionRegistry; }

// Registration only: lifecycle consumers do not depend on the JSON transport.
void register_device_model_routes(lunaredu::lunaros::DeviceProjectionRegistry &registry);
void register_device_operation_routes(lunaredu::lunaros::DeviceProjectionRegistry &registry);
void setup_device_web_routes(lunaredu::lunaros::DeviceProjectionRegistry &registry);
