#include "canbus/stats.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace canbus {
namespace {

std::uint64_t key(const CanFrame& f) { return f.id | (f.extended ? (1ULL << 32) : 0ULL); }

std::string id_text(std::uint32_t id, bool extended) {
  char buf[16];
  std::snprintf(buf, sizeof buf, extended ? "%08X" : "%03X", id);
  return buf;
}

}  // namespace

std::size_t wire_bits(const CanFrame& frame) noexcept {
  const std::size_t payload = frame.rtr ? 0 : std::size_t{frame.len} * 8;
  if (frame.fd) {
    const std::size_t header = frame.extended ? 48 : 28;
    const std::size_t crc = frame.len <= 16 ? 17 : 21;
    return header + payload + crc + 13;
  }
  return (frame.extended ? 67 : 47) + payload;
}

StatsCollector::StatsCollector(std::uint32_t bitrate) : bitrate_(bitrate) {
  if (bitrate == 0) {
    throw std::invalid_argument("bitrate must be greater than zero");
  }
}

void StatsCollector::add(const CanFrame& frame) {
  if (frames_ == 0) {
    first_ = frame.timestamp;
  }
  last_ = std::max(last_, frame.timestamp);
  ++frames_;
  bytes_ += frame.rtr ? 0 : frame.len;
  bits_ += wire_bits(frame);
  Entry& entry = entries_[key(frame)];
  if (entry.count++ == 0) {
    entry.first = frame.timestamp;
  }
  entry.last = std::max(entry.last, frame.timestamp);
}

StatsSnapshot StatsCollector::snapshot() const {
  StatsSnapshot s;
  s.frames = frames_;
  s.bytes = bytes_;
  s.bitrate = bitrate_;
  s.duration_s = frames_ == 0 ? 0.0 : last_ - first_;
  if (s.duration_s > 0.0) {
    s.frames_per_s = static_cast<double>(frames_) / s.duration_s;
    s.load_percent = 100.0 * static_cast<double>(bits_) / (static_cast<double>(bitrate_) * s.duration_s);
  }
  for (const auto& [k, entry] : entries_) {
    IdStats id;
    id.id = static_cast<std::uint32_t>(k & 0xFFFFFFFFULL);
    id.extended = (k >> 32) != 0;
    id.count = entry.count;
    if (entry.count > 1) {
      id.period_ms = 1000.0 * (entry.last - entry.first) / static_cast<double>(entry.count - 1);
    }
    s.ids.push_back(id);
  }
  return s;
}

std::string format_report(const StatsSnapshot& s) {
  char line[128];
  std::string out;
  std::snprintf(line, sizeof line, "Frames    %llu (%llu data bytes) in %.3f s, %.1f frames/s\n",
                static_cast<unsigned long long>(s.frames), static_cast<unsigned long long>(s.bytes),
                s.duration_s, s.frames_per_s);
  out += line;
  std::snprintf(line, sizeof line, "Bus load  %.2f %% of %u bit/s (estimate, no stuff bits)\n",
                s.load_percent, s.bitrate);
  out += line;
  out += "\nID        Count  Period [ms]\n";
  for (const IdStats& id : s.ids) {
    std::snprintf(line, sizeof line, "%-8s %6llu  %11.3f\n", id_text(id.id, id.extended).c_str(),
                  static_cast<unsigned long long>(id.count), id.period_ms);
    out += line;
  }
  return out;
}

}  // namespace canbus
