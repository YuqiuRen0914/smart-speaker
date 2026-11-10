#include "device_api/routes/device_api_routes.h"

#include "device_api/http/embedded_http_server.h"
#include "device_api/http/http_request_api.h"
#include "device_api/routes/device_model_routes.h"
#include "device_api/routes/device_operation_routes.h"

void register_device_api_routes() {
  register_device_model_routes();
  register_device_operation_routes();
  register_http_route("/api/v1/*", HTTP_OPTIONS, handle_options);
}
