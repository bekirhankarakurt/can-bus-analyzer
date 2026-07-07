#include <sstream>

#include "canbus/candump.hpp"
#include "test_framework.hpp"

using namespace canbus;

namespace {

CanFrame make(std::uint32_t id, std::initializer_list<std::uint8_t> bytes, bool extended = false) {
  CanFrame f;
  f.id = id;
  f.extended = extended;
  f.timestamp = 1700000000.5;
  for (const auto b : bytes) {
    f.data[f.len++] = b;
  }
  return f;
}

}  // namespace

TEST(candump, formats_classical_frame) {
  CHECK_EQ(format_frame(make(0x123, {0xDE, 0xAD, 0xBE, 0xEF}), "vcan0"),
           std::string("(1700000000.500000) vcan0 123#DEADBEEF"));
}

TEST(candump, formats_extended_id_with_eight_digits) {
  CHECK_EQ(format_frame(make(0x18FEF100, {0x01}, true), "can1"),
           std::string("(1700000000.500000) can1 18FEF100#01"));
}

TEST(candump, formats_fd_and_rtr) {
  CanFrame fd = make(0x300, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12});
  fd.fd = true;
  fd.brs = true;
  fd.esi = true;
  CHECK_EQ(format_frame(fd, "vcan0"), std::string("(1700000000.500000) vcan0 300##3010203040506070809"
                                                   "0A0B0C"));
  CanFrame rtr = make(0x7FF, {});
  rtr.rtr = true;
  CHECK_EQ(format_frame(rtr, "vcan0"), std::string("(1700000000.500000) vcan0 7FF#R"));
  rtr.len = 4;
  CHECK_EQ(format_frame(rtr, "vcan0"), std::string("(1700000000.500000) vcan0 7FF#R4"));
}

TEST(candump, roundtrips_every_frame_kind) {
  CanFrame fd = make(0x1ABCDEF0, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}, true);
  fd.fd = true;
  fd.brs = true;
  CanFrame rtr = make(0x42, {});
  rtr.rtr = true;
  rtr.len = 3;
  for (const CanFrame& original : {make(0x001, {}), make(0x7FF, {1, 2, 3, 4, 5, 6, 7, 8}), fd, rtr}) {
    const auto parsed = parse_line(format_frame(original, "vcan7"));
    CHECK(parsed.has_value());
    if (parsed) {
      CHECK(same_content(parsed->frame, original));
      CHECK_EQ(parsed->iface, std::string("vcan7"));
      CHECK_NEAR(parsed->frame.timestamp, original.timestamp, 1e-6);
    }
  }
}

TEST(candump, accepts_dot_separated_data) {
  const auto parsed = parse_line("(1.000000) can0 123#11.22.33");
  CHECK(parsed.has_value());
  if (parsed) {
    CHECK_EQ(static_cast<int>(parsed->frame.len), 3);
    CHECK_EQ(static_cast<int>(parsed->frame.data[2]), 0x33);
  }
}

TEST(candump, rejects_malformed_lines) {
  CHECK(!parse_line("").has_value());
  CHECK(!parse_line("vcan0 123#11").has_value());
  CHECK(!parse_line("(1.0) vcan0 12#11").has_value());       // bad id width
  CHECK(!parse_line("(1.0) vcan0 800#11").has_value());      // id above 11 bits
  CHECK(!parse_line("(1.0) vcan0 123#1").has_value());       // odd digit count
  CHECK(!parse_line("(1.0) vcan0 123#112233445566778899").has_value());  // 9 bytes classical
  CHECK(!parse_line("(1.0) vcan0 123##0112233445566778899").has_value());  // 9 bytes FD
  CHECK(!parse_line("(1.0) vcan0 123#ZZ").has_value());
  CHECK(!parse_line("(abc) vcan0 123#11").has_value());
}

TEST(candump, read_log_skips_comments_and_reports_line_numbers) {
  std::istringstream good("# header\n\n(1.000000) can0 123#11\n");
  CHECK_EQ(read_log(good).size(), std::size_t{1});
  std::istringstream bad("(1.000000) can0 123#11\nnonsense\n");
  try {
    (void)read_log(bad);
    CHECK(false);
  } catch (const std::runtime_error& e) {
    CHECK(std::string(e.what()).find("line 2") != std::string::npos);
  }
}

TEST(candump, reads_fixture_log) {
  const auto lines = read_log_file(std::string(CANBUS_FIXTURE_DIR) + "/sample.log");
  CHECK_EQ(lines.size(), std::size_t{6});
  CHECK(lines[4].frame.fd && lines[4].frame.brs);
  CHECK_EQ(static_cast<int>(lines[4].frame.len), 12);
  CHECK(lines[3].frame.extended);
  CHECK(lines[5].frame.rtr);
  CHECK_THROWS(read_log_file("/nonexistent/file.log"));
}
