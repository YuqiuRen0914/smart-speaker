#pragma once
#include <string>
namespace lunaredu::lunaros::apps {
bool aot_trust_available();
bool verify_aot_signature(const std::string &digest, const std::string &signer,
                          const std::string &signature, std::string &error);
}
