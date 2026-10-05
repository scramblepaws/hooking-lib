#include "overlay/overlay_c.h"

#include <windows.h>

#include <cstring>
#include <string>
#include <vector>

#include "c_api/bridge.h"
#include "core/config.h"
#include "core/log.h"
#include "core/state.h"
#include "external/external.h"
#include "hook/hook.h"
#include "overlay/overlay.h"
#include "script/script.h"
#include "ui/menu_model.h"
#include "ui/notifications.h"
#include "ui/theme.h"
#include "ui/widget.h"

struct OverlayContext {
  int reserved;
};

namespace {

OverlayContext g_context;

overlay::ui::Menu* menu_of(OverlayMenu* handle) { return reinterpret_cast<overlay::ui::Menu*>(handle); }
overlay::ui::Page* page_of(OverlayPage* handle) { return reinterpret_cast<overlay::ui::Page*>(handle); }
const overlay::ui::Page* page_of(const OverlayPage* handle) { return reinterpret_cast<const overlay::ui::Page*>(handle); }
overlay::ui::Section* section_of(OverlaySection* handle) { return reinterpret_cast<overlay::ui::Section*>(handle); }
const overlay::ui::Section* section_of(const OverlaySection* handle) {
  return reinterpret_cast<const overlay::ui::Section*>(handle);
}
overlay::ui::Widget* widget_of(OverlayWidget* handle) { return reinterpret_cast<overlay::ui::Widget*>(handle); }
const overlay::ui::Widget* widget_of(const OverlayWidget* handle) {
  return reinterpret_cast<const overlay::ui::Widget*>(handle);
}

OverlayRect to_c(const overlay::ui::Rect& rect) { return {rect.x, rect.y, rect.w, rect.h}; }
overlay::ui::Rect to_ui(OverlayRect rect) { return {rect.x, rect.y, rect.w, rect.h}; }
OverlayColor to_c(overlay::ui::Color color) { return {color.r, color.g, color.b, color.a}; }
overlay::ui::Color to_ui(OverlayColor color) { return {color.r, color.g, color.b, color.a}; }

}  // namespace

extern "C" {

uint32_t overlay_abi_version(void) { return OVERLAY_ABI_VERSION; }

#define OVERLAY_STRINGIFY_INNER(x) #x
#define OVERLAY_STRINGIFY(x) OVERLAY_STRINGIFY_INNER(x)

const char* overlay_version_string(void) {
  return OVERLAY_STRINGIFY(OVERLAY_VERSION_MAJOR) "." OVERLAY_STRINGIFY(OVERLAY_VERSION_MINOR) "." OVERLAY_STRINGIFY(
      OVERLAY_VERSION_PATCH);
}

OverlayStatus overlay_init(const OverlayInitInfo* info) {
  if (!info || info->size < sizeof(OverlayInitInfo)) return OVERLAY_ERR_INVALID_ARGUMENT;
  overlay::claim_host_init();
  if (info->menu_title && *info->menu_title) overlay::ui::menu().title = info->menu_title;
  if (info->flags & OVERLAY_FLAG_EXTERNAL)
    return overlay::external::start(info->target_hwnd) ? OVERLAY_OK : OVERLAY_ERR_BACKEND_UNAVAILABLE;
  if (info->backend != OVERLAY_BACKEND_AUTO && info->backend != OVERLAY_BACKEND_DX11)
    return OVERLAY_ERR_BACKEND_UNAVAILABLE;
  switch (overlay::init()) {
    case overlay::Status::ok:
    case overlay::Status::already_running:
      overlay::hook::mark_host_mode();
      return OVERLAY_OK;
    case overlay::Status::not_running:
      return OVERLAY_ERR_INTERNAL;
    default:
      return OVERLAY_ERR_HOOK_FAILED;
  }
}

OverlayStatus overlay_shutdown(void) {
  if (overlay::external::running()) {
    overlay::external::stop();
    return OVERLAY_OK;
  }
  return overlay::shutdown() == overlay::Status::ok ? OVERLAY_OK : OVERLAY_ERR_NOT_INITIALIZED;
}

int overlay_is_ready(void) { return (overlay::external::running() || overlay::running()) ? 1 : 0; }

int overlay_set_menu_open(int open) {
  overlay::state().menu_open.store(open != 0);
  return open != 0 ? 1 : 0;
}

int overlay_is_menu_open(void) { return overlay::state().menu_open.load() ? 1 : 0; }

OverlayContext* overlay_context(void) { return &g_context; }

OverlayMenu* overlay_menu(void) { return reinterpret_cast<OverlayMenu*>(&overlay::ui::menu()); }

void overlay_menu_set_chrome_painter(OverlayChromePaintFn painter, void* user) {
  overlay::ui::Menu& m = overlay::ui::menu();
  m.chrome_user = user;
  m.chrome_painter = painter ? reinterpret_cast<void (*)(void*, void*, void*)>(painter) : nullptr;
}

OverlayRect overlay_menu_rect(const OverlayMenu* handle) {
  const overlay::ui::Menu* m = reinterpret_cast<const overlay::ui::Menu*>(handle);
  return m ? to_c(m->rect) : OverlayRect{0, 0, 0, 0};
}

void overlay_menu_set_rect(OverlayMenu* handle, OverlayRect rect) {
  if (overlay::ui::Menu* m = menu_of(handle)) m->rect = to_ui(rect);
}

void overlay_menu_set_page(OverlayMenu* handle, int index) {
  overlay::ui::Menu* m = menu_of(handle);
  if (!m || m->pages.empty()) return;
  if (index < 0) index = 0;
  if (index >= static_cast<int>(m->pages.size())) index = static_cast<int>(m->pages.size()) - 1;
  m->current_page = index;
}

int overlay_menu_page_index(const OverlayMenu* handle) {
  const overlay::ui::Menu* m = reinterpret_cast<const overlay::ui::Menu*>(handle);
  return m ? m->current_page : 0;
}

size_t overlay_menu_page_count(const OverlayMenu* handle) {
  const overlay::ui::Menu* m = reinterpret_cast<const overlay::ui::Menu*>(handle);
  return m ? m->pages.size() : 0;
}

OverlayPage* overlay_menu_page(OverlayMenu* handle, size_t index) {
  overlay::ui::Menu* m = menu_of(handle);
  if (!m || index >= m->pages.size()) return nullptr;
  return reinterpret_cast<OverlayPage*>(&m->pages[index]);
}

OverlayPage* overlay_page_create(OverlayContext* context, const char* title, const char* nav_section) {
  if (!context || !title) return nullptr;
  overlay::ui::Menu& m = overlay::ui::menu();
  m.pages.emplace_back();
  overlay::ui::Page& page = m.pages.back();
  page.title = title;
  page.nav_section = nav_section ? nav_section : "";
  return reinterpret_cast<OverlayPage*>(&page);
}

void overlay_page_destroy(OverlayPage* handle) {
  overlay::ui::Menu& m = overlay::ui::menu();
  for (size_t i = 0; i < m.pages.size(); ++i) {
    if (reinterpret_cast<OverlayPage*>(&m.pages[i]) == handle) {
      m.pages.erase(m.pages.begin() + static_cast<ptrdiff_t>(i));
      if (m.current_page >= static_cast<int>(m.pages.size())) m.current_page = 0;
      return;
    }
  }
}

const char* overlay_page_title(const OverlayPage* page) {
  const overlay::ui::Page* p = page_of(page);
  return p ? p->title.c_str() : "";
}

const char* overlay_page_nav_section(const OverlayPage* page) {
  const overlay::ui::Page* p = page_of(page);
  return p ? p->nav_section.c_str() : "";
}

size_t overlay_page_section_count(const OverlayPage* page) {
  const overlay::ui::Page* p = page_of(page);
  return p ? p->sections.size() : 0;
}

OverlaySection* overlay_page_section(OverlayPage* page, size_t index) {
  overlay::ui::Page* p = page_of(page);
  if (!p || index >= p->sections.size()) return nullptr;
  return reinterpret_cast<OverlaySection*>(&p->sections[index]);
}

OverlaySection* overlay_section_create(OverlayPage* page, const char* title) {
  overlay::ui::Page* p = page_of(page);
  if (!p || !title) return nullptr;
  p->sections.emplace_back();
  p->sections.back().title = title;
  return reinterpret_cast<OverlaySection*>(&p->sections.back());
}

void overlay_section_destroy(OverlaySection* handle) {
  overlay::ui::Menu& m = overlay::ui::menu();
  for (overlay::ui::Page& page : m.pages) {
    for (size_t i = 0; i < page.sections.size(); ++i) {
      if (reinterpret_cast<OverlaySection*>(&page.sections[i]) == handle) {
        page.sections.erase(page.sections.begin() + static_cast<ptrdiff_t>(i));
        return;
      }
    }
  }
}

const char* overlay_section_title(const OverlaySection* section) {
  const overlay::ui::Section* s = section_of(section);
  return s ? s->title.c_str() : "";
}

size_t overlay_section_widget_count(const OverlaySection* section) {
  const overlay::ui::Section* s = section_of(section);
  return s ? s->widgets.size() : 0;
}

OverlayWidget* overlay_section_widget(OverlaySection* section, size_t index) {
  overlay::ui::Section* s = section_of(section);
  if (!s || index >= s->widgets.size()) return nullptr;
  return reinterpret_cast<OverlayWidget*>(&s->widgets[index]);
}

OverlayWidget* overlay_widget_create(OverlaySection* section, OverlayWidgetKind kind, const char* id, const char* label) {
  overlay::ui::Section* s = section_of(section);
  if (!s || !id) return nullptr;
  overlay::ui::Widget widget;
  widget.id = id;
  widget.label = label ? label : "";
  switch (kind) {
    case OVERLAY_WIDGET_TOGGLE:
      widget.klass = overlay::ui::WidgetClass::Toggle;
      widget.value = false;
      break;
    case OVERLAY_WIDGET_CHECKBOX:
      widget.klass = overlay::ui::WidgetClass::Checkbox;
      widget.value = false;
      break;
    case OVERLAY_WIDGET_SLIDER:
      widget.klass = overlay::ui::WidgetClass::Slider;
      widget.value = 0.0f;
      widget.min_value = 0.0f;
      widget.max_value = 1.0f;
      widget.format = "%.2f";
      break;
    case OVERLAY_WIDGET_DROPDOWN:
      widget.klass = overlay::ui::WidgetClass::Dropdown;
      widget.value = 0;
      break;
    case OVERLAY_WIDGET_KEYBIND:
      widget.klass = overlay::ui::WidgetClass::Keybind;
      widget.value = overlay::ui::KeyCombo{};
      break;
    case OVERLAY_WIDGET_TEXT_INPUT:
      widget.klass = overlay::ui::WidgetClass::TextInput;
      widget.value = std::string();
      break;
    case OVERLAY_WIDGET_COLOR_PICKER:
      widget.klass = overlay::ui::WidgetClass::ColorPicker;
      widget.value = overlay::ui::Color{1.0f, 1.0f, 1.0f, 1.0f};
      widget.show_gear = false;
      break;
    case OVERLAY_WIDGET_BUTTON:
      widget.klass = overlay::ui::WidgetClass::Button;
      widget.value = false;
      widget.show_label = false;
      widget.show_gear = false;
      break;
    default:
      return nullptr;
  }
  s->widgets.push_back(std::move(widget));
  return reinterpret_cast<OverlayWidget*>(&s->widgets.back());
}

void overlay_widget_destroy(OverlayWidget* handle) {
  overlay::ui::Menu& m = overlay::ui::menu();
  for (overlay::ui::Page& page : m.pages) {
    for (overlay::ui::Section& section : page.sections) {
      for (size_t i = 0; i < section.widgets.size(); ++i) {
        if (reinterpret_cast<OverlayWidget*>(&section.widgets[i]) == handle) {
          section.widgets.erase(section.widgets.begin() + static_cast<ptrdiff_t>(i));
          return;
        }
      }
    }
  }
}

void overlay_widget_set_rect(OverlayWidget* handle, OverlayRect rect) {
  if (overlay::ui::Widget* w = widget_of(handle)) w->rect = to_ui(rect);
}

void overlay_widget_set_indent(OverlayWidget* handle, int indent) {
  if (overlay::ui::Widget* w = widget_of(handle)) w->indent = indent < 0 ? 0 : indent;
}

void overlay_widget_set_bool(OverlayWidget* handle, int value) {
  overlay::ui::Widget* w = widget_of(handle);
  if (w) {
    if (bool* target = std::get_if<bool>(&w->value)) *target = value != 0;
  }
}

int overlay_widget_get_bool(const OverlayWidget* handle) {
  const overlay::ui::Widget* w = widget_of(handle);
  const bool* value = w ? std::get_if<bool>(&w->value) : nullptr;
  return value && *value ? 1 : 0;
}

void overlay_widget_set_float(OverlayWidget* handle, float value) {
  overlay::ui::Widget* w = widget_of(handle);
  if (w) {
    if (float* target = std::get_if<float>(&w->value)) *target = value;
  }
}

float overlay_widget_get_float(const OverlayWidget* handle) {
  const overlay::ui::Widget* w = widget_of(handle);
  const float* value = w ? std::get_if<float>(&w->value) : nullptr;
  return value ? *value : 0.0f;
}

void overlay_widget_set_int(OverlayWidget* handle, int value) {
  overlay::ui::Widget* w = widget_of(handle);
  if (w) {
    if (int* target = std::get_if<int>(&w->value)) *target = value;
  }
}

int overlay_widget_get_int(const OverlayWidget* handle) {
  const overlay::ui::Widget* w = widget_of(handle);
  const int* value = w ? std::get_if<int>(&w->value) : nullptr;
  return value ? *value : 0;
}

size_t overlay_widget_get_text(const OverlayWidget* handle, char* buffer, size_t capacity) {
  const overlay::ui::Widget* w = widget_of(handle);
  const std::string* text = w ? std::get_if<std::string>(&w->value) : nullptr;
  const size_t length = text ? text->size() : 0;
  if (buffer && capacity > 0) {
    const size_t copy = length < capacity - 1 ? length : capacity - 1;
    if (text) std::memcpy(buffer, text->data(), copy);
    buffer[copy] = '\0';
  }
  return length + 1;
}

void overlay_widget_set_text(OverlayWidget* handle, const char* value) {
  overlay::ui::Widget* w = widget_of(handle);
  if (w) {
    if (std::string* target = std::get_if<std::string>(&w->value)) *target = value ? value : "";
  }
}

void overlay_widget_set_key(OverlayWidget* handle, int code) {
  overlay::ui::Widget* w = widget_of(handle);
  if (w) {
    if (overlay::ui::KeyCombo* target = std::get_if<overlay::ui::KeyCombo>(&w->value)) target->code = code;
  }
}

int overlay_widget_get_key(const OverlayWidget* handle) {
  const overlay::ui::Widget* w = widget_of(handle);
  const overlay::ui::KeyCombo* key = w ? std::get_if<overlay::ui::KeyCombo>(&w->value) : nullptr;
  return key ? key->code : 0;
}

void overlay_widget_set_color(OverlayWidget* handle, OverlayColor color) {
  overlay::ui::Widget* w = widget_of(handle);
  if (w) {
    if (overlay::ui::Color* target = std::get_if<overlay::ui::Color>(&w->value)) *target = to_ui(color);
  }
}

OverlayColor overlay_widget_get_color(const OverlayWidget* handle) {
  const overlay::ui::Widget* w = widget_of(handle);
  const overlay::ui::Color* color = w ? std::get_if<overlay::ui::Color>(&w->value) : nullptr;
  return color ? to_c(*color) : OverlayColor{0, 0, 0, 1};
}

void overlay_widget_set_range(OverlayWidget* handle, float min, float max) {
  overlay::ui::Widget* w = widget_of(handle);
  if (!w) return;
  w->min_value = min;
  w->max_value = max;
}

void overlay_widget_set_format(OverlayWidget* handle, const char* format, const char* alt_text) {
  overlay::ui::Widget* w = widget_of(handle);
  if (!w) return;
  w->format = overlay::ui::sanitize_number_format(format);
  w->alt_text = alt_text ? alt_text : "";
}

void overlay_widget_set_items(OverlayWidget* handle, const char* const* items, size_t count) {
  overlay::ui::Widget* w = widget_of(handle);
  if (!w) return;
  if (count > 0 && !items) return;
  w->items.clear();
  w->items.reserve(count);
  for (size_t i = 0; i < count; ++i) w->items.push_back(items[i] ? items[i] : "");
}

void overlay_widget_set_enabled(OverlayWidget* handle, int enabled) {
  if (overlay::ui::Widget* w = widget_of(handle)) w->state.enabled = enabled != 0;
}

void overlay_widget_set_painter(OverlayWidget* handle, OverlayPaintFn painter, void* user) {
  overlay::ui::Widget* w = widget_of(handle);
  if (!w) return;
  w->c_painter_user = user;
  w->c_painter = painter ? reinterpret_cast<void (*)(void*, const void*, void*)>(painter) : nullptr;
}

OverlayRect overlay_widget_rect(const OverlayWidget* handle) {
  const overlay::ui::Widget* w = widget_of(handle);
  return w ? to_c(w->rect) : OverlayRect{0, 0, 0, 0};
}

OverlayRect overlay_widget_control_rect(const OverlayWidget* handle, float scale) {
  const overlay::ui::Widget* w = widget_of(handle);
  return w ? to_c(overlay::ui::widget_control_rect(*w, scale)) : OverlayRect{0, 0, 0, 0};
}

int overlay_widget_is_hovered(const OverlayWidget* handle) {
  const overlay::ui::Widget* w = widget_of(handle);
  return w && w->state.hovered ? 1 : 0;
}

int overlay_widget_is_focused(const OverlayWidget* handle) {
  const overlay::ui::Widget* w = widget_of(handle);
  return w && w->state.focused ? 1 : 0;
}

int overlay_widget_was_clicked(const OverlayWidget* handle) {
  const overlay::ui::Widget* w = widget_of(handle);
  return w && w->state.clicked ? 1 : 0;
}

float overlay_widget_anim(const OverlayWidget* handle) {
  const overlay::ui::Widget* w = widget_of(handle);
  return w ? w->state.anim : 0.0f;
}

float overlay_canvas_scale(const OverlayCanvas* canvas) { return canvas && canvas->impl ? canvas->impl->scale() : 1.0f; }

OverlayVec2 overlay_canvas_measure_text(const OverlayCanvas* canvas, const char* text) {
  if (!canvas || !canvas->impl || !text) return {0, 0};
  const overlay::ui::Vec2 size = canvas->impl->measure_text(text);
  return {size.x, size.y};
}

OverlayVec2 overlay_canvas_measure_text_sized(const OverlayCanvas* canvas, const char* text, float size) {
  if (!canvas || !canvas->impl || !text) return {0, 0};
  const overlay::ui::Vec2 measured = canvas->impl->measure_text_sized(text, size);
  return {measured.x, measured.y};
}

void overlay_canvas_fill_rect(OverlayCanvas* canvas, OverlayRect rect, OverlayColor color, float radius) {
  if (canvas && canvas->impl) canvas->impl->fill_rect(to_ui(rect), to_ui(color), radius);
}

void overlay_canvas_stroke_rect(OverlayCanvas* canvas, OverlayRect rect, OverlayColor color, float thickness,
                                float radius) {
  if (canvas && canvas->impl) canvas->impl->stroke_rect(to_ui(rect), to_ui(color), thickness, radius);
}

void overlay_canvas_fill_circle(OverlayCanvas* canvas, OverlayVec2 center, float radius, OverlayColor color) {
  if (canvas && canvas->impl) canvas->impl->fill_circle({center.x, center.y}, radius, to_ui(color));
}

void overlay_canvas_stroke_circle(OverlayCanvas* canvas, OverlayVec2 center, float radius, OverlayColor color,
                                  float thickness) {
  if (canvas && canvas->impl) canvas->impl->stroke_circle({center.x, center.y}, radius, to_ui(color), thickness);
}

void overlay_canvas_fill_triangle(OverlayCanvas* canvas, OverlayVec2 a, OverlayVec2 b, OverlayVec2 c, OverlayColor color) {
  if (canvas && canvas->impl) canvas->impl->fill_triangle({a.x, a.y}, {b.x, b.y}, {c.x, c.y}, to_ui(color));
}

void overlay_canvas_text(OverlayCanvas* canvas, OverlayVec2 pos, OverlayColor color, const char* text) {
  if (canvas && canvas->impl && text) canvas->impl->text({pos.x, pos.y}, to_ui(color), text);
}

void overlay_canvas_text_sized(OverlayCanvas* canvas, OverlayVec2 pos, OverlayColor color, float size, const char* text) {
  if (canvas && canvas->impl && text) canvas->impl->text_sized({pos.x, pos.y}, to_ui(color), size, text);
}

void overlay_canvas_text_centered(OverlayCanvas* canvas, OverlayRect rect, OverlayColor color, const char* text) {
  if (canvas && canvas->impl && text) canvas->impl->text_centered(to_ui(rect), to_ui(color), text);
}

void overlay_canvas_push_clip(OverlayCanvas* canvas, OverlayRect rect) {
  if (canvas && canvas->impl) canvas->impl->push_clip(to_ui(rect));
}

void overlay_canvas_pop_clip(OverlayCanvas* canvas) {
  if (canvas && canvas->impl) canvas->impl->pop_clip();
}

OverlayStatus overlay_config_save(const char* name) {
  if (!name) return OVERLAY_ERR_INVALID_ARGUMENT;
  return overlay::config::save(name) ? OVERLAY_OK : OVERLAY_ERR_IO;
}

OverlayStatus overlay_config_load(const char* name) {
  if (!name) return OVERLAY_ERR_INVALID_ARGUMENT;
  return overlay::config::load(name) ? OVERLAY_OK : OVERLAY_ERR_IO;
}

OverlayStatus overlay_config_delete(const char* name) {
  if (!name) return OVERLAY_ERR_INVALID_ARGUMENT;
  return overlay::config::remove(name) ? OVERLAY_OK : OVERLAY_ERR_IO;
}

void overlay_config_list(OverlayConfigListFn callback, void* user) {
  if (!callback) return;
  overlay::config::list(reinterpret_cast<void (*)(void*, const char*)>(callback), user);
}

void overlay_notify(const char* message) { overlay::ui::notify(message); }

void overlay_event(const char* message) { overlay::ui::emit_event(message); }

void overlay_toast_position(OverlayToastPosition position) {
  switch (position) {
    case OVERLAY_TOAST_TOP_LEFT: overlay::ui::set_toast_position(overlay::ui::ToastPosition::TopLeft); break;
    case OVERLAY_TOAST_BOTTOM_RIGHT: overlay::ui::set_toast_position(overlay::ui::ToastPosition::BottomRight); break;
    case OVERLAY_TOAST_BOTTOM_LEFT: overlay::ui::set_toast_position(overlay::ui::ToastPosition::BottomLeft); break;
    default: overlay::ui::set_toast_position(overlay::ui::ToastPosition::TopRight); break;
  }
}

void overlay_toast_duration(float seconds) { overlay::ui::set_toast_duration(seconds); }

OverlayStatus overlay_style_parse(const char* css) {
  if (!css) return OVERLAY_ERR_INVALID_ARGUMENT;
  return overlay::ui::style_parse(css) ? OVERLAY_OK : OVERLAY_ERR_INVALID_ARGUMENT;
}

OverlayStatus overlay_style_load(const char* utf8_path) {
  if (!utf8_path) return OVERLAY_ERR_INVALID_ARGUMENT;
  const int length = MultiByteToWideChar(CP_UTF8, 0, utf8_path, -1, nullptr, 0);
  if (length <= 0) return OVERLAY_ERR_INVALID_ARGUMENT;
  std::wstring wide(static_cast<size_t>(length - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8_path, -1, wide.data(), length);
  return overlay::ui::style_load_file(wide) ? OVERLAY_OK : OVERLAY_ERR_IO;
}

void overlay_style_clear(void) { overlay::ui::style_clear(); }

OverlayStatus overlay_script_execute(const char* source, const char* chunk_name) {
  if (!source) return OVERLAY_ERR_INVALID_ARGUMENT;
  return overlay::script::execute(source, chunk_name) ? OVERLAY_OK : OVERLAY_ERR_INTERNAL;
}

OverlayStatus overlay_script_reload(void) {
  overlay::script::request_reload();
  return OVERLAY_OK;
}

}  // extern "C"
