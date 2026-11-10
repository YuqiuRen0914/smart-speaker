#include "lunaredu_lunaros/desktop/core/utf8.h"

#include <cstring>

namespace lunaredu::desktop {

uint32_t next_utf8(const char *&cursor) {
  if (cursor == nullptr || *cursor == '\0') {
    return 0;
  }

  const auto *bytes = reinterpret_cast<const unsigned char *>(cursor);
  const unsigned char first = bytes[0];
  if (first < 0x80) {
    ++cursor;
    return first;
  }

  uint32_t codepoint = 0;
  int continuation_count = 0;
  uint32_t minimum = 0;
  if ((first & 0xE0) == 0xC0) {
    codepoint = first & 0x1F;
    continuation_count = 1;
    minimum = 0x80;
  } else if ((first & 0xF0) == 0xE0) {
    codepoint = first & 0x0F;
    continuation_count = 2;
    minimum = 0x800;
  } else if ((first & 0xF8) == 0xF0) {
    codepoint = first & 0x07;
    continuation_count = 3;
    minimum = 0x10000;
  } else {
    ++cursor;
    return 0xFFFD;
  }

  for (int index = 1; index <= continuation_count; ++index) {
    const unsigned char next = bytes[index];
    if (next == 0 || (next & 0xC0) != 0x80) {
      ++cursor;
      return 0xFFFD;
    }
    codepoint = (codepoint << 6) | (next & 0x3F);
  }
  cursor += continuation_count + 1;
  if (codepoint < minimum || codepoint > 0x10FFFF ||
      (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
    return 0xFFFD;
  }
  return codepoint;
}

size_t sanitize_utf8(const char *input, char *output, size_t output_capacity,
                     Utf8CodepointSupport supported, void *context,
                     size_t *replacement_count) {
  size_t replacements = 0;
  size_t written = 0;
  if (output_capacity == 0) {
    if (replacement_count != nullptr) {
      *replacement_count = 0;
    }
    return 0;
  }
  output[0] = '\0';
  if (input == nullptr) {
    if (replacement_count != nullptr) {
      *replacement_count = 0;
    }
    return 0;
  }

  const char *cursor = input;
  while (*cursor != '\0') {
    const char *sequence = cursor;
    const uint32_t codepoint = next_utf8(cursor);
    const size_t sequence_bytes = static_cast<size_t>(cursor - sequence);
    const bool layout_control =
        codepoint == '\n' || codepoint == '\r' || codepoint == '\t';
    const bool keep = codepoint != 0xFFFD &&
                      (layout_control || supported == nullptr ||
                       supported(context, codepoint));
    const size_t bytes_to_write = keep ? sequence_bytes : 1;
    if (written + bytes_to_write >= output_capacity) {
      break;
    }
    if (keep) {
      std::memcpy(output + written, sequence, sequence_bytes);
      written += sequence_bytes;
    } else {
      output[written++] = '?';
      ++replacements;
    }
  }
  output[written] = '\0';
  if (replacement_count != nullptr) {
    *replacement_count = replacements;
  }
  return written;
}

} // namespace lunaredu::desktop
