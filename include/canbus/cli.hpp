#pragma once

#include <atomic>
#include <iosfwd>
#include <string>
#include <vector>

namespace canbus {

/// Runs the canscan command line. `args` excludes the program name. Returns the
/// process exit code: 0 success, 1 runtime error, 2 usage error. Long-running
/// commands return cleanly once `stop` becomes true.
[[nodiscard]] int run_cli(const std::vector<std::string>& args, std::ostream& out,
                          std::ostream& err, const std::atomic<bool>& stop);

}  // namespace canbus
