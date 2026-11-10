#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
namespace lunaredu::lunaros {
struct LocalWebApplicationRequest {
  const uint8_t *body = nullptr;
  size_t bytes = 0;
  void *transport = nullptr;
  std::string (*argument)(void *, const char *) = nullptr;
};
struct LocalWebApplicationResponse {
  int status = 200;
  const char *content_type = "application/json; charset=utf-8";
  std::string body;
  // Optional borrowed immutable body, valid until complete (also on send error).
  const uint8_t *data = nullptr;
  size_t bytes = 0;
  void *loan = nullptr;
  void (*complete)(void *) = nullptr;
};
struct LocalWebApplicationRoute {
  const char *path = nullptr; // Product namespace /app-api/, never system routes.
  bool post = false;
  const char *operation = nullptr; // Same local-access policy as Device API.
  void *context = nullptr;
  void (*handle)(void *, const LocalWebApplicationRequest &, LocalWebApplicationResponse &) = nullptr;
};
// Configure once before HTTP starts. Route array/strings/contexts live through
// all HTTP restarts. Callbacks enqueue work or read snapshots, never do device I/O.
bool configure_local_web_application(std::span<const LocalWebApplicationRoute>, std::string &error);
}
