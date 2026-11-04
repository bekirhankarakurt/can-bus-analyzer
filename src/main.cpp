#include <atomic>
#include <csignal>
#include <iostream>
#include <string>
#include <vector>

#include "canbus/cli.hpp"

namespace {

std::atomic<bool> g_stop{false};

void on_signal(int /*signal_number*/) { g_stop.store(true); }

}  // namespace

int main(int argc, char** argv) {
  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);
  const std::vector<std::string> args(argv + 1, argv + argc);
  return canbus::run_cli(args, std::cout, std::cerr, g_stop);
}
