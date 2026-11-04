#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "canbus/dbc.hpp"
#include "canbus/frame.hpp"
#include "canbus/stats.hpp"

namespace canbus {

/// Keeps the latest frame per identifier and renders a refreshable table with the
/// decoded signal values underneath each row. `db` may be null (raw view only) and
/// must outlive the view.
class LiveView {
 public:
  explicit LiveView(const Database* db) noexcept : db_(db) {}

  void update(const CanFrame& frame);
  [[nodiscard]] std::string render(const StatsSnapshot& stats) const;

 private:
  struct Entry {
    CanFrame last;
    std::uint64_t count = 0;
  };

  const Database* db_;
  std::map<std::uint64_t, Entry> entries_;
};

/// "11 22 33" (RTR frames have no data).
[[nodiscard]] std::string format_data(const CanFrame& frame);

}  // namespace canbus
