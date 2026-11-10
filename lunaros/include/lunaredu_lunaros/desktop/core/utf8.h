#pragma once

#include <cstddef>
#include <cstdint>

namespace lunaredu::desktop {

// Decode one UTF-8 code point and advance cursor. An invalid sequence consumes
// one byte and returns U+FFFD so external text can never stall the renderer.
uint32_t next_utf8(const char *&cursor);

using Utf8CodepointSupport = bool (*)(void *context, uint32_t codepoint);

// Copies complete UTF-8 sequences into output and replaces invalid or
// unsupported code points with an ASCII question mark. The output is always
// NUL-terminated when output_capacity is non-zero.
size_t sanitize_utf8(const char *input, char *output, size_t output_capacity,
                     Utf8CodepointSupport supported, void *context,
                     size_t *replacement_count = nullptr);

} // namespace lunaredu::desktop
