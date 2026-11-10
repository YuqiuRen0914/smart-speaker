#pragma once
#include "app/package/application_package.h"
#include "app/package/application_identity.h"
#include <vector>
namespace lunaredu::lunaros::apps {
std::string digest(const uint8_t *, size_t);
bool unpack(PackageBytes, Application &, size_t &, std::string &);
}
