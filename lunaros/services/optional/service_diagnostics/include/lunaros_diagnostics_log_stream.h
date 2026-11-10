#pragma once
#include <cstdint>
#include <string>
#include <utility>
namespace lunaredu::lunaros {
struct DiagnosticsLogStreamConfig {
  uint16_t port=2323;
  std::string greeting="LunarOS log stream\r\n";
};
// Explicit optional output owned by Diagnostics. Keep this instance alive
// throughout the Device lifetime; boot capture never starts a TCP listener.
class DiagnosticsLogStream {
public:
  explicit DiagnosticsLogStream(DiagnosticsLogStreamConfig config={}) : config_(std::move(config)) {}
  DiagnosticsLogStream(const DiagnosticsLogStream &)=delete;
  DiagnosticsLogStream &operator=(const DiagnosticsLogStream &)=delete;
  ~DiagnosticsLogStream();
  bool capture_boot(std::string &);
  bool shutdown(std::string &);
  void tick();
  bool client_connected() const;
private:
  friend bool start_diagnostics_log_stream(std::string &);
  friend void stop_diagnostics_log_stream();
  DiagnosticsLogStreamConfig config_;
  bool captured_=false,started_=false;
};
}
