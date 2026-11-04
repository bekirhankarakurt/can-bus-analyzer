#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "canbus/frame.hpp"

namespace canbus {

// Estimated bits a frame occupies on the wire: header, payload, CRC, ACK, EOF and
// interframe space. Stuff bits are not counted and CAN FD is costed at the nominal
// bit rate, so the result is a lower-bound estimate.
[[nodiscard]] std::size_t wire_bits(const CanFrame& frame) noexcept;

struct IdStats {
  std::uint32_t id = 0;
  bool extended = false;
  std::uint64_t count = 0;
  double period_ms = 0.0;  // mean inter-arrival time; 0 with fewer than two frames
};

struct StatsSnapshot {
  std::uint64_t frames = 0;
  std::uint64_t bytes = 0;
  double duration_s = 0.0;  // last minus first frame timestamp
  double frames_per_s = 0.0;
  double load_percent = 0.0;
  std::uint32_t bitrate = 0;
  std::vector<IdStats> ids;  // ascending by (extended, id)
};

class StatsCollector {
 public:
  // Throws std::invalid_argument if bitrate is zero.
  explicit StatsCollector(std::uint32_t bitrate);

  void add(const CanFrame& frame);
  [[nodiscard]] StatsSnapshot snapshot() const;

 private:
  struct Entry {
    std::uint64_t count = 0;
    double first = 0.0;
    double last = 0.0;
  };

  std::uint32_t bitrate_;
  std::uint64_t frames_ = 0;
  std::uint64_t bytes_ = 0;
  std::uint64_t bits_ = 0;
  double first_ = 0.0;
  double last_ = 0.0;
  std::map<std::uint64_t, Entry> entries_;
};

[[nodiscard]] std::string format_report(const StatsSnapshot& stats);

}  // namespace canbus
