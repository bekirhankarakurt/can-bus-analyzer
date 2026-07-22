#pragma once

#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "canbus/frame.hpp"

namespace canbus {

struct LogLine {
  CanFrame frame;
  std::string iface;
};

/// Formats a frame as a candump -L line, e.g. "(1700000000.000000) vcan0 123#DEADBEEF".
/// CAN FD: "123##1DEADBEEF" (flags nibble: 1 = BRS, 2 = ESI). RTR: "123#R".
[[nodiscard]] std::string format_frame(const CanFrame& frame, std::string_view iface);

/// Parses one candump -L line. Returns nullopt if the line is malformed.
[[nodiscard]] std::optional<LogLine> parse_line(std::string_view line);

/// Reads a whole log. Blank lines and '#' comments are skipped; any other
/// malformed line throws std::runtime_error naming the line number.
[[nodiscard]] std::vector<LogLine> read_log(std::istream& in);
[[nodiscard]] std::vector<LogLine> read_log_file(const std::string& path);

}  // namespace canbus
