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

void check(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL %s\n", what);
    ++g_failures;
  }
}

void test_gear_settings() {
  overlay::ui::Widget toggle = overlay::ui::make_toggle("gearbind", "Bound", false);
  toggle.rect = {50.0f, 0.0f, 200.0f, 24.0f};

  overlay::ui::Input open_input;
  open_input.scale = 1.0f;
  open_input.mouse = {185.0f, 12.0f};
  open_input.mouse_clicked = true;
  overlay::ui::widget_update(toggle, open_input);
  check(toggle.state.settings_open, "gear opens widget settings");

  overlay::ui::Input close_input;
  close_input.scale = 1.0f;
  close_input.mouse = {400.0f, 400.0f};
  close_input.mouse_clicked = true;
  overlay::ui::widget_update(toggle, close_input);
  check(!toggle.state.settings_open, "clicking away closes widget settings");

  toggle.bind_key = 'X';
  overlay::ui::Input key_input;
  key_input.scale = 1.0f;
  key_input.captured_key = 'X';
  overlay::ui::widget_update(toggle, key_input);
  check(overlay::ui::widget_bool(toggle), "bind key toggles the widget");
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

  test_gear_settings();

  if (g_failures == 0) {
    std::printf("unit_widget: all checks passed\n");
    return 0;
  }
  std::printf("unit_widget: %d failure(s)\n", g_failures);
  return 1;
}
