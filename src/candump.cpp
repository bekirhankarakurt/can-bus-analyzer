#include "canbus/candump.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <istream>
#include <stdexcept>

#include "detail.hpp"

namespace canbus {
namespace {

constexpr char kHexDigits[] = "0123456789ABCDEF";

void append_hex(std::string& out, std::uint32_t value, int digits) {
  for (int i = digits - 1; i >= 0; --i) {
    out.push_back(kHexDigits[(value >> (4 * i)) & 0xFU]);
  }
}

}  // namespace

std::string format_frame(const CanFrame& frame, std::string_view iface) {
  char stamp[40];
  std::snprintf(stamp, sizeof stamp, "(%.6f) ", frame.timestamp);
  std::string out = stamp;
  out.append(iface);
  out.push_back(' ');
  append_hex(out, frame.id, frame.extended ? 8 : 3);
  out.push_back('#');
  if (frame.rtr) {
    out.push_back('R');
    if (frame.len != 0) {
      out.push_back(static_cast<char>('0' + frame.len));
    }
    return out;
  }
  if (frame.fd) {
    out.push_back('#');
    append_hex(out, (frame.brs ? 1U : 0U) | (frame.esi ? 2U : 0U), 1);
  }
  for (std::size_t i = 0; i < frame.len; ++i) {
    append_hex(out, frame.data[i], 2);
  }
  return out;
}

std::optional<LogLine> parse_line(std::string_view line) {
  line = detail::trim(line);
  if (line.empty() || line.front() != '(') {
    return std::nullopt;
  }
  const auto close = line.find(')');
  if (close == std::string_view::npos) {
    return std::nullopt;
  }
  const std::string stamp(line.substr(1, close - 1));
  char* end = nullptr;
  const double timestamp = std::strtod(stamp.c_str(), &end);
  if (stamp.empty() || *end != '\0') {
    return std::nullopt;
  }

  std::string_view rest = detail::trim(line.substr(close + 1));
  const auto space = rest.find(' ');
  if (space == std::string_view::npos) {
    return std::nullopt;
  }
  LogLine result;
  result.iface = std::string(rest.substr(0, space));
  rest = detail::trim(rest.substr(space + 1));

  const auto hash = rest.find('#');
  if (hash == std::string_view::npos) {
    return std::nullopt;
  }
  const std::string_view id_text = rest.substr(0, hash);
  std::string_view body = rest.substr(hash + 1);

  CanFrame& frame = result.frame;
  frame.timestamp = timestamp;
  if (id_text.size() == 3) {
    frame.extended = false;
  } else if (id_text.size() == 8) {
    frame.extended = true;
  } else {
    return std::nullopt;
  }
  if (!detail::parse_hex_u32(id_text, frame.id) ||
      frame.id > (frame.extended ? kMaxExtendedId : kMaxStandardId)) {
    return std::nullopt;
  }

  if (!body.empty() && body.front() == '#') {
    frame.fd = true;
    body.remove_prefix(1);
    const int flags = body.empty() ? -1 : detail::hex_value(body.front());
    if (flags < 0 || flags > 3) {
      return std::nullopt;
    }
    frame.brs = (flags & 1) != 0;
    frame.esi = (flags & 2) != 0;
    body.remove_prefix(1);
  } else if (!body.empty() && body.front() == 'R') {
    frame.rtr = true;
    body.remove_prefix(1);
    if (!body.empty()) {
      if (body.size() != 1 || body.front() < '0' || body.front() > '8') {
        return std::nullopt;
      }
      frame.len = static_cast<std::uint8_t>(body.front() - '0');
    }
    return result;
  }

  std::size_t count = 0;
  int high = -1;
  for (const char c : body) {
    if (c == '.') {
      continue;
    }
    const int digit = detail::hex_value(c);
    if (digit < 0) {
      return std::nullopt;
    }
    if (high < 0) {
      high = digit;
      continue;
    }
    if (count >= kMaxDataLen) {
      return std::nullopt;
    }
    frame.data[count++] = static_cast<std::uint8_t>(high * 16 + digit);
    high = -1;
  }
  if (high >= 0 || (frame.fd ? !is_valid_fd_length(count) : count > 8)) {
    return std::nullopt;
  }
  frame.len = static_cast<std::uint8_t>(count);
  return result;
}

std::vector<LogLine> read_log(std::istream& in) {
  std::vector<LogLine> lines;
  std::string text;
  std::size_t number = 0;
  while (std::getline(in, text)) {
    ++number;
    const std::string_view trimmed = detail::trim(text);
    if (trimmed.empty() || trimmed.front() == '#') {
      continue;
    }
    auto parsed = parse_line(trimmed);
    if (!parsed) {
      throw std::runtime_error("log line " + std::to_string(number) +
                               ": malformed candump entry");
    }
    lines.push_back(std::move(*parsed));
  }
  return lines;
}

std::vector<LogLine> read_log_file(const std::string& path) {
  std::ifstream file(path);
  if (!file) {
    throw std::runtime_error("cannot open log file '" + path + "'");
  }
  return read_log(file);
}

}  // namespace canbus
