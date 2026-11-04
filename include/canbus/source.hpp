#pragma once

#include <atomic>
#include <chrono>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

#include "canbus/candump.hpp"
#include "canbus/dbc.hpp"
#include "canbus/frame.hpp"

namespace canbus {

enum class ReadStatus {
  Frame,    // a frame was stored in the output argument
  Timeout,  // nothing arrived yet; call again (lets callers poll a stop flag)
  End,      // the source is exhausted
};

class FrameSource {
 public:
  virtual ~FrameSource() = default;
  [[nodiscard]] virtual ReadStatus next(CanFrame& out) = 0;
  [[nodiscard]] virtual std::string name() const = 0;
};

class FrameSink {
 public:
  virtual ~FrameSink() = default;
  virtual void send(const CanFrame& frame) = 0;
};

// Replays a parsed candump log. With `loop`, timestamps keep increasing across passes.
class LogSource final : public FrameSource {
 public:
  explicit LogSource(const std::vector<LogLine>& lines, bool loop = false);
  ReadStatus next(CanFrame& out) override;
  [[nodiscard]] std::string name() const override { return iface_; }

 private:
  std::vector<CanFrame> frames_;
  std::string iface_;
  bool loop_;
  std::size_t index_ = 0;
  double span_ = 0.0;
  double offset_ = 0.0;
};

// Synthesizes traffic from a DBC: every message is emitted at its GenMsgCycleTime
// (100 ms if unset) with each signal sweeping its range on a sine wave. Fully
// deterministic for a given start time. Never ends.
class GeneratorSource final : public FrameSource {
 public:
  GeneratorSource(const Database& db, double start_time);
  ReadStatus next(CanFrame& out) override;
  [[nodiscard]] std::string name() const override { return "gen0"; }

 private:
  struct Slot {
    Message message;
    double period_s = 0.1;
    std::uint64_t tick = 0;
  };

  [[nodiscard]] double due(const Slot& slot) const noexcept;

  std::vector<Slot> slots_;
  double start_;
};

// Releases frames of `inner` in real time (scaled by `speed`), based on their timestamps.
// Returns End as soon as `stop` is set.
class PacedSource final : public FrameSource {
 public:
  // Throws std::invalid_argument unless speed > 0.
  PacedSource(std::unique_ptr<FrameSource> inner, double speed, const std::atomic<bool>& stop);
  ReadStatus next(CanFrame& out) override;
  [[nodiscard]] std::string name() const override { return inner_->name(); }

 private:
  std::unique_ptr<FrameSource> inner_;
  double speed_;
  const std::atomic<bool>& stop_;
  bool started_ = false;
  double first_timestamp_ = 0.0;
  std::chrono::steady_clock::time_point wall_start_;
};

// Prints frames as candump lines.
class StreamSink final : public FrameSink {
 public:
  StreamSink(std::ostream& out, std::string iface) : out_(out), iface_(std::move(iface)) {}
  void send(const CanFrame& frame) override;

 private:
  std::ostream& out_;
  std::string iface_;
};

}  // namespace canbus
