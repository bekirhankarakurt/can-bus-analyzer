#pragma once

#include <cstdint>
#include <string_view>

namespace canbus::detail {

[[nodiscard]] inline std::string_view trim(std::string_view s) noexcept {
  constexpr const char* kWhitespace = " \t\r\n";
  const auto begin = s.find_first_not_of(kWhitespace);
  if (begin == std::string_view::npos) {
    return {};
  }
  const auto end = s.find_last_not_of(kWhitespace);
  return s.substr(begin, end - begin + 1);
}

[[nodiscard]] inline int hex_value(char c) noexcept {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

// Parses 1-8 hex digits. Rejects signs, whitespace and prefixes.
[[nodiscard]] inline bool parse_hex_u32(std::string_view text, std::uint32_t& out) noexcept {
  if (text.empty() || text.size() > 8) {
    return false;
  }
  std::uint32_t value = 0;
  for (const char c : text) {
    const int digit = hex_value(c);
    if (digit < 0) {
      return false;
    }
    value = (value << 4) | static_cast<std::uint32_t>(digit);
  }
  out = value;
  return true;
}

}  // namespace canbus::detail
