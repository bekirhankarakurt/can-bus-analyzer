#include <atomic>
#include <chrono>
#include <sstream>

#include "canbus/socketcan.hpp"
#include "canbus/source.hpp"
#include "test_framework.hpp"

using namespace canbus;

namespace {

std::vector<LogLine> fixture_log() {
  return read_log_file(std::string(CANBUS_FIXTURE_DIR) + "/sample.log");
}

Database fixture_dbc() { return Database::load(std::string(CANBUS_FIXTURE_DIR) + "/sample.dbc"); }

}  // namespace

TEST(sources, log_source_replays_in_order_then_ends) {
  LogSource source(fixture_log());
  CHECK_EQ(source.name(), std::string("vcan0"));
  CanFrame f;
  int count = 0;
  double last = 0.0;
  while (source.next(f) == ReadStatus::Frame) {
    CHECK(f.timestamp >= last);
    last = f.timestamp;
    ++count;
  }
  CHECK_EQ(count, 6);
  CHECK(source.next(f) == ReadStatus::End);
}

TEST(sources, log_source_loop_keeps_timestamps_increasing) {
  LogSource source(fixture_log(), true);
  CanFrame f;
  double last = 0.0;
  for (int i = 0; i < 20; ++i) {
    CHECK(source.next(f) == ReadStatus::Frame);
    CHECK(f.timestamp > last);
    last = f.timestamp;
  }
}

TEST(sources, empty_log_source_ends_even_when_looping) {
  LogSource source({}, true);
  CanFrame f;
  CHECK(source.next(f) == ReadStatus::End);
}

TEST(sources, generator_honours_cycle_times_and_signal_ranges) {
  const Database db = fixture_dbc();
  GeneratorSource gen(db, 1000.0);
  CanFrame f;
  double previous_engine = -1.0;
  int engine_frames = 0;
  for (int i = 0; i < 400; ++i) {
    CHECK(gen.next(f) == ReadStatus::Frame);
    const Message* message = db.find(f.id, f.extended);
    CHECK(message != nullptr);
    if (message == nullptr) {
      return;
    }
    for (const DecodedSignal& d : decode_message(*message, f)) {
      const auto [lo, hi] = physical_range(*d.signal);
      CHECK(d.value >= lo - d.signal->factor && d.value <= hi + d.signal->factor);
    }
    if (f.id == 256) {
      if (previous_engine >= 0.0) {
        CHECK_NEAR(f.timestamp - previous_engine, 0.010, 1e-9);
      }
      previous_engine = f.timestamp;
      ++engine_frames;
    }
  }
  CHECK(engine_frames > 100);
}

TEST(sources, generator_emits_fd_frames_for_long_messages_and_is_deterministic) {
  const Database db = fixture_dbc();
  GeneratorSource a(db, 5.0);
  GeneratorSource b(db, 5.0);
  bool saw_fd = false;
  for (int i = 0; i < 200; ++i) {
    CanFrame fa;
    CanFrame fb;
    CHECK(a.next(fa) == ReadStatus::Frame && b.next(fb) == ReadStatus::Frame);
    CHECK(same_content(fa, fb));
    if (fa.id == 768) {
      saw_fd = true;
      CHECK(fa.fd && fa.brs && fa.len == 12);
    }
  }
  CHECK(saw_fd);
}

TEST(sources, generator_rejects_empty_database) {
  std::istringstream empty("VERSION \"\"\n");
  CHECK_THROWS(GeneratorSource(Database::parse(empty), 0.0));
}

TEST(sources, paced_source_waits_for_frame_timestamps) {
  std::atomic<bool> stop{false};
  PacedSource paced(std::make_unique<LogSource>(fixture_log()), 1.0, stop);
  CanFrame f;
  const auto start = std::chrono::steady_clock::now();
  int count = 0;
  while (paced.next(f) == ReadStatus::Frame) {
    ++count;
  }
  const auto elapsed = std::chrono::steady_clock::now() - start;
  CHECK_EQ(count, 6);
  CHECK(elapsed >= std::chrono::milliseconds(28));  // log spans 30 ms
  CHECK(elapsed < std::chrono::milliseconds(500));
}

TEST(sources, paced_source_stops_promptly_when_requested) {
  std::atomic<bool> stop{false};
  PacedSource paced(std::make_unique<LogSource>(fixture_log()), 0.00001, stop);  // ~50 min span
  CanFrame f;
  CHECK(paced.next(f) == ReadStatus::Frame);
  stop.store(true);
  CHECK(paced.next(f) == ReadStatus::End);
  CHECK_THROWS(PacedSource(std::make_unique<LogSource>(fixture_log()), 0.0, stop));
}

TEST(sources, stream_sink_prints_candump_lines) {
  std::ostringstream out;
  StreamSink sink(out, "gen0");
  sink.send(fixture_log()[0].frame);
  CHECK_EQ(out.str(), std::string("(1700000000.000000) gen0 100#7017826400000000\n"));
}

TEST(sources, socketcan_reports_missing_interface) {
  CHECK_THROWS(SocketCan("nonexistent0"));
  CHECK_THROWS(SocketCan(""));
  CHECK_THROWS(SocketCan("interface_name_too_long"));
}
