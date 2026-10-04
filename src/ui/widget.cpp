#include "ui/widget.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "ui/palette.h"
#include "ui/theme.h"

namespace overlay::ui {

namespace {

constexpr const char* kIconGear = u8"\uf013";

constexpr float kRowHeight = 24.0f;
constexpr float kToggleWidth = 42.0f;
constexpr float kCheckboxSize = 18.0f;
constexpr float kSliderWidth = 170.0f;
constexpr float kSliderBoxWidth = 52.0f;
constexpr float kSliderGap = 10.0f;
constexpr float kDropdownWidth = 150.0f;
constexpr float kKeybindWidth = 90.0f;
constexpr float kTextInputWidth = 150.0f;
constexpr float kSwatchWidth = 42.0f;
constexpr float kButtonWidth = 120.0f;
constexpr float kGearGap = 14.0f;
constexpr float kPopupItemHeight = 30.0f;
constexpr float kPopupPad = 6.0f;
constexpr float kPickerSize = 180.0f;
constexpr float kPickerHueHeight = 14.0f;
constexpr float kPickerPad = 8.0f;
constexpr int kPickerCells = 24;
constexpr int kPickerHueCells = 36;
constexpr size_t kTextLimit = 48;

float clamp01(float value) { return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); }

float control_width(const Widget& widget, float scale) {
  if (widget.control_width > 0.0f) return widget.control_width * scale;
  switch (widget.klass) {
    case WidgetClass::Toggle: return kToggleWidth * scale;
    case WidgetClass::Checkbox: return kCheckboxSize * scale;
    case WidgetClass::Slider: return kSliderWidth * scale;
    case WidgetClass::Dropdown: return kDropdownWidth * scale;
    case WidgetClass::Keybind: return kKeybindWidth * scale;
    case WidgetClass::TextInput: return kTextInputWidth * scale;
    case WidgetClass::ColorPicker: return kSwatchWidth * scale;
    case WidgetClass::Button: return kButtonWidth * scale;
  }
  return 0.0f;
}

Rect control_rect(const Widget& widget, float scale) {
  const float height = kRowHeight * scale;
  const float width = control_width(widget, scale);
  return {widget.rect.right() - width, widget.rect.y + (widget.rect.h - height) * 0.5f, width, height};
}

void paint_row_chrome(const Widget& widget, Canvas& canvas, const Rect& control) {
  const float scale = canvas.scale();
  if (widget.show_label && !widget.label.empty()) {
    const Color text = style_color(widget, "text", theme().text);
    const Vec2 size = canvas.measure_text(widget.label.c_str());
    canvas.text({widget.rect.x, widget.rect.y + (widget.rect.h - size.y) * 0.5f}, text, widget.label.c_str());
  }
  if (widget.show_gear) {
    const Color dim = style_color(widget, "text-dim", theme().text_dim);
    const Vec2 size = canvas.measure_text(kIconGear);
    canvas.text({control.x - kGearGap * scale - size.x, widget.rect.y + (widget.rect.h - size.y) * 0.5f}, dim, kIconGear);
  }
}

Color hsv_to_rgb(float h, float s, float v) {
  const float sector = h * 6.0f;
  const int index = static_cast<int>(sector) % 6;
  const float f = sector - std::floor(sector);
  const float p = v * (1.0f - s);
  const float q = v * (1.0f - s * f);
  const float t = v * (1.0f - s * (1.0f - f));
  switch (index) {
    case 0: return {v, t, p, 1.0f};
    case 1: return {q, v, p, 1.0f};
    case 2: return {p, v, t, 1.0f};
    case 3: return {p, q, v, 1.0f};
    case 4: return {t, p, v, 1.0f};
    default: return {v, p, q, 1.0f};
  }
}

void rgb_to_hsv(const Color& color, float& h, float& s, float& v) {
  const float max = color.r > color.g ? (color.r > color.b ? color.r : color.b) : (color.g > color.b ? color.g : color.b);
  const float min = color.r < color.g ? (color.r < color.b ? color.r : color.b) : (color.g < color.b ? color.g : color.b);
  v = max;
  const float delta = max - min;
  s = max <= 0.0f ? 0.0f : delta / max;
  if (delta <= 0.0f) {
    h = 0.0f;
    return;
  }
  if (max == color.r)
    h = std::fmod((color.g - color.b) / delta + 6.0f, 6.0f);
  else if (max == color.g)
    h = (color.b - color.r) / delta + 2.0f;
  else
    h = (color.r - color.g) / delta + 4.0f;
  h /= 6.0f;
}

void paint_toggle(const Widget& widget, Canvas& canvas) {
  const Rect control = control_rect(widget, canvas.scale());
  paint_row_chrome(widget, canvas, control);
  const float radius = control.h * 0.5f;
  const Color track = style_color(widget, "track", theme().track);
  const Color accent = style_color(widget, "accent", theme().accent);
  const Color knob = style_color(widget, "knob", theme().knob);
  canvas.fill_rect(control, lerp(track, accent, widget.state.anim), radius);
  canvas.fill_circle({control.x + radius + widget.state.anim * (control.w - 2.0f * radius), control.y + radius},
                     radius - 3.5f * canvas.scale(), knob);
}

void paint_checkbox(const Widget& widget, Canvas& canvas) {
  const float scale = canvas.scale();
  const Rect control = control_rect(widget, scale);
  paint_row_chrome(widget, canvas, control);

  const float rounding = style_rounding(widget, theme().rounding) * scale;
  const Color track = style_color(widget, "track", theme().track);
  const Color accent = style_color(widget, "accent", theme().accent);
  const Color border = style_color(widget, "border", theme().border);
  const float size = kCheckboxSize * scale;
  const Rect box{control.right() - size, control.y + (control.h - size) * 0.5f, size, size};
  canvas.fill_rect(box, track, rounding);
  canvas.stroke_rect(box, widget.state.anim > 0.5f ? accent : border, 1.0f, rounding);
  if (widget.state.anim > 0.05f) {
    canvas.fill_rect({box.x + 3.0f * scale, box.y + 3.0f * scale, box.w - 6.0f * scale, box.h - 6.0f * scale},
                     lerp(track, accent, widget.state.anim), rounding);
  }
}

void paint_slider(const Widget& widget, Canvas& canvas) {
  const float scale = canvas.scale();
  const Rect control = control_rect(widget, scale);
  paint_row_chrome(widget, canvas, control);

  const float rounding = style_rounding(widget, theme().rounding) * scale;
  const Color track = style_color(widget, "track", theme().track);
  const Color accent = style_color(widget, "accent", theme().accent);
  const Color text = style_color(widget, "text", theme().text);
  const float box_width = kSliderBoxWidth * scale;
  const float gap = kSliderGap * scale;
  const float track_width = control.w - box_width - gap;
  const float value = widget_float(widget);
  const float t = clamp01((value - widget.min_value) / (widget.max_value - widget.min_value));
  const float mid = control.y + control.h * 0.5f;
  const float half = 2.5f * scale;

  canvas.fill_rect({control.x, mid - half, track_width, half * 2.0f}, track, half);
  if (t > 0.0f) canvas.fill_rect({control.x, mid - half, track_width * t, half * 2.0f}, accent, half);
  canvas.fill_circle({control.x + track_width * t, mid}, 7.0f * scale, accent);

  char text_buffer[32];
  if (widget.alt_text && value <= widget.min_value + 0.5f)
    std::snprintf(text_buffer, sizeof(text_buffer), "%s", widget.alt_text);
  else
    std::snprintf(text_buffer, sizeof(text_buffer), widget.format, value);

  const Rect box{control.x + track_width + gap, control.y + 1.0f * scale, box_width, 22.0f * scale};
  canvas.fill_rect(box, track, rounding);
  canvas.text_centered(box, text, text_buffer);
}

void paint_dropdown(const Widget& widget, Canvas& canvas) {
  const float scale = canvas.scale();
  const Rect control = control_rect(widget, scale);
  paint_row_chrome(widget, canvas, control);

  const float rounding = style_rounding(widget, theme().rounding) * scale;
  const Color field = style_color(widget, "field", theme().field);
  const Color border = style_color(widget, "border", theme().border);
  const Color text = style_color(widget, "text", theme().text);
  const Color dim = style_color(widget, "text-dim", theme().text_dim);
  canvas.fill_rect(control, field, rounding);
  canvas.stroke_rect(control, border, 1.0f, rounding);

  const int index = widget_int(widget);
  const char* preview = index >= 0 && index < static_cast<int>(widget.items.size()) ? widget.items[index].c_str() : "";
  canvas.push_clip({control.x, control.y, control.w - 24.0f * scale, control.h});
  const Vec2 size = canvas.measure_text(preview);
  canvas.text({control.x + 10.0f * scale, control.y + (control.h - size.y) * 0.5f}, text, preview);
  canvas.pop_clip();

  const float arrow_x = control.right() - 16.0f * scale;
  const float arrow_y = control.y + control.h * 0.5f;
  const float half = 4.0f * scale;
  canvas.fill_triangle({arrow_x - half, arrow_y - half * 0.5f}, {arrow_x + half, arrow_y - half * 0.5f},
                       {arrow_x, arrow_y + half}, dim);
}

void paint_keybind(const Widget& widget, Canvas& canvas) {
  const float scale = canvas.scale();
  const Rect control = control_rect(widget, scale);
  paint_row_chrome(widget, canvas, control);

  const float rounding = style_rounding(widget, theme().rounding) * scale;
  const Color field = style_color(widget, "field", theme().field);
  const Color border = style_color(widget, "border", theme().border);
  const Color accent = style_color(widget, "accent", theme().accent);
  const Color text = style_color(widget, "text", theme().text);
  const Color dim = style_color(widget, "text-dim", theme().text_dim);
  canvas.fill_rect(control, field, rounding);
  canvas.stroke_rect(control, widget.state.capturing ? accent : border, 1.0f, rounding);

  char name[32];
  const char* label = nullptr;
  if (widget.state.capturing) {
    label = "...";
  } else {
    key_name(widget_key(widget).code, name, sizeof(name));
    label = name;
  }
  canvas.text_centered(control, widget.state.capturing ? dim : text, label);
}

void paint_textinput(const Widget& widget, Canvas& canvas) {
  const float scale = canvas.scale();
  const Rect control = control_rect(widget, scale);
  paint_row_chrome(widget, canvas, control);

  const float rounding = style_rounding(widget, theme().rounding) * scale;
  const Color field = style_color(widget, "field", theme().field);
  const Color border = style_color(widget, "border", theme().border);
  const Color accent = style_color(widget, "accent", theme().accent);
  const Color text = style_color(widget, "text", theme().text);
  canvas.fill_rect(control, field, rounding);
  canvas.stroke_rect(control, widget.state.focused ? accent : border, 1.0f, rounding);

  const std::string& value = widget_string(widget);
  canvas.push_clip(control);
  const Vec2 size = canvas.measure_text(value.c_str());
  canvas.text({control.x + 10.0f * scale, control.y + (control.h - size.y) * 0.5f}, text, value.c_str());
  if (widget.state.focused && std::fmod(widget.state.blink, 1.0f) < 0.55f) {
    const float caret_x = control.x + 10.0f * scale + size.x + 1.0f * scale;
    canvas.fill_rect({caret_x, control.y + 4.0f * scale, 1.5f * scale, control.h - 8.0f * scale}, text);
  }
  canvas.pop_clip();
}

void paint_colorpicker(const Widget& widget, Canvas& canvas) {
  const float scale = canvas.scale();
  const Rect control = control_rect(widget, scale);
  paint_row_chrome(widget, canvas, control);
  const Color border = style_color(widget, "border", theme().border);
  canvas.fill_rect(control, widget_color(widget), style_rounding(widget, theme().rounding) * scale);
  canvas.stroke_rect(control, border, 1.0f, style_rounding(widget, theme().rounding) * scale);
}

void paint_button(const Widget& widget, Canvas& canvas) {
  const float scale = canvas.scale();
  const Rect control = control_rect(widget, scale);
  const float rounding = style_rounding(widget, theme().rounding) * scale;
  const Color button = style_color(widget, "button", theme().button);
  const Color hover = style_color(widget, "button-hover", theme().button_hover);
  const Color border = style_color(widget, "border", theme().border);
  const Color text = style_color(widget, "text", theme().text);
  canvas.fill_rect(control, widget.state.hovered || widget.state.held ? hover : button, rounding);
  canvas.stroke_rect(control, border, 1.0f, rounding);
  canvas.text_centered(control, text, widget.label.c_str());
}

void picker_geometry(const Rect& popup, float scale, Rect& sv, Rect& hue) {
  const float pad = kPickerPad * scale;
  const float size = kPickerSize * scale;
  sv = {popup.x + pad, popup.y + pad, size, size};
  hue = {popup.x + pad, popup.y + pad + size + pad, size, kPickerHueHeight * scale};
}

void update_toggle(Widget& widget, Input& input) {
  bool& value = std::get<bool>(widget.value);
  const Rect control = control_rect(widget, input.scale);
  widget.state.hovered = control.contains(input.mouse);
  if (widget.state.hovered && input.mouse_clicked && !input.click_consumed) {
    value = !value;
    input.click_consumed = true;
  }
  const float target = value ? 1.0f : 0.0f;
  const float speed = input.dt * 16.0f;
  widget.state.anim += (target - widget.state.anim) * (speed < 1.0f ? speed : 1.0f);
}

void update_slider(Widget& widget, Input& input) {
  float& value = std::get<float>(widget.value);
  const Rect control = control_rect(widget, input.scale);
  const float box_width = kSliderBoxWidth * input.scale;
  const float gap = kSliderGap * input.scale;
  const Rect track{control.x, control.y, control.w - box_width - gap, control.h};
  widget.state.hovered = track.contains(input.mouse);

  if (widget.state.hovered && input.mouse_clicked && !input.click_consumed) {
    widget.state.held = true;
    input.click_consumed = true;
  }
  if (widget.state.held) {
    if (!input.mouse_down) {
      widget.state.held = false;
    } else {
      const float t = clamp01((input.mouse.x - track.x) / track.w);
      value = widget.min_value + t * (widget.max_value - widget.min_value);
    }
  }
}

float popup_y_below_or_above(const Rect& control, float popup_height, float scale, const Rect& bounds) {
  const float below = control.bottom() + 4.0f * scale;
  if (below + popup_height <= bounds.bottom()) return below;
  return control.y - popup_height - 4.0f * scale;
}

void update_dropdown(Widget& widget, Input& input) {
  int& index = std::get<int>(widget.value);
  const float scale = input.scale;
  const Rect control = control_rect(widget, scale);

  if (!widget.state.open) {
    widget.state.hovered = control.contains(input.mouse);
    if (widget.state.hovered && input.mouse_clicked && !input.click_consumed) {
      widget.state.open = true;
      input.click_consumed = true;
    }
    return;
  }

  const float item_height = kPopupItemHeight * scale;
  const float pad = kPopupPad * scale;
  const float popup_height = pad * 2.0f + item_height * static_cast<float>(widget.items.size());
  widget.state.popup = {control.x, popup_y_below_or_above(control, popup_height, scale, input.bounds), control.w,
                        popup_height};

  widget.state.hovered_item = -1;
  if (widget.state.popup.contains(input.mouse)) {
    const float local = input.mouse.y - widget.state.popup.y - pad;
    const int item = static_cast<int>(local / item_height);
    if (item >= 0 && item < static_cast<int>(widget.items.size())) widget.state.hovered_item = item;
  }

  if (input.mouse_clicked) {
    if (widget.state.hovered_item >= 0) index = widget.state.hovered_item;
    widget.state.open = false;
    input.click_consumed = true;
  }
}

void update_keybind(Widget& widget, Input& input) {
  const Rect control = control_rect(widget, input.scale);
  widget.state.hovered = control.contains(input.mouse);

  if (widget.state.hovered && input.mouse_clicked && !input.click_consumed) {
    widget.state.capturing = !widget.state.capturing;
    widget.state.armed_frame = input.frame;
    input.click_consumed = true;
  }

  if (widget.state.capturing) {
    input.click_consumed = true;
    if (input.frame > widget.state.armed_frame && input.captured_key != 0) {
      KeyCombo& key = std::get<KeyCombo>(widget.value);
      key.code = input.captured_key == VK_ESCAPE ? 0 : input.captured_key;
      widget.state.capturing = false;
      input.captured_key = 0;
    }
  }
}

void update_textinput(Widget& widget, Input& input) {
  const Rect control = control_rect(widget, input.scale);
  widget.state.hovered = control.contains(input.mouse);

  if (widget.state.hovered && input.mouse_clicked && !input.click_consumed) {
    input.focus_target = &widget;
    input.click_consumed = true;
  }

  if (widget.state.focused) {
    std::string& value = std::get<std::string>(widget.value);
    for (int i = 0; i < input.char_count && value.size() < kTextLimit; ++i) {
      const unsigned int code = input.chars[i];
      if (code >= 32 && code < 127) value += static_cast<char>(code);
    }
    if (input.captured_key == VK_BACK && !value.empty()) {
      value.pop_back();
      input.captured_key = 0;
    } else if (input.captured_key == VK_RETURN || input.captured_key == VK_ESCAPE) {
      widget.state.focused = false;
      input.captured_key = 0;
    }
    widget.state.blink += input.dt;
  }
}

void update_colorpicker(Widget& widget, Input& input) {
  Color& value = std::get<Color>(widget.value);
  const float scale = input.scale;
  const Rect control = control_rect(widget, scale);

  if (!widget.state.open) {
    widget.state.hovered = control.contains(input.mouse);
    if (widget.state.hovered && input.mouse_clicked && !input.click_consumed) {
      widget.state.open = true;
      rgb_to_hsv(value, widget.state.hue, widget.state.sv_s, widget.state.sv_v);
      input.click_consumed = true;
    }
    return;
  }

  const float pad = kPickerPad * scale;
  const float size = kPickerSize * scale;
  const float popup_height = pad * 3.0f + size + kPickerHueHeight * scale;
  const float popup_width = size + pad * 2.0f;
  widget.state.popup = {control.right() - popup_width,
                        popup_y_below_or_above(control, popup_height, scale, input.bounds), popup_width, popup_height};

  Rect sv;
  Rect hue;
  picker_geometry(widget.state.popup, scale, sv, hue);

  if (input.mouse_clicked) {
    if (sv.contains(input.mouse)) {
      widget.state.drag_sv = true;
      input.click_consumed = true;
    } else if (hue.contains(input.mouse)) {
      widget.state.drag_hue = true;
      input.click_consumed = true;
    } else {
      widget.state.open = false;
      input.click_consumed = true;
    }
  }

  if (widget.state.drag_sv || widget.state.drag_hue) {
    input.click_consumed = true;
    if (!input.mouse_down) {
      widget.state.drag_sv = false;
      widget.state.drag_hue = false;
    } else {
      if (widget.state.drag_sv) {
        widget.state.sv_s = clamp01((input.mouse.x - sv.x) / sv.w);
        widget.state.sv_v = 1.0f - clamp01((input.mouse.y - sv.y) / sv.h);
      }
      if (widget.state.drag_hue) widget.state.hue = clamp01((input.mouse.x - hue.x) / hue.w);
      value = hsv_to_rgb(widget.state.hue, widget.state.sv_s, widget.state.sv_v);
    }
  }
}

void update_button(Widget& widget, Input& input) {
  const Rect control = control_rect(widget, input.scale);
  widget.state.hovered = control.contains(input.mouse);
  widget.state.clicked = false;
  if (widget.state.hovered && input.mouse_clicked && !input.click_consumed) {
    widget.state.clicked = true;
    input.click_consumed = true;
  }
}

}  // namespace

bool widget_bool(const Widget& widget) { return std::get<bool>(widget.value); }
float widget_float(const Widget& widget) { return std::get<float>(widget.value); }
int widget_int(const Widget& widget) { return std::get<int>(widget.value); }
const std::string& widget_string(const Widget& widget) { return std::get<std::string>(widget.value); }
Color widget_color(const Widget& widget) { return std::get<Color>(widget.value); }
const KeyCombo& widget_key(const Widget& widget) { return std::get<KeyCombo>(widget.value); }

Widget make_toggle(const char* id, const char* label, bool value) {
  Widget widget;
  widget.id = id;
  widget.klass = WidgetClass::Toggle;
  widget.label = label;
  widget.value = value;
  return widget;
}

Widget make_checkbox(const char* id, const char* label, bool value) {
  Widget widget;
  widget.id = id;
  widget.klass = WidgetClass::Checkbox;
  widget.label = label;
  widget.value = value;
  return widget;
}

Widget make_slider(const char* id, const char* label, float value, float min, float max, const char* format,
                   const char* alt_text) {
  Widget widget;
  widget.id = id;
  widget.klass = WidgetClass::Slider;
  widget.label = label;
  widget.value = value;
  widget.min_value = min;
  widget.max_value = max;
  widget.format = format;
  widget.alt_text = alt_text;
  return widget;
}

Widget make_dropdown(const char* id, const char* label, int index, std::vector<std::string> items) {
  Widget widget;
  widget.id = id;
  widget.klass = WidgetClass::Dropdown;
  widget.label = label;
  widget.value = index;
  widget.items = std::move(items);
  return widget;
}

Widget make_keybind(const char* id, const char* label, int code) {
  Widget widget;
  widget.id = id;
  widget.klass = WidgetClass::Keybind;
  widget.label = label;
  widget.value = KeyCombo{code};
  return widget;
}

Widget make_textinput(const char* id, const char* label, const char* value) {
  Widget widget;
  widget.id = id;
  widget.klass = WidgetClass::TextInput;
  widget.label = label;
  widget.value = std::string(value);
  return widget;
}

Widget make_colorpicker(const char* id, const char* label, Color color) {
  Widget widget;
  widget.id = id;
  widget.klass = WidgetClass::ColorPicker;
  widget.label = label;
  widget.value = color;
  widget.show_gear = false;
  return widget;
}

Widget make_button(const char* id, const char* label) {
  Widget widget;
  widget.id = id;
  widget.klass = WidgetClass::Button;
  widget.label = label;
  widget.value = false;
  widget.show_label = false;
  widget.show_gear = false;
  return widget;
}

void key_name(int code, char* out, size_t capacity) {
  if (code == 0) {
    std::snprintf(out, capacity, "None");
  } else if (code < 0) {
    std::snprintf(out, capacity, "Mouse%u", static_cast<unsigned>(-code));
  } else if ((code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9')) {
    std::snprintf(out, capacity, "%c", static_cast<char>(code));
  } else if (code >= VK_F1 && code <= VK_F12) {
    std::snprintf(out, capacity, "F%d", code - VK_F1 + 1);
  } else {
    const char* name = nullptr;
    switch (code) {
      case VK_SPACE: name = "Space"; break;
      case VK_TAB: name = "Tab"; break;
      case VK_SHIFT: name = "Shift"; break;
      case VK_CONTROL: name = "Ctrl"; break;
      case VK_MENU: name = "Alt"; break;
      case VK_RETURN: name = "Enter"; break;
      case VK_ESCAPE: name = "Esc"; break;
      case VK_BACK: name = "Back"; break;
      case VK_INSERT: name = "Insert"; break;
      case VK_DELETE: name = "Delete"; break;
      case VK_HOME: name = "Home"; break;
      case VK_END: name = "End"; break;
      case VK_PRIOR: name = "PgUp"; break;
      case VK_NEXT: name = "PgDn"; break;
      case VK_LEFT: name = "Left"; break;
      case VK_RIGHT: name = "Right"; break;
      case VK_UP: name = "Up"; break;
      case VK_DOWN: name = "Down"; break;
      default: break;
    }
    if (name)
      std::snprintf(out, capacity, "%s", name);
    else
      std::snprintf(out, capacity, "0x%02X", code);
  }
}

const char* widget_class_name(WidgetClass klass) {
  switch (klass) {
    case WidgetClass::Toggle: return "toggle";
    case WidgetClass::Checkbox: return "checkbox";
    case WidgetClass::Slider: return "slider";
    case WidgetClass::Dropdown: return "dropdown";
    case WidgetClass::Keybind: return "keybind";
    case WidgetClass::TextInput: return "text-input";
    case WidgetClass::ColorPicker: return "color-picker";
    case WidgetClass::Button: return "button";
  }
  return "widget";
}

Rect widget_control_rect(const Widget& widget, float scale) { return control_rect(widget, scale); }

void widget_update(Widget& widget, Input& input) {
  switch (widget.klass) {
    case WidgetClass::Toggle:
    case WidgetClass::Checkbox: update_toggle(widget, input); break;
    case WidgetClass::Slider: update_slider(widget, input); break;
    case WidgetClass::Dropdown: update_dropdown(widget, input); break;
    case WidgetClass::Keybind: update_keybind(widget, input); break;
    case WidgetClass::TextInput: update_textinput(widget, input); break;
    case WidgetClass::ColorPicker: update_colorpicker(widget, input); break;
    case WidgetClass::Button: update_button(widget, input); break;
  }
}

void widget_paint(const Widget& widget, Canvas& canvas) {
  if (widget.painter) {
    widget.painter(widget, canvas);
    return;
  }
  if (widget.c_painter) return;
  switch (widget.klass) {
    case WidgetClass::Toggle: paint_toggle(widget, canvas); break;
    case WidgetClass::Checkbox: paint_checkbox(widget, canvas); break;
    case WidgetClass::Slider: paint_slider(widget, canvas); break;
    case WidgetClass::Dropdown: paint_dropdown(widget, canvas); break;
    case WidgetClass::Keybind: paint_keybind(widget, canvas); break;
    case WidgetClass::TextInput: paint_textinput(widget, canvas); break;
    case WidgetClass::ColorPicker: paint_colorpicker(widget, canvas); break;
    case WidgetClass::Button: paint_button(widget, canvas); break;
  }
}

void widget_paint_popup(const Widget& widget, Canvas& canvas) {
  if ((widget.klass != WidgetClass::Dropdown && widget.klass != WidgetClass::ColorPicker) || !widget.state.open ||
      widget.painter || widget.c_painter)
    return;

  const float scale = canvas.scale();
  const Rect popup = widget.state.popup;
  canvas.fill_rect(popup, style_color(widget, "panel", theme().panel), style_rounding(widget, theme().rounding) * scale);
  canvas.stroke_rect(popup, style_color(widget, "border", theme().border), 1.0f, style_rounding(widget, theme().rounding) * scale);

  if (widget.klass == WidgetClass::Dropdown) {
    const float item_height = kPopupItemHeight * scale;
    const float pad = kPopupPad * scale;
    const Color text = style_color(widget, "text", theme().text);
    const Color hover = style_color(widget, "row-hover", theme().row_hover);
    for (size_t i = 0; i < widget.items.size(); ++i) {
      const Rect item{popup.x + pad, popup.y + pad + item_height * static_cast<float>(i), popup.w - pad * 2.0f, item_height};
      if (static_cast<int>(i) == widget.state.hovered_item) canvas.fill_rect(item, hover, 2.0f * scale);
      const Vec2 size = canvas.measure_text(widget.items[i].c_str());
      canvas.text({item.x + 8.0f * scale, item.y + (item.h - size.y) * 0.5f}, text, widget.items[i].c_str());
    }
    return;
  }

  Rect sv;
  Rect hue;
  picker_geometry(popup, scale, sv, hue);

  const float cell_w = sv.w / static_cast<float>(kPickerCells);
  const float cell_h = sv.h / static_cast<float>(kPickerCells);
  for (int j = 0; j < kPickerCells; ++j) {
    for (int i = 0; i < kPickerCells; ++i) {
      const float s = static_cast<float>(i) / static_cast<float>(kPickerCells - 1);
      const float v = 1.0f - static_cast<float>(j) / static_cast<float>(kPickerCells - 1);
      const Rect cell{sv.x + cell_w * static_cast<float>(i), sv.y + cell_h * static_cast<float>(j), cell_w + 0.5f,
                      cell_h + 0.5f};
      canvas.fill_rect(cell, hsv_to_rgb(widget.state.hue, s, v));
    }
  }

  const float hue_cell = hue.w / static_cast<float>(kPickerHueCells);
  for (int i = 0; i < kPickerHueCells; ++i) {
    const float h = static_cast<float>(i) / static_cast<float>(kPickerHueCells - 1);
    canvas.fill_rect({hue.x + hue_cell * static_cast<float>(i), hue.y, hue_cell + 0.5f, hue.h}, hsv_to_rgb(h, 1.0f, 1.0f));
  }

  const Vec2 marker{sv.x + widget.state.sv_s * sv.w, sv.y + (1.0f - widget.state.sv_v) * sv.h};
  canvas.stroke_circle(marker, 6.0f * scale, {0.0f, 0.0f, 0.0f, 0.8f}, 1.5f * scale);
  canvas.stroke_circle(marker, 5.0f * scale, {1.0f, 1.0f, 1.0f, 1.0f}, 1.5f * scale);

  const float hue_x = hue.x + widget.state.hue * hue.w;
  canvas.fill_rect({hue_x - 1.5f * scale, hue.y - 1.0f * scale, 3.0f * scale, hue.h + 2.0f * scale},
                   {1.0f, 1.0f, 1.0f, 1.0f});
}

}  // namespace overlay::ui
