#include "delivery/update/ota_image_policy.h"

#include <array>
#include <cctype>
#include <string_view>

namespace lunaredu::lunaros {
namespace {

struct SemanticVersion {
  std::array<uint32_t, 3> parts = {};
  std::string_view prerelease;
};

bool parse_number(std::string_view value, size_t &cursor, uint32_t &output) {
  if (cursor >= value.size() ||
      !std::isdigit(static_cast<unsigned char>(value[cursor]))) {
    return false;
  }
  if (value[cursor] == '0' && cursor + 1 < value.size() &&
      std::isdigit(static_cast<unsigned char>(value[cursor + 1]))) {
    return false;
  }
  uint64_t number = 0;
  while (cursor < value.size() &&
         std::isdigit(static_cast<unsigned char>(value[cursor]))) {
    number = number * 10 + static_cast<uint32_t>(value[cursor] - '0');
    if (number > UINT32_MAX) return false;
    ++cursor;
  }
  output = static_cast<uint32_t>(number);
  return true;
}

bool valid_identifier_text(std::string_view value) {
  if (value.empty()) return false;
  for (const char character : value) {
    if (!(std::isalnum(static_cast<unsigned char>(character)) ||
          character == '-' || character == '.')) {
      return false;
    }
  }
  return value.front() != '.' && value.back() != '.' &&
         value.find("..") == std::string_view::npos;
}

bool parse_semantic_version(std::string_view value, SemanticVersion &output) {
  output = {};
  if (!value.empty() && (value.front() == 'v' || value.front() == 'V')) {
    value.remove_prefix(1);
  }
  size_t cursor = 0;
  for (size_t index = 0; index < output.parts.size(); ++index) {
    if (!parse_number(value, cursor, output.parts[index])) return false;
    if (index + 1 < output.parts.size()) {
      if (cursor >= value.size() || value[cursor] != '.') return false;
      ++cursor;
    }
  }
  if (cursor < value.size() && value[cursor] == '-') {
    const size_t begin = ++cursor;
    while (cursor < value.size() && value[cursor] != '+') ++cursor;
    output.prerelease = value.substr(begin, cursor - begin);
    if (!valid_identifier_text(output.prerelease)) return false;
  }
  if (cursor < value.size() && value[cursor] == '+') {
    ++cursor;
    if (!valid_identifier_text(value.substr(cursor))) return false;
    cursor = value.size();
  }
  return cursor == value.size();
}

int compare_prerelease(std::string_view left, std::string_view right) {
  if (left.empty() || right.empty()) {
    return left.empty() == right.empty() ? 0 : (left.empty() ? 1 : -1);
  }
  size_t left_cursor = 0;
  size_t right_cursor = 0;
  while (left_cursor < left.size() || right_cursor < right.size()) {
    if (left_cursor >= left.size()) return -1;
    if (right_cursor >= right.size()) return 1;
    const size_t left_end = left.find('.', left_cursor);
    const size_t right_end = right.find('.', right_cursor);
    const std::string_view left_part = left.substr(
        left_cursor, (left_end == std::string_view::npos ? left.size() : left_end) - left_cursor);
    const std::string_view right_part = right.substr(
        right_cursor, (right_end == std::string_view::npos ? right.size() : right_end) - right_cursor);
    const bool left_numeric = left_part.find_first_not_of("0123456789") == std::string_view::npos;
    const bool right_numeric = right_part.find_first_not_of("0123456789") == std::string_view::npos;
    if (left_numeric && right_numeric) {
      if (left_part.size() != right_part.size()) return left_part.size() < right_part.size() ? -1 : 1;
      if (left_part != right_part) return left_part < right_part ? -1 : 1;
    } else if (left_numeric != right_numeric) {
      return left_numeric ? -1 : 1;
    } else if (left_part != right_part) {
      return left_part < right_part ? -1 : 1;
    }
    left_cursor = left_end == std::string_view::npos ? left.size() : left_end + 1;
    right_cursor = right_end == std::string_view::npos ? right.size() : right_end + 1;
  }
  return 0;
}

int compare_versions(const SemanticVersion &left,
                     const SemanticVersion &right) {
  for (size_t index = 0; index < left.parts.size(); ++index) {
    if (left.parts[index] != right.parts[index]) {
      return left.parts[index] < right.parts[index] ? -1 : 1;
    }
  }
  return compare_prerelease(left.prerelease, right.prerelease);
}

} // namespace

bool ota_candidate_identity_allowed(const OtaCandidateIdentity &running,
                                    const OtaCandidateIdentity &candidate,
                                    std::string &error,
                                    const std::string &bootstrap_projects) {
  // Only the factory bootstrap image may install an explicitly listed product.
  // This opt-in never relaxes ordinary product upgrades, including downgrades.
  const bool cross_project = running.project_name != candidate.project_name;
  const bool bootstrap = running.project_name == "lunaros-bootstrap" &&
      !candidate.project_name.empty() &&
      candidate.project_name.find(',') == std::string::npos &&
      ("," + bootstrap_projects + ",").find("," + candidate.project_name + ",") !=
          std::string::npos;
  if (running.project_name.empty() || candidate.project_name.empty() ||
      (cross_project && !bootstrap)) {
    error = "ota_project_mismatch";
    return false;
  }
  SemanticVersion current_version;
  SemanticVersion next_version;
  if (!parse_semantic_version(running.version, current_version) ||
      !parse_semantic_version(candidate.version, next_version)) {
    error = "ota_version_invalid";
    return false;
  }
  if (!cross_project && compare_versions(next_version, current_version) <= 0) {
    error = "ota_version_not_newer";
    return false;
  }
  if (candidate.secure_version < running.secure_version) {
    error = "ota_secure_version_rollback";
    return false;
  }
  error.clear();
  return true;
}

} // namespace lunaredu::lunaros
