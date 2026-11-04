#include "canbus/candump.hpp"
#include "canbus/dbc.hpp"
#include "canbus/live.hpp"
#include "canbus/stats.hpp"
#include "test_framework.hpp"

using namespace canbus;

namespace {

CanFrame frame(std::uint32_t id, std::uint8_t len, double t) {
  CanFrame f;
  f.id = id;
  f.len = len;
  f.timestamp = t;
  return f;
}

}  // namespace

TEST(stats, wire_bit_estimates) {
  CHECK_EQ(wire_bits(frame(1, 8, 0)), std::size_t{111});
  CHECK_EQ(wire_bits(frame(1, 0, 0)), std::size_t{47});
  CanFrame ext = frame(1, 8, 0);
  ext.extended = true;
  CHECK_EQ(wire_bits(ext), std::size_t{131});
  CanFrame rtr = frame(1, 4, 0);
  rtr.rtr = true;
  CHECK_EQ(wire_bits(rtr), std::size_t{47});
  CanFrame fd = frame(1, 64, 0);
  fd.fd = true;
  CHECK_EQ(wire_bits(fd), std::size_t{28 + 512 + 21 + 13});
}

TEST(stats, load_rate_and_period) {
  StatsCollector stats(500000);
  stats.add(frame(0x100, 8, 10.0));
  stats.add(frame(0x100, 8, 10.5));
  stats.add(frame(0x200, 8, 11.0));
  const StatsSnapshot s = stats.snapshot();
  CHECK_EQ(s.frames, std::uint64_t{3});
  CHECK_EQ(s.bytes, std::uint64_t{24});
  CHECK_NEAR(s.duration_s, 1.0, 1e-12);
  CHECK_NEAR(s.frames_per_s, 3.0, 1e-12);
  CHECK_NEAR(s.load_percent, 100.0 * 333.0 / 500000.0, 1e-9);
  CHECK_EQ(s.ids.size(), std::size_t{2});
  CHECK_EQ(s.ids[0].id, 0x100u);
  CHECK_EQ(s.ids[0].count, std::uint64_t{2});
  CHECK_NEAR(s.ids[0].period_ms, 500.0, 1e-9);
  CHECK_NEAR(s.ids[1].period_ms, 0.0, 0.0);
}

TEST(stats, empty_and_single_frame_have_zero_rates) {
  StatsCollector stats(250000);
  CHECK_EQ(stats.snapshot().frames, std::uint64_t{0});
  stats.add(frame(1, 1, 5.0));
  CHECK_NEAR(stats.snapshot().load_percent, 0.0, 0.0);
  CHECK_THROWS(StatsCollector(0));
}

TEST(stats, report_lists_every_id) {
  StatsCollector stats(500000);
  stats.add(frame(0x100, 8, 0.0));
  stats.add(frame(0x18FEF100, 1, 1.0));
  CanFrame ext = frame(0x18FEF100, 1, 1.0);
  ext.extended = true;
  stats.add(ext);
  const std::string report = format_report(stats.snapshot());
  CHECK(report.find("Bus load") != std::string::npos);
  CHECK(report.find("100 ") != std::string::npos);
  CHECK(report.find("18FEF100") != std::string::npos);
}

TEST(live, renders_latest_values_with_decoded_signals) {
  const Database db = Database::load(std::string(CANBUS_FIXTURE_DIR) + "/sample.dbc");
  LiveView view(&db);
  for (const LogLine& line : read_log_file(std::string(CANBUS_FIXTURE_DIR) + "/sample.log")) {
    view.update(line.frame);
  }
  const std::string text = view.render(StatsSnapshot{});
  CHECK(text.find("EngineData") != std::string::npos);
  CHECK(text.find("EngineSpeed=3000 rpm") != std::string::npos);  // latest, not first, frame
  CHECK(text.find("EngineSpeed=1500 rpm") == std::string::npos);
  CHECK(text.find("SteeringAngle=-12.3 deg") != std::string::npos);
  CHECK(text.find("7FF") != std::string::npos);
}

TEST(live, works_without_a_database) {
  LiveView view(nullptr);
  view.update(frame(0x123, 2, 0));
  CHECK(view.render(StatsSnapshot{}).find("123") != std::string::npos);
}
