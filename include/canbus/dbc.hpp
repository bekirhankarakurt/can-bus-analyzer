#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "canbus/frame.hpp"

namespace canbus {

enum class ByteOrder : std::uint8_t { LittleEndian, BigEndian };  ///< DBC @1 = Intel, @0 = Motorola
enum class Sign : std::uint8_t { Unsigned, Signed };

struct Signal {
  std::string name;
  unsigned start_bit = 0;  ///< LSB for little endian, MSB for big endian (DBC convention)
  unsigned length = 1;
  ByteOrder byte_order = ByteOrder::LittleEndian;
  Sign sign = Sign::Unsigned;
  double factor = 1.0;
  double offset = 0.0;
  double minimum = 0.0;
  double maximum = 0.0;
  std::string unit;
};

struct Message {
  std::uint32_t id = 0;
  bool extended = false;
  std::string name;
  unsigned dlc = 0;
  std::string sender;
  std::vector<Signal> signals;
  std::optional<unsigned> cycle_ms;  ///< from BA_ "GenMsgCycleTime"
};

struct DecodedSignal {
  const Signal* signal = nullptr;
  double value = 0.0;
};

/// Physical value = raw * factor + offset. Returns nullopt if the signal does not
/// fit inside `len` payload bytes.
[[nodiscard]] std::optional<double> decode_signal(const Signal& signal, const std::uint8_t* data,
                                                  std::size_t len);

/// Writes `physical` into the payload (rounded and clamped to the raw range).
/// Returns false, leaving `data` untouched, if the signal does not fit in `len` bytes.
[[nodiscard]] bool encode_signal(const Signal& signal, double physical, std::uint8_t* data,
                                 std::size_t len);

/// Declared [minimum, maximum] if set, otherwise the range the raw bits can represent.
[[nodiscard]] std::pair<double, double> physical_range(const Signal& signal);

/// Decodes every signal of `message` that fits in the frame payload.
[[nodiscard]] std::vector<DecodedSignal> decode_message(const Message& message,
                                                        const CanFrame& frame);

/// "EngineSpeed=1500 rpm"
[[nodiscard]] std::string format_signal(const DecodedSignal& decoded);

class Database {
 public:
  /// Supports BO_, SG_ (both byte orders, signed/unsigned, factor/offset) and
  /// BA_ "GenMsgCycleTime". Multiplexed signals are skipped and counted.
  /// Throws std::runtime_error with a line number on malformed input.
  [[nodiscard]] static Database parse(std::istream& in);
  [[nodiscard]] static Database load(const std::string& path);

  [[nodiscard]] const Message* find(std::uint32_t id, bool extended) const noexcept;
  [[nodiscard]] const std::vector<Message>& messages() const noexcept { return messages_; }
  [[nodiscard]] std::size_t skipped_multiplexed() const noexcept { return skipped_multiplexed_; }

 private:
  std::vector<Message> messages_;
  std::map<std::uint64_t, std::size_t> index_;
  std::size_t skipped_multiplexed_ = 0;
};

}  // namespace canbus
