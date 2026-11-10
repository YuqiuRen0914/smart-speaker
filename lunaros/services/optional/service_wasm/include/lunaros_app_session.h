#pragma once
#include "app/execution/application_session.h"
#include "app/execution/draw_list.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lunaredu::lunaros::apps {
// Package/protocol validation is available on every target. Execution currently
// requires the ESP32-S3 WAMR port; this probe allocates no VM or owner task.
bool session_backend_available();
bool session_aot_available();
bool validate_aot_header(const uint8_t *, size_t);
// Reject start functions, imported memory/tables and unbounded memory before
// WAMR instantiation. Export calls are independently metered.
bool validate_module(const uint8_t *bytes, size_t size,
                     uint32_t maximum_pages = 4,bool *uses_device_basic=nullptr);
class Session {
public:
  Session();
  ~Session();
  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;
  bool open(const uint8_t *, size_t, SessionStorage, uint32_t seed,
            std::string &error, bool aot = false);
  bool event(int kind, int a, int b, int c, std::string &error);
  bool checkpoint(std::string &error);
  bool validate_save(SessionStorage, std::string &error);
  void close();
  bool close(std::string &error); // Retains VM and callbacks while devices retire.
  bool active() const;
  std::shared_ptr<const DrawList> frame() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace lunaredu::lunaros::apps
