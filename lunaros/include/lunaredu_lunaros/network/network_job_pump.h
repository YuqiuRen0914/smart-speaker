#pragma once
#include <cstdint>
namespace lunaredu::lunaros {
// Call only on the network I/O executor. One job per iteration keeps queued
// service operations responsive even when producers continuously submit work.
void enter_network_job_pump(void *);
void leave_network_job_pump(void *);
uint32_t poll_network_job_pump(void *);
} // namespace lunaredu::lunaros
