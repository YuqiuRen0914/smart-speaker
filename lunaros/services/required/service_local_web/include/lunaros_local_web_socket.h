#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace lunaredu::lunaros {
struct LocalWebSocketFrame {
  int connection = -1;
  bool binary = false;
  const uint8_t *data = nullptr;
  size_t size = 0;
  void *transport = nullptr;
  bool (*send)(void *, bool binary, const uint8_t *, size_t) = nullptr;
};
struct LocalWebSocketEndpoint {
  const char *path = nullptr;
  const char *protocol = nullptr;
  void *context = nullptr;
  // First call receives offered subprotocols; subsequent calls receive null.
  // The owner must recheck its expiring REST-issued authority for every frame.
  bool (*authorize)(void *, int connection, const char *protocols) = nullptr;
  bool (*receive)(void *, const LocalWebSocketFrame &) = nullptr;
};
// Immutable boot-time registration. Same origin policy as Local Web; sockets
// are unavailable on the provisioning surface. Owners issue authority using
// ordinary protected Device API operations, never from URL credentials.
bool configure_local_web_sockets(const LocalWebSocketEndpoint *, size_t, std::string &error);
} // namespace lunaredu::lunaros
