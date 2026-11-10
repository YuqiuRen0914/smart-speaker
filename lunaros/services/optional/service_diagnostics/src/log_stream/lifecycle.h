#pragma once
#include <string>
namespace lunaredu::lunaros {
bool diagnostics_log_stream_selected();
bool start_diagnostics_log_stream(std::string &);
void stop_diagnostics_log_stream();
}
