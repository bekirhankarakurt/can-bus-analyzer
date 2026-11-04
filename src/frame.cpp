#include "canbus/frame.hpp"

#include <algorithm>
#include <chrono>

namespace canbus {

bool is_valid_fd_length(std::size_t len) noexcept {
  if (len <= 8) {
    return true;
  }
  switch (len) {
    case 12:
    case 16:
    case 20:
    case 24:
    case 32:
    case 48:
    case 64:
      return true;
    default:
      return false;
  }
}

bool same_content(const CanFrame& a, const CanFrame& b) noexcept {
  return a.id == b.id && a.extended == b.extended && a.rtr == b.rtr && a.fd == b.fd &&
         a.brs == b.brs && a.esi == b.esi && a.len == b.len &&
         std::equal(a.data.begin(), a.data.begin() + a.len, b.data.begin());
}

double unix_time_now() noexcept {
  const auto since_epoch = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration<double>(since_epoch).count();
}

}  // namespace canbus
