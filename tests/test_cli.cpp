#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "canbus/candump.hpp"
#include "canbus/cli.hpp"
#include "test_framework.hpp"

using namespace canbus;

namespace {

struct Result {
  int code = 0;
  std::string out;
  std::string err;
};

Result run(std::vector<std::string> args) {
  std::ostringstream out;
  std::ostringstream err;
  const std::atomic<bool> stop{false};
  Result r;
  r.code = run_cli(args, out, err, stop);
  r.out = out.str();
  r.err = err.str();
  return r;
}

std::string fixture(const std::string& name) { return std::string(CANBUS_FIXTURE_DIR) + "/" + name; }

std::size_t count_lines(const std::string& text) {
  return static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n'));
}

std::string temp_file(const std::string& name) {
  return (std::filesystem::temp_directory_path() / ("canscan_test_" + name)).string();
}

bool contains(const std::string& text, const std::string& part) {
  return text.find(part) != std::string::npos;
}

}  // namespace

TEST(cli, help_lists_every_command) {
  const Result r = run({"--help"});
  CHECK_EQ(r.code, 0);
  for (const char* word : {"monitor", "record", "stats", "replay", "generate"}) {
    CHECK(contains(r.out, word));
  }
}

TEST(cli, monitor_decodes_signals_from_a_log) {
  const Result r = run({"monitor", "--replay", fixture("sample.log"), "--dbc", fixture("sample.dbc"),
                        "--speed", "0"});
  CHECK_EQ(r.code, 0);
  CHECK_EQ(count_lines(r.out), std::size_t{6});
  CHECK(contains(r.out, "EngineData  EngineSpeed=1500 rpm  CoolantTemp=90 degC  ThrottlePos=40 %"));
  CHECK(contains(r.out, "SteeringAngle=-12.3 deg  WheelSpeed=123.45 km/h  BrakePressed=1"));
  CHECK(contains(r.out, "CruiseControl  SetSpeed=45 km/h"));
  CHECK(contains(r.out, "FdPayload  Counter=1  Delta=-12 m"));
  CHECK(contains(r.out, "7FF#R"));
}

TEST(cli, monitor_applies_filters_and_count) {
  const Result only = run({"monitor", "--replay", fixture("sample.log"), "--speed", "0", "-f", "100"});
  CHECK_EQ(count_lines(only.out), std::size_t{2});
  const Result rejected = run({"monitor", "--replay", fixture("sample.log"), "--speed", "0", "-f", "100~7FF"});
  CHECK_EQ(count_lines(rejected.out), std::size_t{4});
  const Result limited = run({"monitor", "--replay", fixture("sample.log"), "--speed", "0", "-n", "3"});
  CHECK_EQ(count_lines(limited.out), std::size_t{3});
  const Result timed = run({"monitor", "--replay", fixture("sample.log"), "--speed", "0", "-t", "0.015"});
  CHECK_EQ(count_lines(timed.out), std::size_t{3});
}

TEST(cli, live_view_prints_table_with_signals) {
  const Result r = run({"monitor", "--live", "--replay", fixture("sample.log"), "--dbc",
                        fixture("sample.dbc"), "--speed", "0"});
  CHECK_EQ(r.code, 0);
  CHECK(contains(r.out, "EngineSpeed=3000 rpm"));
  CHECK(contains(r.out, "frames/s"));
}

TEST(cli, record_roundtrips_the_log_byte_for_byte) {
  const std::string path = temp_file("roundtrip.log");
  const Result r = run({"record", "--replay", fixture("sample.log"), "--speed", "0", "-o", path});
  CHECK_EQ(r.code, 0);
  CHECK(contains(r.err, "recorded 6 frames"));
  std::ifstream recorded(path);
  std::ifstream original(fixture("sample.log"));
  std::stringstream a;
  std::stringstream b;
  a << recorded.rdbuf();
  b << original.rdbuf();
  CHECK_EQ(a.str(), b.str());
  std::filesystem::remove(path);
}

TEST(cli, generate_then_record_produces_decodable_traffic) {
  const std::string path = temp_file("generated.log");
  const Result r = run({"record", "--generate", fixture("sample.dbc"), "--speed", "0", "-n", "50", "-o", path});
  CHECK_EQ(r.code, 0);
  const auto lines = read_log_file(path);
  CHECK_EQ(lines.size(), std::size_t{50});
  CHECK_EQ(lines[0].iface, std::string("gen0"));
  std::filesystem::remove(path);

  const Result printed = run({"generate", fixture("sample.dbc"), "--speed", "0", "-n", "7"});
  CHECK_EQ(printed.code, 0);
  CHECK_EQ(count_lines(printed.out), std::size_t{7});
}

TEST(cli, monitor_of_generated_traffic_uses_the_generator_dbc_for_decoding) {
  const Result r = run({"monitor", "--generate", fixture("sample.dbc"), "--speed", "0", "-n", "5"});
  CHECK(contains(r.out, "EngineData  EngineSpeed="));
}

TEST(cli, replay_without_interface_prints_the_log) {
  const Result r = run({"replay", fixture("sample.log"), "--speed", "0"});
  CHECK_EQ(r.code, 0);
  CHECK_EQ(count_lines(r.out), std::size_t{6});
}

TEST(cli, replay_loops_until_count) {
  const Result r = run({"replay", fixture("sample.log"), "--speed", "0", "--loop", "-n", "20"});
  CHECK_EQ(count_lines(r.out), std::size_t{20});
}

TEST(cli, stats_reports_load_and_ids) {
  const Result r = run({"stats", "--replay", fixture("sample.log"), "--speed", "0"});
  CHECK_EQ(r.code, 0);
  CHECK(contains(r.out, "Frames    6"));
  CHECK(contains(r.out, "Bus load"));
  CHECK(contains(r.out, "18FEF100"));
}

TEST(cli, usage_errors_return_two) {
  CHECK_EQ(run({}).code, 2);
  CHECK_EQ(run({"bogus"}).code, 2);
  CHECK_EQ(run({"monitor"}).code, 2);                                         // no input
  CHECK_EQ(run({"monitor", "-i", "vcan0", "--replay", "x.log"}).code, 2);     // two inputs
  CHECK_EQ(run({"record", "--replay", fixture("sample.log")}).code, 2);       // no -o
  CHECK_EQ(run({"replay"}).code, 2);
  CHECK_EQ(run({"generate", fixture("sample.dbc"), "--speed", "0"}).code, 2);  // endless burst
  CHECK_EQ(run({"stats", "--replay", fixture("sample.log"), "--live"}).code, 2);
  CHECK_EQ(run({"monitor", "--replay", fixture("sample.log"), "--speed", "fast"}).code, 2);
  CHECK_EQ(run({"monitor", "--replay"}).code, 2);
  CHECK_EQ(run({"monitor", "--replay", fixture("sample.log"), "-f", "zz"}).code, 2);
  CHECK_EQ(run({"monitor", "--replay", fixture("sample.log"), "--bitrate", "0"}).code, 2);
}

TEST(cli, runtime_errors_return_one_with_message) {
  const Result missing = run({"monitor", "--replay", "/nonexistent.log"});
  CHECK_EQ(missing.code, 1);
  CHECK(contains(missing.err, "cannot open log file"));
  CHECK_EQ(run({"monitor", "-i", "nonexistent0"}).code, 1);
  CHECK_EQ(run({"replay", fixture("sample.log"), "-i", "nonexistent0"}).code, 1);
  CHECK_EQ(run({"monitor", "--replay", fixture("sample.log"), "--dbc", "/nonexistent.dbc"}).code, 1);
}

TEST(cli, multiplexed_signals_produce_a_warning) {
  const Result r = run({"stats", "--replay", fixture("sample.log"), "--dbc", fixture("sample.dbc"), "--speed", "0"});
  CHECK(contains(r.err, "2 multiplexed signal(s)"));
}
