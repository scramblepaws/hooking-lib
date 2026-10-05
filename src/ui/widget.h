#pragma once

#include <functional>
#include <string>
#include <variant>
#include <vector>

#include "ui/canvas.h"

namespace overlay::ui {

enum class WidgetClass {
  Toggle,
  Slider,
  Dropdown,
  Checkbox,
  Keybind,
  TextInput,
  ColorPicker,
  Button,
};

// 0 = none, >0 VK code, <0 mouse button (-1 left, -2 right, -3 middle, -4/-5 extra).
struct KeyCombo {
  int code = 0;
};

using WidgetValue = std::variant<bool, float, int, std::string, Color, KeyCombo>;

struct Widget;

struct WidgetState {
  bool enabled = true;
  bool hovered = false;
  bool held = false;
  bool open = false;
  bool focused = false;
  bool capturing = false;
  bool settings_open = false;
  int hovered_item = -1;
  int hovered_swatch = -1;
  int armed_frame = 0;
  float anim = 0.0f;
  float blink = 0.0f;
  float hue = 0.0f;
  float sv_s = 1.0f;
  float sv_v = 1.0f;
  bool drag_sv = false;
  bool drag_hue = false;
  bool clicked = false;
  Rect popup;
};

// Draws one Widget into a Canvas; replaces the default painter for its class.
using Painter = std::function<void(const Widget&, Canvas&)>;

struct Widget {
  std::string id;
  WidgetClass klass = WidgetClass::Toggle;
  std::string label;
  WidgetValue value = false;
  float min_value = 0.0f;
  float max_value = 1.0f;
  std::string format = "%.0f";
  std::string alt_text;
  std::vector<std::string> items;

  bool show_label = true;
  bool show_gear = true;
  float control_width = 0.0f;

  // Per-widget sub-settings (the gear popup): accent override and a key that
  // toggles/activates the widget.
  bool has_accent = false;
  Color accent_override{};
  int bind_key = 0;

  Painter painter;
  void* c_painter_user = nullptr;
  void (*c_painter)(void* user, const void* widget, void* canvas) = nullptr;
  WidgetState state;
  Rect rect;
};

struct Input {
  Vec2 mouse;
  bool mouse_clicked = false;
  bool mouse_down = false;
  bool click_consumed = false;
  float dt = 0.0f;
  float scale = 1.0f;
  int frame = 0;
  float time = 0.0f;
  int captured_key = 0;
  unsigned int chars[32] = {};
  int char_count = 0;
  Widget* focus_target = nullptr;
  Rect bounds;
};

Rect widget_control_rect(const Widget& widget, float scale);
void widget_update(Widget& widget, Input& input);
void widget_paint(const Widget& widget, Canvas& canvas);
void widget_paint_popup(const Widget& widget, Canvas& canvas);

Widget make_toggle(const char* id, const char* label, bool value);
Widget make_checkbox(const char* id, const char* label, bool value);
Widget make_slider(const char* id, const char* label, float value, float min, float max, const char* format,
                   const char* alt_text = nullptr);
Widget make_dropdown(const char* id, const char* label, int index, std::vector<std::string> items);
Widget make_keybind(const char* id, const char* label, int code);
Widget make_textinput(const char* id, const char* label, const char* value);
Widget make_colorpicker(const char* id, const char* label, Color color);
Widget make_button(const char* id, const char* label);

bool widget_bool(const Widget& widget);
float widget_float(const Widget& widget);
int widget_int(const Widget& widget);
const std::string& widget_string(const Widget& widget);
Color widget_color(const Widget& widget);
const KeyCombo& widget_key(const Widget& widget);

// Returns the format string only if it is a single numeric conversion
// (%[flags][width][.precision]f); anything else falls back to "%.0f".
std::string sanitize_number_format(const char* format);

void key_name(int code, char* out, size_t capacity);
const char* widget_class_name(WidgetClass klass);

}  // namespace overlay::ui
