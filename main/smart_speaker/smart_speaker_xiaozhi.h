#pragma once

#include <string>

namespace smart_speaker {

bool register_xiaozhi_projection(std::string &error);
bool setup_xiaozhi_service();
void tick_xiaozhi_service();
bool xiaozhi_start_activation();
bool xiaozhi_clear_activation();
bool xiaozhi_start_listening();
bool xiaozhi_stop_listening();
bool xiaozhi_finish_listening();
std::string xiaozhi_status_label();

} // namespace smart_speaker
