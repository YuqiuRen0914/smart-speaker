#pragma once
#include <string>
namespace lunaredu::lunaros::apps {
bool id_valid(const std::string &v);
bool version_valid(const std::string &v);
bool digest_valid(const std::string &v);
bool version_parts(const std::string &v, unsigned (&parts)[3]);
}
