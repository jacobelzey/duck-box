// Runs every TEST() in the native test files. See README, "Tests".
#include "mini_test.h"

int main() {
  int failedTests = 0;
  int passedTests = 0;
  for (const auto& t : mini_test::registry()) {
    mini_test::failuresInCurrentTest() = 0;
    t.fn();
    if (mini_test::failuresInCurrentTest() == 0) {
      ++passedTests;
      std::printf("[ ok ] %s\n", t.name.c_str());
    } else {
      ++failedTests;
      std::printf("[FAIL] %s\n", t.name.c_str());
    }
  }
  std::printf("\n%d tests: %d passed, %d failed (%ld checks)\n",
              passedTests + failedTests, passedTests, failedTests,
              mini_test::checksRun());
  return failedTests;
}
