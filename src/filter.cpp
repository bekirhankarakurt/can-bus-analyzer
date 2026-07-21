#include "canbus/filter.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

#include "detail.hpp"

namespace canbus {

Filter Filter::parse(std::string_view spec) {
  const auto separator = spec.find_first_of(":~");
  const std::string_view id_text = spec.substr(0, separator);
  Filter filter;
  if (!detail::parse_hex_u32(id_text, filter.id_) || filter.id_ > kMaxExtendedId) {
    throw std::invalid_argument("invalid filter '" + std::string(spec) + "': bad identifier");
  }
  filter.extended_ = id_text.size() == 8 || filter.id_ > kMaxStandardId;
  filter.mask_ = filter.extended_ ? kMaxExtendedId : kMaxStandardId;
  if (separator != std::string_view::npos) {
    filter.inverted_ = spec[separator] == '~';
    if (!detail::parse_hex_u32(spec.substr(separator + 1), filter.mask_)) {
      throw std::invalid_argument("invalid filter '" + std::string(spec) + "': bad mask");
    }
  }
  return filter;
}

bool Filter::matches(const CanFrame& frame) const noexcept {
  return frame.extended == extended_ && (frame.id & mask_) == (id_ & mask_);
}

void FilterSet::add(const Filter& filter) {
  (filter.inverted() ? reject_ : accept_).push_back(filter);
}

bool FilterSet::accepts(const CanFrame& frame) const noexcept {
  const auto matches = [&frame](const Filter& f) { return f.matches(frame); };
  if (std::any_of(reject_.begin(), reject_.end(), matches)) {
    return false;
  }
  return accept_.empty() || std::any_of(accept_.begin(), accept_.end(), matches);
}

}  // namespace canbus
