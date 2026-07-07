#include <exception>
#include <iostream>
#include <string>

#include "test_framework.hpp"

// Usage: canbus_tests [suite]   -- runs every test, or only the named suite.
int main(int argc, char** argv) {
  const std::string only = argc > 1 ? argv[1] : "";
  int ran = 0;
  for (const auto& test : testing::registry()) {
    if (!only.empty() && test.suite != only) {
      continue;
    }
    const int before = testing::failure_count();
    try {
      test.body();
    } catch (const std::exception& e) {
      testing::fail(__FILE__, __LINE__, test.suite + "." + test.name + " threw: " + e.what());
    }
    ++ran;
    std::cout << (testing::failure_count() == before ? "[ ok ] " : "[FAIL] ") << test.suite
              << '.' << test.name << '\n';
  }
  if (ran == 0) {
    std::cerr << "no tests matched '" << only << "'\n";
    return 2;
  }
  std::cout << ran << " tests, " << testing::failure_count() << " failed checks\n";
  return testing::failure_count() == 0 ? 0 : 1;
}
