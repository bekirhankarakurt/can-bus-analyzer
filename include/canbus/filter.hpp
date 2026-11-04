#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "canbus/frame.hpp"

namespace canbus {

// One identifier filter in candump syntax:
//   "123"        exact match on 0x123
//   "123:7F0"    (id & mask) == (0x123 & 0x7F0)
//   "123~7FF"    inverted: rejects matching frames
// An identifier with 8 hex digits, or above 0x7FF, selects extended frames.
class Filter {
 public:
  // Throws std::invalid_argument on malformed specs.
  [[nodiscard]] static Filter parse(std::string_view spec);

  [[nodiscard]] bool matches(const CanFrame& frame) const noexcept;
  [[nodiscard]] bool inverted() const noexcept { return inverted_; }

 private:
  std::uint32_t id_ = 0;
  std::uint32_t mask_ = 0;
  bool extended_ = false;
  bool inverted_ = false;
};

// A frame is accepted when it matches at least one positive filter (or none
// are configured) and matches no inverted filter.
class FilterSet {
 public:
  void add(const Filter& filter);
  [[nodiscard]] bool accepts(const CanFrame& frame) const noexcept;
  [[nodiscard]] bool empty() const noexcept { return accept_.empty() && reject_.empty(); }

 private:
  std::vector<Filter> accept_;
  std::vector<Filter> reject_;
};

}  // namespace canbus
