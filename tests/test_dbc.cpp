#include <cmath>
#include <sstream>
#include <string_view>

#include "canbus/candump.hpp"
#include "canbus/dbc.hpp"
#include "test_framework.hpp"

using namespace canbus;

namespace {

Database fixture() { return Database::load(std::string(CANBUS_FIXTURE_DIR) + "/sample.dbc"); }

const Signal& signal_of(const Message& m, std::string_view name) {
  for (const Signal& s : m.signals) {
    if (s.name == name) {
      return s;
    }
  }
  throw std::runtime_error("no such signal");
}

}  // namespace

TEST(dbc, parses_messages_signals_and_cycle_times) {
  const Database db = fixture();
  CHECK_EQ(db.messages().size(), std::size_t{5});
  const Message* engine = db.find(256, false);
  CHECK(engine != nullptr);
  if (engine == nullptr) {
    return;
  }
  CHECK_EQ(engine->name, std::string("EngineData"));
  CHECK_EQ(engine->signals.size(), std::size_t{3});
  CHECK(engine->cycle_ms.has_value() && *engine->cycle_ms == 10u);
  const Signal& temp = signal_of(*engine, "CoolantTemp");
  CHECK_NEAR(temp.offset, -40.0, 0.0);
  CHECK_EQ(temp.unit, std::string("degC"));
  CHECK(signal_of(*db.find(512, false), "SteeringAngle").byte_order == ByteOrder::BigEndian);
  CHECK(signal_of(*db.find(512, false), "SteeringAngle").sign == Sign::Signed);
}

TEST(dbc, extended_ids_are_distinct_from_standard) {
  const Database db = fixture();
  const Message* cruise = db.find(0x18FEF100, true);
  CHECK(cruise != nullptr);
  CHECK(db.find(0x18FEF100, false) == nullptr);
  CHECK(db.find(0x100, true) == nullptr);
}

TEST(dbc, skips_multiplexed_signals) {
  const Database db = fixture();
  CHECK_EQ(db.skipped_multiplexed(), std::size_t{2});
  CHECK(db.find(1024, false)->signals.empty());
}

TEST(dbc, decodes_little_endian_with_scale_and_offset) {
  const Database db = fixture();
  const auto frame = parse_line("(1.0) vcan0 100#7017826400000000")->frame;
  const auto decoded = decode_message(*db.find(256, false), frame);
  CHECK_EQ(decoded.size(), std::size_t{3});
  CHECK_NEAR(decoded[0].value, 1500.0, 1e-9);
  CHECK_NEAR(decoded[1].value, 90.0, 1e-9);
  CHECK_NEAR(decoded[2].value, 40.0, 1e-9);
  CHECK_EQ(format_signal(decoded[0]), std::string("EngineSpeed=1500 rpm"));
}

TEST(dbc, decodes_big_endian_signed_and_unsigned) {
  const Database db = fixture();
  const auto frame = parse_line("(1.0) vcan0 200#FF85303901000000")->frame;
  const auto decoded = decode_message(*db.find(512, false), frame);
  CHECK_EQ(decoded.size(), std::size_t{3});
  CHECK_NEAR(decoded[0].value, -12.3, 1e-9);
  CHECK_NEAR(decoded[1].value, 123.45, 1e-9);
  CHECK_NEAR(decoded[2].value, 1.0, 0.0);
}

TEST(dbc, decodes_can_fd_payload_beyond_eight_bytes) {
  const Database db = fixture();
  const auto fd = read_log_file(std::string(CANBUS_FIXTURE_DIR) + "/sample.log")[4].frame;
  const auto decoded = decode_message(*db.find(768, false), fd);
  CHECK_EQ(decoded.size(), std::size_t{2});
  CHECK_NEAR(decoded[0].value, 1.0, 0.0);
  CHECK_NEAR(decoded[1].value, -12.0, 1e-9);
}

TEST(dbc, signals_that_do_not_fit_the_frame_are_omitted) {
  const Database db = fixture();
  CanFrame frame;
  frame.len = 2;
  CHECK_EQ(decode_message(*db.find(256, false), frame).size(), std::size_t{1});
}

TEST(dbc, encode_decode_roundtrip_for_both_byte_orders) {
  const Database db = fixture();
  for (const unsigned id : {256u, 512u}) {
    for (const Signal& s : db.find(id, false)->signals) {
      const auto [lo, hi] = physical_range(s);
      for (const double target : {lo, (lo + hi) / 2, hi}) {
        std::uint8_t data[8] = {};
        CHECK(encode_signal(s, target, data, 8));
        const auto back = decode_signal(s, data, 8);
        CHECK(back.has_value());
        if (back) {
          CHECK_NEAR(*back, target, std::abs(s.factor) / 2 + 1e-9);
        }
      }
    }
  }
}

TEST(dbc, encode_clamps_and_preserves_neighbouring_bits) {
  const Database db = fixture();
  const Signal& rpm = signal_of(*db.find(256, false), "EngineSpeed");
  std::uint8_t data[8] = {0, 0, 0xAA, 0xBB, 0, 0, 0, 0};
  CHECK(encode_signal(rpm, 1e9, data, 8));
  CHECK_EQ(static_cast<int>(data[0]), 0xFF);
  CHECK_EQ(static_cast<int>(data[1]), 0xFF);
  CHECK_EQ(static_cast<int>(data[2]), 0xAA);
  CHECK(encode_signal(rpm, -5.0, data, 8));
  CHECK_EQ(static_cast<int>(data[0]), 0);
  CHECK_EQ(static_cast<int>(data[3]), 0xBB);
  CHECK(!encode_signal(rpm, 1.0, data, 1));
}

TEST(dbc, rejects_malformed_input_with_line_numbers) {
  const auto parse = [](const std::string& text) {
    std::istringstream in(text);
    return Database::parse(in);
  };
  CHECK_THROWS(parse("SG_ X : 0|8@1+ (1,0) [0|1] \"\" N\n"));
  CHECK_THROWS(parse("BO_ 1 A: 8 N\n SG_ X : 0|8@1+ (0,0) [0|1] \"\" N\n"));
  CHECK_THROWS(parse("BO_ 1 A: 1 N\n SG_ X : 0|16@1+ (1,0) [0|1] \"\" N\n"));
  CHECK_THROWS(parse("BO_ 1 A: 8 N\nBO_ 1 B: 8 N\n"));
  CHECK_THROWS(parse("BO_ 1 A: 9 N\n"));
  CHECK_THROWS(parse("BO_ garbage\n"));
  CHECK_THROWS(Database::load("/nonexistent.dbc"));
  try {
    (void)parse("BO_ 1 A: 8 N\nBO_ nonsense\n");
    CHECK(false);
  } catch (const std::runtime_error& e) {
    CHECK(std::string(e.what()).find("line 2") != std::string::npos);
  }
}
