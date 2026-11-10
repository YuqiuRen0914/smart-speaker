#pragma once
#include <cstdint>
#include <string>
namespace lunaredu::lunaros::log_stream {
struct Config {
  uint16_t port = 2323;
  std::string greeting = "LunarOS log stream\r\n";
};
// One ESP-IDF vprintf hook per process. Install before startup to buffer boot logs.
// tick/shutdown run on the caller's executor; tick sends at most 2 KiB.
void initialize(const Config &config = {});
void tick();
void shutdown();
bool client_connected();
}
