#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace canbus {

inline constexpr std::size_t kMaxDataLen = 64;
inline constexpr std::uint32_t kMaxStandardId = 0x7FF;
inline constexpr std::uint32_t kMaxExtendedId = 0x1FFFFFFF;

/// One classical CAN or CAN FD frame. `timestamp` is seconds since the Unix epoch.
struct CanFrame {
  std::uint32_t id = 0;
  bool extended = false;
  bool rtr = false;
  bool fd = false;
  bool brs = false;
  bool esi = false;
  std::uint8_t len = 0;
  std::array<std::uint8_t, kMaxDataLen> data{};
  double timestamp = 0.0;
};

/// True for every payload length a CAN FD frame can carry (0-8, 12, 16, 20, 24, 32, 48, 64).
[[nodiscard]] bool is_valid_fd_length(std::size_t len) noexcept;

/// Compares everything except the timestamp (and bytes beyond `len`).
[[nodiscard]] bool same_content(const CanFrame& a, const CanFrame& b) noexcept;

/// Current wall-clock time in seconds since the Unix epoch.
[[nodiscard]] double unix_time_now() noexcept;

}  // namespace canbus
