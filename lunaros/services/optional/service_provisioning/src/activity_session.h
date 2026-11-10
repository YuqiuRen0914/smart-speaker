#pragma once
#include <cstdint>
#include <string>
namespace lunaredu::lunaros::provisioning {
struct ActivityObservation;
ActivityObservation observe_session();
bool claim_session(uint64_t &, bool &, std::string &);
bool release_session(uint64_t, std::string &);
}
