#include "canbus/source.hpp"

#include <algorithm>
#include <cmath>
#include <ostream>
#include <stdexcept>
#include <thread>

namespace canbus {
namespace {

constexpr unsigned kDefaultCycleMs = 100;
constexpr double kTwoPi = 6.283185307179586;
constexpr std::chrono::milliseconds kStopPollInterval{20};

}  // namespace

LogSource::LogSource(const std::vector<LogLine>& lines, bool loop) : loop_(loop) {
  frames_.reserve(lines.size());
  for (const LogLine& line : lines) {
    frames_.push_back(line.frame);
  }
  if (!lines.empty()) {
    iface_ = lines.front().iface;
    span_ = std::max(0.0, frames_.back().timestamp - frames_.front().timestamp);
  }
}

ReadStatus LogSource::next(CanFrame& out) {
  if (index_ >= frames_.size()) {
    if (!loop_ || frames_.empty()) {
      return ReadStatus::End;
    }
    offset_ += span_ + 0.001;
    index_ = 0;
  }
  out = frames_[index_++];
  out.timestamp += offset_;
  return ReadStatus::Frame;
}

GeneratorSource::GeneratorSource(const Database& db, double start_time) : start_(start_time) {
  for (const Message& message : db.messages()) {
    Slot slot;
    slot.message = message;
    const unsigned cycle = message.cycle_ms.value_or(0) > 0 ? *message.cycle_ms : kDefaultCycleMs;
    slot.period_s = cycle / 1000.0;
    slots_.push_back(std::move(slot));
  }
  if (slots_.empty()) {
    throw std::invalid_argument("the DBC defines no messages to generate");
  }
}

double GeneratorSource::due(const Slot& slot) const noexcept {
  return start_ + static_cast<double>(slot.tick) * slot.period_s;
}

ReadStatus GeneratorSource::next(CanFrame& out) {
  const auto earliest = std::min_element(
      slots_.begin(), slots_.end(),
      [this](const Slot& a, const Slot& b) { return due(a) < due(b); });
  const std::size_t slot_index = static_cast<std::size_t>(earliest - slots_.begin());
  const Message& message = earliest->message;
  const double when = due(*earliest);

  out = CanFrame{};
  out.id = message.id;
  out.extended = message.extended;
  out.len = static_cast<std::uint8_t>(message.dlc);
  out.fd = message.dlc > 8;
  out.brs = out.fd;
  out.timestamp = when;
  for (std::size_t i = 0; i < message.signals.size(); ++i) {
    const Signal& signal = message.signals[i];
    const auto [low, high] = physical_range(signal);
    const double k = static_cast<double>(slot_index + i);
    const double wave = std::sin(kTwoPi * (when - start_) / (4.0 + k) + 0.7 * k);
    const double value = (low + high) / 2.0 + (high - low) / 2.0 * wave;
    (void)encode_signal(signal, value, out.data.data(), out.len);
  }
  ++earliest->tick;
  return ReadStatus::Frame;
}

PacedSource::PacedSource(std::unique_ptr<FrameSource> inner, double speed,
                         const std::atomic<bool>& stop)
    : inner_(std::move(inner)), speed_(speed), stop_(stop) {
  if (!(speed > 0.0)) {
    throw std::invalid_argument("speed must be greater than zero");
  }
}

ReadStatus PacedSource::next(CanFrame& out) {
  const ReadStatus status = inner_->next(out);
  if (status != ReadStatus::Frame) {
    return status;
  }
  if (!started_) {
    started_ = true;
    first_timestamp_ = out.timestamp;
    wall_start_ = std::chrono::steady_clock::now();
    return ReadStatus::Frame;
  }
  const auto offset = std::chrono::duration<double>((out.timestamp - first_timestamp_) / speed_);
  const auto deadline = wall_start_ + std::chrono::duration_cast<std::chrono::steady_clock::duration>(offset);
  for (;;) {
    if (stop_.load()) {
      return ReadStatus::End;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      return ReadStatus::Frame;
    }
    std::this_thread::sleep_for(std::min<std::chrono::steady_clock::duration>(
        deadline - now, kStopPollInterval));
  }
}

void StreamSink::send(const CanFrame& frame) {
  out_ << format_frame(frame, iface_) << std::endl;
}

}  // namespace canbus
