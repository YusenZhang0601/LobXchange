#pragma once

#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace lobx_test {

struct TestCase {
  std::string suite;
  std::string name;
  void (*fn)();
};

inline std::vector<TestCase>& registry() {
  static std::vector<TestCase> tests;
  return tests;
}

struct Registrar {
  Registrar(const char* suite, const char* name, void (*fn)()) {
    registry().push_back(TestCase{suite, name, fn});
  }
};

inline std::string location(const char* file, int line) {
  std::ostringstream os;
  os << file << ':' << line;
  return os.str();
}

inline void fail(const char* file, int line, const std::string& message) {
  throw std::runtime_error(location(file, line) + " " + message);
}

template <typename T>
inline std::string value_to_string(const T& value) {
  if constexpr (std::is_enum_v<T>) {
    return std::to_string(static_cast<int>(value));
  } else {
    std::ostringstream os;
    os << value;
    return os.str();
  }
}

template <typename A, typename B>
inline void expect_eq(const A& actual, const B& expected, const char* actual_expr, const char* expected_expr,
                      const char* file, int line, const std::string& detail = {}) {
  if (actual == expected) return;
  std::ostringstream os;
  os << "expected " << actual_expr << " == " << expected_expr
     << " actual=" << value_to_string(actual)
     << " expected=" << value_to_string(expected);
  if (!detail.empty()) os << " -- " << detail;
  fail(file, line, os.str());
}

template <typename A, typename B>
inline void expect_ne(const A& actual, const B& expected, const char* actual_expr, const char* expected_expr,
                      const char* file, int line, const std::string& detail = {}) {
  if (actual != expected) return;
  std::ostringstream os;
  os << "expected " << actual_expr << " != " << expected_expr << " both=" << value_to_string(actual);
  if (!detail.empty()) os << " -- " << detail;
  fail(file, line, os.str());
}

inline void expect_true(bool condition, const char* expr, const char* file, int line, const std::string& detail = {}) {
  if (condition) return;
  std::ostringstream os;
  os << "expected true: " << expr;
  if (!detail.empty()) os << " -- " << detail;
  fail(file, line, os.str());
}

inline void expect_false(bool condition, const char* expr, const char* file, int line, const std::string& detail = {}) {
  if (!condition) return;
  std::ostringstream os;
  os << "expected false: " << expr;
  if (!detail.empty()) os << " -- " << detail;
  fail(file, line, os.str());
}

inline int run_all_tests(const std::string& filter = {}) {
  int failed = 0;
  int ran = 0;
  for (const TestCase& test : registry()) {
    const std::string full_name = test.suite + "." + test.name;
    if (!filter.empty() && full_name.find(filter) == std::string::npos) continue;
    ++ran;
    try {
      test.fn();
      std::cout << "[PASS] " << full_name << "\n";
    } catch (const std::exception& e) {
      ++failed;
      std::cout << "[FAIL] " << full_name << " -- " << e.what() << "\n";
    } catch (...) {
      ++failed;
      std::cout << "[FAIL] " << full_name << " -- unknown exception\n";
    }
  }
  std::cout << "ran=" << ran << " failed=" << failed << "\n";
  return failed == 0 ? 0 : 1;
}

} // namespace lobx_test

#define LOBX_TEST_JOIN_IMPL(a, b) a##b
#define LOBX_TEST_JOIN(a, b) LOBX_TEST_JOIN_IMPL(a, b)

#define TEST(suite, name) \
  static void LOBX_TEST_JOIN(test_, LOBX_TEST_JOIN(suite, LOBX_TEST_JOIN(_, name)))(); \
  static ::lobx_test::Registrar LOBX_TEST_JOIN(reg_, LOBX_TEST_JOIN(suite, LOBX_TEST_JOIN(_, name)))(#suite, #name, LOBX_TEST_JOIN(test_, LOBX_TEST_JOIN(suite, LOBX_TEST_JOIN(_, name)))); \
  static void LOBX_TEST_JOIN(test_, LOBX_TEST_JOIN(suite, LOBX_TEST_JOIN(_, name)))()

#define EXPECT_TRUE(expr) ::lobx_test::expect_true(static_cast<bool>(expr), #expr, __FILE__, __LINE__)
#define EXPECT_TRUE_MSG(expr, msg) ::lobx_test::expect_true(static_cast<bool>(expr), #expr, __FILE__, __LINE__, (msg))
#define EXPECT_FALSE(expr) ::lobx_test::expect_false(static_cast<bool>(expr), #expr, __FILE__, __LINE__)
#define EXPECT_FALSE_MSG(expr, msg) ::lobx_test::expect_false(static_cast<bool>(expr), #expr, __FILE__, __LINE__, (msg))
#define EXPECT_EQ(actual, expected) ::lobx_test::expect_eq((actual), (expected), #actual, #expected, __FILE__, __LINE__)
#define EXPECT_EQ_MSG(actual, expected, msg) ::lobx_test::expect_eq((actual), (expected), #actual, #expected, __FILE__, __LINE__, (msg))
#define EXPECT_NE(actual, expected) ::lobx_test::expect_ne((actual), (expected), #actual, #expected, __FILE__, __LINE__)
#define EXPECT_NE_MSG(actual, expected, msg) ::lobx_test::expect_ne((actual), (expected), #actual, #expected, __FILE__, __LINE__, (msg))
#define FAIL_TEST(msg) ::lobx_test::fail(__FILE__, __LINE__, (msg))
