#include <windows.h>

#include <cstdio>
#include <cstring>

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

  if (g_failures == 0) {
    std::printf("unit_widget: all checks passed\n");
    return 0;
  }
  std::printf("unit_widget: %d failure(s)\n", g_failures);
  return 1;
}
