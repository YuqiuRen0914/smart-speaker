#pragma once

#include "device_api/http/http_routes.h"
#include "device_api/http/segmented_json.h"
using HttpJsonStreamProducer = bool (*)(lunaredu::lunaros::internal::JsonStreamSink &,void *);
void send_segmented_json(int status_code,HttpJsonStreamProducer producer,void *context);
JsonDocument make_http_json_document();
ArduinoJson::Allocator *current_http_json_allocator();
std::string *current_http_response_workspace();


const uint8_t *request_body_data();
size_t request_body_size();
size_t request_content_length();
std::string request_header(const char *name);

// Opaque bridge used while a request is synchronously dispatched to its
// declared LunarOS owner executor. The HTTP worker remains blocked until the
// owner completes, so the request lifetime stays valid without exposing
// HttpRequestContext outside the transport.
void *current_request_context_handle();
bool request_context_has_arg(const char *name, void *context);
std::string request_context_arg(const char *name, void *context);
std::string request_context_header(const char *name, void *context);
int read_request_body_chunk_from_context(uint8_t *buffer, size_t max_size,
                                         void *context);
bool send_http_binary_response_from_context(
    int status_code, const char *content_type, const uint8_t *data,
    size_t size, void *context);
bool begin_http_stream_from_context(int status_code, const char *content_type,
                                    void *context);
bool send_http_chunk_from_context(const uint8_t *data, size_t size,
                                  void *context);
void end_http_stream_from_context(void *context);

namespace lunaredu::lunaros {class JsonSchemaWorkspace;}
lunaredu::lunaros::JsonSchemaWorkspace *current_http_schema_workspace();
