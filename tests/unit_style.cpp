#include <cmath>
#include <cstdio>

#include "ui/theme.h"
#include "ui/widget.h"

namespace {

int g_failures = 0;

void check(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    ++g_failures;
  }
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

bool same(const overlay::ui::Color& a, const overlay::ui::Color& b) {
  return near(a.r, b.r) && near(a.g, b.g) && near(a.b, b.b) && near(a.a, b.a);
}

}  // namespace

int main() {
  using namespace overlay::ui;

  style_clear();
  check(same(theme_color("accent", theme().accent), theme().accent), "defaults without stylesheet");

  const char* css =
      "menu { accent: #00ff00; rounding: 5; }\n"
      "toggle { track: #111; }\n"
      "#enabled { knob: rgb(255, 0, 0); }\n"
      "slider { accent: rgba(0, 0, 255, 0.5); }\n";
  check(style_parse(css), "stylesheet parses");

  Widget toggle = make_toggle("some", "Label", false);
  Widget enabled = make_toggle("enabled", "Label", false);
  Widget slider = make_slider("s", "L", 0.0f, 0.0f, 1.0f, "%.0f");

  check(near(style_color(toggle, "accent", theme().accent).g, 1.0f), "menu accent reaches widgets");
  check(near(style_color(toggle, "track", theme().track).r, 17.0f / 255.0f), "class override applies");
  check(near(style_color(enabled, "knob", theme().knob).r, 1.0f), "id override applies");
  check(near(style_rounding(toggle, 1.0f), 5.0f), "global rounding applies");

  const Color slider_accent = style_color(slider, "accent", theme().accent);
  check(near(slider_accent.b, 1.0f) && near(slider_accent.a, 0.5f), "rgba override applies");

  check(style_parse("toggle { knob: #ff0000; } #enabled { knob: #0000ff; }"), "precedence stylesheet parses");
  check(near(style_color(enabled, "knob", theme().knob).b, 1.0f), "id beats class");

  check(!style_parse("toggle { accent: #fff;"), "unterminated rule rejected");

  style_clear();
  check(style_parse("toggle { accent: notacolor; }"), "invalid color tolerated");
  check(near(style_color(toggle, "accent", theme().accent).r, theme().accent.r), "invalid color falls back");

  style_clear();
  check(same(theme_color("accent", theme().accent), theme().accent), "clear resets overrides");

  if (g_failures == 0) {
    std::printf("unit_style: all checks passed\n");
    return 0;
  }
  std::printf("unit_style: %d failure(s)\n", g_failures);
  return 1;
}
