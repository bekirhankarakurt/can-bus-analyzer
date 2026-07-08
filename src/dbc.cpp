#include "canbus/dbc.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <istream>
#include <regex>
#include <stdexcept>

#include "detail.hpp"

namespace canbus {
namespace {

constexpr std::uint64_t kExtendedFlag = 0x80000000ULL;

std::uint64_t key(std::uint32_t id, bool extended) {
  return id | (extended ? (1ULL << 32) : 0ULL);
}

// positions[i] = frame bit index holding bit i of the signal (0 = LSB).
// Returns false if any bit falls outside `total_bits`.
bool bit_positions(const Signal& s, std::size_t total_bits,
                   std::array<std::size_t, 64>& positions) {
  if (s.length == 0 || s.length > 64) {
    return false;
  }
  if (s.byte_order == ByteOrder::LittleEndian) {
    for (unsigned i = 0; i < s.length; ++i) {
      positions[i] = s.start_bit + i;
    }
  } else {
    // Motorola "sawtooth" numbering: walk from the MSB downwards, jumping to
    // bit 7 of the next byte after bit 0 of the current one.
    std::size_t pos = s.start_bit;
    for (unsigned i = s.length; i-- > 0;) {
      positions[i] = pos;
      pos = (pos % 8 == 0) ? pos + 15 : pos - 1;
    }
  }
  return std::all_of(positions.begin(), positions.begin() + s.length,
                     [total_bits](std::size_t p) { return p < total_bits; });
}

[[noreturn]] void fail(std::size_t line, const std::string& what) {
  throw std::runtime_error("DBC line " + std::to_string(line) + ": " + what);
}

double to_double(const std::string& text, std::size_t line) {
  char* end = nullptr;
  const double value = std::strtod(text.c_str(), &end);
  if (end == text.c_str() || *end != '\0' || !std::isfinite(value)) {
    fail(line, "invalid number '" + text + "'");
  }
  return value;
}

std::uint64_t to_unsigned(const std::string& text, std::uint64_t max, std::size_t line) {
  errno = 0;
  const unsigned long long value = std::strtoull(text.c_str(), nullptr, 10);
  if (errno == ERANGE || value > max) {
    fail(line, "value '" + text + "' out of range");
  }
  return value;
}

bool starts_with(std::string_view text, std::string_view prefix) {
  return text.substr(0, prefix.size()) == prefix;
}

}  // namespace

std::optional<double> decode_signal(const Signal& signal, const std::uint8_t* data,
                                    std::size_t len) {
  std::array<std::size_t, 64> positions{};
  if (!bit_positions(signal, len * 8, positions)) {
    return std::nullopt;
  }
  std::uint64_t raw = 0;
  for (unsigned i = 0; i < signal.length; ++i) {
    const std::size_t p = positions[i];
    if (((data[p / 8] >> (p % 8)) & 1) != 0) {
      raw |= std::uint64_t{1} << i;
    }
  }
  double value = 0.0;
  if (signal.sign == Sign::Signed) {
    if (signal.length < 64 && ((raw >> (signal.length - 1)) & 1) != 0) {
      raw |= ~std::uint64_t{0} << signal.length;
    }
    value = static_cast<double>(static_cast<std::int64_t>(raw));
  } else {
    value = static_cast<double>(raw);
  }
  return value * signal.factor + signal.offset;
}

bool encode_signal(const Signal& signal, double physical, std::uint8_t* data, std::size_t len) {
  std::array<std::size_t, 64> positions{};
  if (!bit_positions(signal, len * 8, positions)) {
    return false;
  }
  const double scaled = std::round((physical - signal.offset) / signal.factor);
  std::uint64_t raw = 0;
  if (!std::isnan(scaled)) {
    if (signal.sign == Sign::Signed) {
      const std::int64_t max = signal.length == 64
                                   ? INT64_MAX
                                   : (std::int64_t{1} << (signal.length - 1)) - 1;
      const std::int64_t min = -max - 1;
      std::int64_t clamped = 0;
      if (scaled >= static_cast<double>(max)) {
        clamped = max;
      } else if (scaled <= static_cast<double>(min)) {
        clamped = min;
      } else {
        clamped = static_cast<std::int64_t>(scaled);
      }
      raw = static_cast<std::uint64_t>(clamped);
    } else {
      const std::uint64_t max =
          signal.length == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << signal.length) - 1;
      if (scaled >= static_cast<double>(max)) {
        raw = max;
      } else if (scaled > 0.0) {
        raw = static_cast<std::uint64_t>(scaled);
      }
    }
  }
  for (unsigned i = 0; i < signal.length; ++i) {
    const std::size_t p = positions[i];
    const auto mask = static_cast<std::uint8_t>(1u << (p % 8));
    if (((raw >> i) & 1) != 0) {
      data[p / 8] = static_cast<std::uint8_t>(data[p / 8] | mask);
    } else {
      data[p / 8] = static_cast<std::uint8_t>(data[p / 8] & ~mask);
    }
  }
  return true;
}

std::pair<double, double> physical_range(const Signal& signal) {
  if (signal.minimum < signal.maximum) {
    return {signal.minimum, signal.maximum};
  }
  const int bits = static_cast<int>(signal.length);
  const double raw_min = signal.sign == Sign::Signed ? -std::ldexp(1.0, bits - 1) : 0.0;
  const double raw_max =
      signal.sign == Sign::Signed ? std::ldexp(1.0, bits - 1) - 1.0 : std::ldexp(1.0, bits) - 1.0;
  const double a = raw_min * signal.factor + signal.offset;
  const double b = raw_max * signal.factor + signal.offset;
  return {std::min(a, b), std::max(a, b)};
}

std::vector<DecodedSignal> decode_message(const Message& message, const CanFrame& frame) {
  std::vector<DecodedSignal> out;
  for (const Signal& signal : message.signals) {
    if (const auto value = decode_signal(signal, frame.data.data(), frame.len)) {
      out.push_back({&signal, *value});
    }
  }
  return out;
}

std::string format_signal(const DecodedSignal& decoded) {
  char number[32];
  std::snprintf(number, sizeof number, "%.6g", decoded.value);
  std::string text = decoded.signal->name + "=" + number;
  if (!decoded.signal->unit.empty()) {
    text += " " + decoded.signal->unit;
  }
  return text;
}

const Message* Database::find(std::uint32_t id, bool extended) const noexcept {
  const auto it = index_.find(key(id, extended));
  return it == index_.end() ? nullptr : &messages_[it->second];
}

Database Database::load(const std::string& path) {
  std::ifstream file(path);
  if (!file) {
    throw std::runtime_error("cannot open DBC file '" + path + "'");
  }
  return parse(file);
}

Database Database::parse(std::istream& in) {
  static const std::regex message_re(R"(^BO_\s+(\d+)\s+(\w+)\s*:\s*(\d+)\s+(\w+))");
  static const std::regex signal_re(
      R"re(^SG_\s+(\w+)\s*(m\d+M?|M)?\s*:\s*(\d+)\|(\d+)@([01])([+-])\s*)re"
      R"re(\(\s*([^,\s]+)\s*,\s*([^)\s]+)\s*\)\s*\[\s*([^|\s]+)\s*\|\s*([^\]\s]+)\s*\]\s*)re"
      R"re("([^"]*)")re");
  static const std::regex cycle_re(R"(^BA_\s+"GenMsgCycleTime"\s+BO_\s+(\d+)\s+(\d+)\s*;)");

  Database db;
  std::string raw;
  std::size_t line_no = 0;
  while (std::getline(in, raw)) {
    ++line_no;
    const std::string line(detail::trim(raw));
    std::smatch m;

    if (starts_with(line, "BO_ ")) {
      if (!std::regex_search(line, m, message_re)) {
        fail(line_no, "malformed BO_ definition");
      }
      const std::uint64_t dbc_id = to_unsigned(m[1], 0xFFFFFFFFULL, line_no);
      Message message;
      message.extended = (dbc_id & kExtendedFlag) != 0;
      message.id = static_cast<std::uint32_t>(dbc_id & kMaxExtendedId);
      message.name = m[2];
      message.dlc = static_cast<unsigned>(to_unsigned(m[3], 64, line_no));
      message.sender = m[4];
      if (!is_valid_fd_length(message.dlc)) {
        fail(line_no, "invalid DLC " + std::to_string(message.dlc));
      }
      if (db.index_.count(key(message.id, message.extended)) != 0) {
        fail(line_no, "duplicate message id " + std::to_string(message.id));
      }
      db.index_[key(message.id, message.extended)] = db.messages_.size();
      db.messages_.push_back(std::move(message));
    } else if (starts_with(line, "SG_ ")) {
      if (db.messages_.empty()) {
        fail(line_no, "SG_ before any BO_");
      }
      if (!std::regex_search(line, m, signal_re)) {
        fail(line_no, "malformed SG_ definition");
      }
      if (m[2].matched) {
        ++db.skipped_multiplexed_;
        continue;
      }
      Signal signal;
      signal.name = m[1];
      signal.start_bit = static_cast<unsigned>(to_unsigned(m[3], 511, line_no));
      signal.length = static_cast<unsigned>(to_unsigned(m[4], 64, line_no));
      signal.byte_order = m[5] == "1" ? ByteOrder::LittleEndian : ByteOrder::BigEndian;
      signal.sign = m[6] == "-" ? Sign::Signed : Sign::Unsigned;
      signal.factor = to_double(m[7], line_no);
      signal.offset = to_double(m[8], line_no);
      signal.minimum = to_double(m[9], line_no);
      signal.maximum = to_double(m[10], line_no);
      signal.unit = m[11];
      if (signal.factor == 0.0) {
        fail(line_no, "signal '" + signal.name + "' has a zero factor");
      }
      std::array<std::size_t, 64> positions{};
      Message& message = db.messages_.back();
      if (!bit_positions(signal, std::size_t{message.dlc} * 8, positions)) {
        fail(line_no, "signal '" + signal.name + "' does not fit in message '" + message.name +
                          "' (" + std::to_string(message.dlc) + " bytes)");
      }
      message.signals.push_back(std::move(signal));
    } else if (starts_with(line, "BA_ \"GenMsgCycleTime\"")) {
      if (std::regex_search(line, m, cycle_re)) {
        const std::uint64_t dbc_id = to_unsigned(m[1], 0xFFFFFFFFULL, line_no);
        const bool extended = (dbc_id & kExtendedFlag) != 0;
        const auto id = static_cast<std::uint32_t>(dbc_id & kMaxExtendedId);
        const auto it = db.index_.find(key(id, extended));
        if (it != db.index_.end()) {
          db.messages_[it->second].cycle_ms =
              static_cast<unsigned>(to_unsigned(m[2], 3600000, line_no));
        }
      }
    }
  }
  return db;
}

}  // namespace canbus
