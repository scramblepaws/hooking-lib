#include <cstdio>

#include "core/salvage.h"

namespace {

int g_failures = 0;

void check(bool condition, const char* what) {
  std::printf("%s %s\n", condition ? "ok  " : "FAIL", what);
  if (!condition) ++g_failures;
}

void quiet(void*) {}

void null_write(void*) { *static_cast<volatile int*>(nullptr) = 1; }

// Namespace-scope volatile: the compiler cannot prove its value, so the
// division is actually emitted and traps at runtime.
volatile int g_divisor = 0;

void divide_by_zero(void*) {
  volatile int result = 7 / g_divisor;
  (void)result;
}

void throw_cpp(void*) { throw 42; }

}  // namespace

int main() {
  overlay::salvage::install();
  check(overlay::salvage::run(quiet, nullptr), "clean call returns true");
  check(!overlay::salvage::run(null_write, nullptr), "null write contained");
  check(!overlay::salvage::run(divide_by_zero, nullptr), "divide by zero contained");
  check(!overlay::salvage::run(throw_cpp, nullptr), "c++ exception contained");
  check(overlay::salvage::run(quiet, nullptr), "usable again after containment");
  overlay::salvage::remove();

  std::printf(g_failures == 0 ? "unit_salvage: all checks passed\n" : "unit_salvage: FAILED\n");
  return g_failures == 0 ? 0 : 1;
}
