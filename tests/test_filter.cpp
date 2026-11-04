#include "canbus/filter.hpp"
#include "test_framework.hpp"

using namespace canbus;

namespace {

CanFrame frame(std::uint32_t id, bool extended = false) {
  CanFrame f;
  f.id = id;
  f.extended = extended;
  return f;
}

}  // namespace

TEST(filter, exact_id_match) {
  const Filter f = Filter::parse("123");
  CHECK(f.matches(frame(0x123)));
  CHECK(!f.matches(frame(0x124)));
  CHECK(!f.matches(frame(0x123, true)));
}

TEST(filter, mask_match) {
  const Filter f = Filter::parse("120:7F0");
  CHECK(f.matches(frame(0x12F)));
  CHECK(f.matches(frame(0x120)));
  CHECK(!f.matches(frame(0x130)));
}

TEST(filter, extended_selected_by_width_or_value) {
  CHECK(Filter::parse("18FEF100").matches(frame(0x18FEF100, true)));
  CHECK(!Filter::parse("18FEF100").matches(frame(0x18FEF100, false)));
  CHECK(Filter::parse("00000123").matches(frame(0x123, true)));
  CHECK(Filter::parse("800").matches(frame(0x800, true)));
}

TEST(filter, set_semantics) {
  FilterSet none;
  CHECK(none.empty());
  CHECK(none.accepts(frame(0x7FF)));

  FilterSet pass;
  pass.add(Filter::parse("100"));
  pass.add(Filter::parse("200:700"));
  CHECK(pass.accepts(frame(0x100)));
  CHECK(pass.accepts(frame(0x2AB)));
  CHECK(!pass.accepts(frame(0x300)));

  FilterSet reject;
  reject.add(Filter::parse("100~7FF"));
  CHECK(!reject.accepts(frame(0x100)));
  CHECK(reject.accepts(frame(0x101)));

  pass.add(Filter::parse("2AB~7FF"));
  CHECK(!pass.accepts(frame(0x2AB)));
  CHECK(pass.accepts(frame(0x2AC)));
}

TEST(filter, rejects_malformed_specs) {
  CHECK_THROWS(Filter::parse(""));
  CHECK_THROWS(Filter::parse("xyz"));
  CHECK_THROWS(Filter::parse("123:"));
  CHECK_THROWS(Filter::parse("123:zz"));
  CHECK_THROWS(Filter::parse("20000000"));
  CHECK_THROWS(Filter::parse("+12"));
}
