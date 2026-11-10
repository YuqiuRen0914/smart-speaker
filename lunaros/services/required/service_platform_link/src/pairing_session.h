#pragma once
#include <cstdint>
#include <string>
namespace lunaredu::lunaros::platform_link {
struct PairingActivityObservation;
PairingActivityObservation observe_pairing_session();
uint64_t pairing_session();
bool claim_pairing_session(bool, uint64_t &, bool &, std::string &);
bool release_pairing_session(uint64_t, std::string &);
}
