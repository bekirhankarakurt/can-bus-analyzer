#pragma once

#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// Minimal dependency-free unit test harness: TEST(suite, name) { CHECK(...); }
namespace testing {

struct Case {
  std::string suite;
  std::string name;
  std::function<void()> body;
};

inline std::vector<Case>& registry() {
  static std::vector<Case> cases;
  return cases;
}

inline int& failure_count() {
  static int failures = 0;
  return failures;
}

struct Registrar {
  Registrar(const char* suite, const char* name, std::function<void()> body) {
    registry().push_back({suite, name, std::move(body)});
  }
};

inline void fail(const char* file, int line, const std::string& what) {
  ++failure_count();
  std::cerr << file << ':' << line << ": check failed: " << what << '\n';
}

template <typename T>
std::string str(const T& value) {
  std::ostringstream os;
  os << value;
  return os.str();
}

}  // namespace testing

#define TEST(suite, name)                                                         \
  static void suite##_##name();                                                   \
  static const ::testing::Registrar registrar_##suite##_##name(#suite, #name,     \
                                                               suite##_##name);   \
  static void suite##_##name()

#define CHECK(cond)                                           \
  do {                                                        \
    if (!(cond)) ::testing::fail(__FILE__, __LINE__, #cond);  \
  } while (0)

#define CHECK_EQ(a, b)                                                              \
  do {                                                                              \
    const auto& check_a = (a);                                                      \
    const auto& check_b = (b);                                                      \
    if (!(check_a == check_b))                                                      \
      ::testing::fail(__FILE__, __LINE__,                                           \
                      std::string(#a " == " #b " (") + ::testing::str(check_a) +    \
                          " vs " + ::testing::str(check_b) + ")");                  \
  } while (0)

#define CHECK_NEAR(a, b, eps)                                                       \
  do {                                                                              \
    const double check_a = (a);                                                     \
    const double check_b = (b);                                                     \
    if (!(std::fabs(check_a - check_b) <= (eps)))                                   \
      ::testing::fail(__FILE__, __LINE__,                                           \
                      std::string(#a " ~= " #b " (") + ::testing::str(check_a) +    \
                          " vs " + ::testing::str(check_b) + ")");                  \
  } while (0)

#define CHECK_THROWS(expr)                                                \
  do {                                                                    \
    bool check_threw = false;                                             \
    try {                                                                 \
      (void)(expr);                                                       \
    } catch (const std::exception&) {                                     \
      check_threw = true;                                                 \
    }                                                                     \
    if (!check_threw) ::testing::fail(__FILE__, __LINE__, #expr " did not throw"); \
  } while (0)
