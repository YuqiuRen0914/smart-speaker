#pragma once
#include <string>
namespace lunaredu::pet::actions {
bool list(bool allow_changes, const char *arguments, std::string &response, std::string &error);
bool describe(bool allow_changes, const char *arguments, std::string &response, std::string &error);
bool call(bool allow_changes, const char *arguments, std::string &response, std::string &error);
} // namespace lunaredu::pet::actions
