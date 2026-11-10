#pragma once
#include "device_api/http/http_request_context.h"
class HttpContextLease {
public:
  HttpContextLease();
  ~HttpContextLease();
  HttpContextLease(const HttpContextLease &)=delete;
  HttpRequestContext &context(){return *context_;}
private:
  HttpRequestContext local_;
  HttpRequestContext *context_,*previous_;
  bool shared_=false;
};
void retire_http_workspace();
JsonDocument make_http_json_document();
