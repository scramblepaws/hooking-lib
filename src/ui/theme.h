#pragma once

#include <map>
#include <string>

#include "ui/canvas.h"
#include "ui/widget.h"

namespace overlay::ui {

struct Theme {
  Color accent{0.70f, 0.21f, 0.21f, 1.0f};
  Color track{0.149f, 0.149f, 0.165f, 1.0f};
  Color knob{0.91f, 0.91f, 0.91f, 1.0f};
  Color text{0.91f, 0.91f, 0.91f, 1.0f};
  Color text_dim{0.55f, 0.55f, 0.57f, 1.0f};
  Color panel{0.090f, 0.090f, 0.102f, 1.0f};
  Color border{0.164f, 0.164f, 0.180f, 1.0f};
  Color window_bg{0.074f, 0.074f, 0.080f, 1.0f};
  Color row_hover{0.118f, 0.118f, 0.133f, 1.0f};
  Color nav_selected{0.125f, 0.125f, 0.145f, 1.0f};
  Color button{0.102f, 0.102f, 0.114f, 1.0f};
  Color button_hover{0.129f, 0.129f, 0.141f, 1.0f};
  Color field{0.075f, 0.075f, 0.080f, 1.0f};
  float rounding = 3.0f;
};

Theme& theme();

// Global override from `menu { ... }`, else the built-in fallback.
Color theme_color(const char* property, Color fallback);
float theme_rounding(float fallback);

// Per-widget override: #id beats class beats the global theme.
Color style_color(const Widget& widget, const char* property, Color fallback);
float style_rounding(const Widget& widget, float fallback);

// CSS subset: `menu { ... }`, `class { ... }`, `#id { ... }`.
// Properties: accent, track, knob, text, text-dim, panel, border, window-bg,
// row-hover, nav-selected, button, button-hover, field, rounding.
void style_clear();
bool style_parse(const char* css);
bool style_load_file(const std::wstring& path);

}  // namespace overlay::ui
