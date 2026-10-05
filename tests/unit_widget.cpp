#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "ui/widget.h"

namespace {

int g_failures = 0;

void expect_key(int code, const char* expected) {
  char buffer[32] = {};
  overlay::ui::key_name(code, buffer, sizeof(buffer));
  if (std::strcmp(buffer, expected) != 0) {
    std::printf("FAIL key_name(%d): got '%s', expected '%s'\n", code, buffer, expected);
    ++g_failures;
  }
}

void expect_format(const char* input, const char* expected) {
  const std::string result = overlay::ui::sanitize_number_format(input);
  if (result != expected) {
    std::printf("FAIL format('%s'): got '%s', expected '%s'\n", input ? input : "(null)", result.c_str(), expected);
    ++g_failures;
  }
}

}  // namespace

int main() {
  expect_key(0, "None");
  expect_key('A', "A");
  expect_key('9', "9");
  expect_key(-1, "Mouse1");
  expect_key(-5, "Mouse5");
  expect_key(VK_F1, "F1");
  expect_key(VK_F12, "F12");
  expect_key(VK_SPACE, "Space");
  expect_key(VK_ESCAPE, "Esc");
  expect_key(0x99, "0x99");

  expect_format("%.1f", "%.1f");
  expect_format("%.2f", "%.2f");
  expect_format("%5.1f", "%5.1f");
  expect_format("%08.1f", "%08.1f");
  expect_format("%f", "%f");
  expect_format("%.1F", "%.1F");
  expect_format("%.0f°", "%.0f°");
  expect_format("%.0f%%", "%.0f%%");
  expect_format("score %.1f", "score %.1f");
  expect_format("%.1f points", "%.1f points");
  expect_format("%s", "%.0f");
  expect_format("%d", "%.0f");
  expect_format("%n", "%.0f");
  expect_format("%.1f%s", "%.0f");
  expect_format("%.1f %.1f", "%.0f");
  expect_format("%*f", "%.0f");
  expect_format("100%", "%.0f");
  expect_format(nullptr, "%.0f");
  expect_format("", "%.0f");

  if (g_failures == 0) {
    std::printf("unit_widget: all checks passed\n");
    return 0;
  }
  std::printf("unit_widget: %d failure(s)\n", g_failures);
  return 1;
}
