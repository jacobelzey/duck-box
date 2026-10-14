// A very small test harness: no framework, just g++.
//
//   TEST(name) { CHECK(cond); CHECK_EQ(a, b); }
//
// Every TEST registers itself; test_main.cpp runs them all and prints a
// pass / fail summary. The process exit code is the number of failed tests
// (0 = all good).
#pragma once

#include <cstdio>
#include <sstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace mini_test {

struct TestCase {
  std::string name;
  std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
  static std::vector<TestCase> tests;
  return tests;
}

inline int& failuresInCurrentTest() {
  static int n = 0;
  return n;
}

inline long& checksRun() {
  static long n = 0;
  return n;
}

struct Registrar {
  Registrar(const char* name, std::function<void()> fn) {
    registry().push_back({name, std::move(fn)});
  }
};

inline void reportFailure(const char* file, int line, const std::string& what) {
  ++failuresInCurrentTest();
  std::printf("    FAILED %s:%d: %s\n", file, line, what.c_str());
}

template <typename A, typename B>
void checkEq(const A& a, const B& b, const char* ea, const char* eb,
             const char* file, int line) {
  ++checksRun();
  if (!(a == b)) {
    std::ostringstream os;
    os << ea << " == " << eb << "  (got " << a << " vs " << b << ")";
    reportFailure(file, line, os.str());
  }
}

}  // namespace mini_test

#define TEST(name)                                                        \
  static void test_##name();                                              \
  static mini_test::Registrar registrar_##name(#name, test_##name);       \
  static void test_##name()

#define CHECK(cond)                                                       \
  do {                                                                    \
    ++mini_test::checksRun();                                             \
    if (!(cond)) mini_test::reportFailure(__FILE__, __LINE__, #cond);     \
  } while (0)

#define CHECK_EQ(a, b) \
  mini_test::checkEq((a), (b), #a, #b, __FILE__, __LINE__)
