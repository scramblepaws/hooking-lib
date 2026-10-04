#include "script/script.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <string>
#include <vector>

#include "lua.h"
#include "luacode.h"
#include "lualib.h"

#include "core/config.h"
#include "core/log.h"
#include "core/paths.h"
#include "ui/menu_model.h"
#include "ui/notifications.h"
#include "ui/widget.h"

namespace overlay::script {

namespace {

constexpr double kTickBudgetMs = 5.0;
constexpr double kReloadIntervalSeconds = 0.5;
constexpr int kCanvasTag = 42;
constexpr int kDrawingTag = 43;

lua_State* g_state = nullptr;
bool g_started = false;
double g_last_now = 0.0;
double g_reload_check_at = 0.0;
unsigned long long g_scripts_stamp = 0;
bool g_reload_pending = false;
std::chrono::steady_clock::time_point g_deadline;

struct Sleeper {
  lua_State* thread = nullptr;
  int ref = 0;
  double wake = 0.0;
  int nargs = 0;
};
std::vector<Sleeper> g_sleepers;
std::map<std::string, int> g_click_refs;
std::vector<int> g_painter_refs;
void* (*g_texture_provider)(const char*) = nullptr;
char* g_spawn_bytecode = nullptr;
size_t g_spawn_size = 0;

struct LuaCanvas {
  overlay::ui::Canvas* canvas = nullptr;
};

overlay::ui::Widget* find_widget(const char* id) {
  if (!id) return nullptr;
  for (overlay::ui::Page& page : overlay::ui::menu().pages)
    for (overlay::ui::Section& section : page.sections)
      for (overlay::ui::Widget& widget : section.widgets)
        if (widget.id == id) return &widget;
  return nullptr;
}

overlay::ui::Section* section_arg(lua_State* state, int index) {
  auto* section = static_cast<overlay::ui::Section*>(lua_touserdata(state, index));
  if (!section) luaL_error(state, "expected a section handle");
  return section;
}

void begin_budget() {
  g_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(kTickBudgetMs));
}

void interrupt(lua_State* state, int gc) {
  if (gc >= 0) return;
  if (std::chrono::steady_clock::now() > g_deadline) luaL_error(state, "script exceeded its time budget");
}

bool parse_hex_color(const char* text, overlay::ui::Color& out) {
  if (!text || text[0] != '#') return false;
  const size_t length = std::strlen(text) - 1;
  auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  int values[4] = {0, 0, 0, 255};
  const char* digits = text + 1;
  if (length == 3) {
    for (int i = 0; i < 3; ++i) {
      const int value = nibble(digits[i]);
      if (value < 0) return false;
      values[i] = value * 17;
    }
  } else if (length == 6 || length == 8) {
    for (size_t i = 0; i < length / 2; ++i) {
      const int high = nibble(digits[i * 2]);
      const int low = nibble(digits[i * 2 + 1]);
      if (high < 0 || low < 0) return false;
      values[i] = high * 16 + low;
    }
  } else {
    return false;
  }
  out = {values[0] / 255.0f, values[1] / 255.0f, values[2] / 255.0f, values[3] / 255.0f};
  return true;
}

// Color is either a hex string (1 stack slot) or four numbers; *used reports how many.
bool color_arg(lua_State* state, int index, overlay::ui::Color& out, int* used = nullptr) {
  if (lua_isstring(state, index)) {
    if (used) *used = 1;
    return parse_hex_color(lua_tostring(state, index), out);
  }
  if (lua_isnumber(state, index)) {
    out.r = static_cast<float>(lua_tonumber(state, index));
    out.g = static_cast<float>(lua_tonumber(state, index + 1));
    out.b = static_cast<float>(lua_tonumber(state, index + 2));
    out.a = static_cast<float>(lua_tonumber(state, index + 3));
    if (out.r > 1.0f) out.r /= 255.0f;
    if (out.g > 1.0f) out.g /= 255.0f;
    if (out.b > 1.0f) out.b /= 255.0f;
    if (used) *used = 4;
    return true;
  }
  return false;
}

void push_color(lua_State* state, const overlay::ui::Color& color) {
  lua_createtable(state, 0, 4);
  lua_pushnumber(state, color.r);
  lua_setfield(state, -2, "r");
  lua_pushnumber(state, color.g);
  lua_setfield(state, -2, "g");
  lua_pushnumber(state, color.b);
  lua_setfield(state, -2, "b");
  lua_pushnumber(state, color.a);
  lua_setfield(state, -2, "a");
}

void push_widget_state(lua_State* state, const overlay::ui::Widget& widget) {
  lua_createtable(state, 0, 16);
  switch (widget.klass) {
    case overlay::ui::WidgetClass::Toggle:
    case overlay::ui::WidgetClass::Checkbox:
    case overlay::ui::WidgetClass::Button:
      lua_pushboolean(state, widget_bool(widget) ? 1 : 0);
      break;
    case overlay::ui::WidgetClass::Slider:
      lua_pushnumber(state, widget_float(widget));
      break;
    case overlay::ui::WidgetClass::Dropdown:
      lua_pushinteger(state, widget_int(widget));
      break;
    case overlay::ui::WidgetClass::Keybind:
      lua_pushinteger(state, widget_key(widget).code);
      break;
    case overlay::ui::WidgetClass::TextInput:
      lua_pushstring(state, widget_string(widget).c_str());
      break;
    case overlay::ui::WidgetClass::ColorPicker:
      push_color(state, widget_color(widget));
      break;
  }
  lua_setfield(state, -2, "value");
  lua_pushboolean(state, widget.state.hovered ? 1 : 0);
  lua_setfield(state, -2, "hovered");
  lua_pushboolean(state, widget.state.focused ? 1 : 0);
  lua_setfield(state, -2, "focused");
  lua_pushboolean(state, widget.state.clicked ? 1 : 0);
  lua_setfield(state, -2, "clicked");
  lua_pushnumber(state, widget.state.anim);
  lua_setfield(state, -2, "anim");
  lua_pushnumber(state, widget.rect.x);
  lua_setfield(state, -2, "x");
  lua_pushnumber(state, widget.rect.y);
  lua_setfield(state, -2, "y");
  lua_pushnumber(state, widget.rect.w);
  lua_setfield(state, -2, "w");
  lua_pushnumber(state, widget.rect.h);
  lua_setfield(state, -2, "h");
  const overlay::ui::Rect control = overlay::ui::widget_control_rect(widget, 1.0f);
  lua_pushnumber(state, control.x);
  lua_setfield(state, -2, "cx");
  lua_pushnumber(state, control.y);
  lua_setfield(state, -2, "cy");
  lua_pushnumber(state, control.w);
  lua_setfield(state, -2, "cw");
  lua_pushnumber(state, control.h);
  lua_setfield(state, -2, "ch");
}

// ---- canvas binding -------------------------------------------------

LuaCanvas* canvas_arg(lua_State* state, int index) {
  auto* canvas = static_cast<LuaCanvas*>(lua_touserdata(state, index));
  if (!canvas || !canvas->canvas) luaL_error(state, "expected a canvas");
  return canvas;
}

overlay::ui::Rect rect_arg(lua_State* state, int index) {
  return {static_cast<float>(luaL_checknumber(state, index)), static_cast<float>(luaL_checknumber(state, index + 1)),
          static_cast<float>(luaL_checknumber(state, index + 2)), static_cast<float>(luaL_checknumber(state, index + 3))};
}

int l_canvas_rect(lua_State* state) {
  LuaCanvas* canvas = canvas_arg(state, 1);
  const overlay::ui::Rect rect = rect_arg(state, 2);
  overlay::ui::Color color;
  int used = 0;
  if (!color_arg(state, 6, color, &used)) luaL_error(state, "expected a color");
  canvas->canvas->fill_rect(rect, color, static_cast<float>(luaL_optnumber(state, 6 + used, 0.0)));
  return 0;
}

int l_canvas_outline(lua_State* state) {
  LuaCanvas* canvas = canvas_arg(state, 1);
  const overlay::ui::Rect rect = rect_arg(state, 2);
  overlay::ui::Color color;
  int used = 0;
  if (!color_arg(state, 6, color, &used)) luaL_error(state, "expected a color");
  const float thickness = static_cast<float>(luaL_optnumber(state, 6 + used, 1.0));
  const float radius = static_cast<float>(luaL_optnumber(state, 7 + used, 0.0));
  canvas->canvas->stroke_rect(rect, color, thickness, radius);
  return 0;
}

int l_canvas_circle(lua_State* state) {
  LuaCanvas* canvas = canvas_arg(state, 1);
  const overlay::ui::Vec2 center{static_cast<float>(luaL_checknumber(state, 2)),
                                 static_cast<float>(luaL_checknumber(state, 3))};
  const float radius = static_cast<float>(luaL_checknumber(state, 4));
  overlay::ui::Color color;
  if (!color_arg(state, 5, color)) luaL_error(state, "expected a color");
  canvas->canvas->fill_circle(center, radius, color);
  return 0;
}

int l_canvas_text(lua_State* state) {
  LuaCanvas* canvas = canvas_arg(state, 1);
  const overlay::ui::Vec2 position{static_cast<float>(luaL_checknumber(state, 2)),
                                   static_cast<float>(luaL_checknumber(state, 3))};
  const char* text = luaL_checkstring(state, 4);
  overlay::ui::Color color;
  int used = 0;
  if (!color_arg(state, 5, color, &used)) luaL_error(state, "expected a color");
  const float size = static_cast<float>(luaL_optnumber(state, 5 + used, 0.0));
  if (size > 0.0f)
    canvas->canvas->text_sized(position, color, size, text);
  else
    canvas->canvas->text(position, color, text);
  return 0;
}

int l_canvas_text_centered(lua_State* state) {
  LuaCanvas* canvas = canvas_arg(state, 1);
  const overlay::ui::Rect rect = rect_arg(state, 2);
  const char* text = luaL_checkstring(state, 6);
  overlay::ui::Color color;
  if (!color_arg(state, 7, color)) luaL_error(state, "expected a color");
  canvas->canvas->text_centered(rect, color, text);
  return 0;
}

int l_canvas_measure(lua_State* state) {
  LuaCanvas* canvas = canvas_arg(state, 1);
  const char* text = luaL_checkstring(state, 2);
  const float size = static_cast<float>(luaL_optnumber(state, 3, 0.0));
  const overlay::ui::Vec2 measured =
      size > 0.0f ? canvas->canvas->measure_text_sized(text, size) : canvas->canvas->measure_text(text);
  lua_pushnumber(state, measured.x);
  lua_pushnumber(state, measured.y);
  return 2;
}

int l_canvas_scale(lua_State* state) {
  lua_pushnumber(state, canvas_arg(state, 1)->canvas->scale());
  return 1;
}

int l_canvas_clip_push(lua_State* state) {
  canvas_arg(state, 1)->canvas->push_clip(rect_arg(state, 2));
  return 0;
}

int l_canvas_clip_pop(lua_State* state) {
  canvas_arg(state, 1)->canvas->pop_clip();
  return 0;
}

// ---- ui binding -----------------------------------------------------

int l_page(lua_State* state) {
  const char* title = luaL_checkstring(state, 1);
  const char* section = luaL_optstring(state, 2, "");
  overlay::ui::Menu& menu = overlay::ui::menu();
  menu.pages.emplace_back();
  overlay::ui::Page& page = menu.pages.back();
  page.title = title;
  page.nav_section = section;
  lua_pushlightuserdata(state, &page);
  return 1;
}

int l_section(lua_State* state) {
  auto* page = static_cast<overlay::ui::Page*>(lua_touserdata(state, 1));
  if (!page) luaL_error(state, "expected a page handle");
  page->sections.emplace_back();
  page->sections.back().title = luaL_checkstring(state, 2);
  lua_pushlightuserdata(state, &page->sections.back());
  return 1;
}

void add_widget(overlay::ui::Section* section, overlay::ui::Widget widget) {
  section->widgets.push_back(std::move(widget));
}

int l_toggle(lua_State* state) {
  overlay::ui::Section* section = section_arg(state, 1);
  add_widget(section, overlay::ui::make_toggle(luaL_checkstring(state, 2), luaL_checkstring(state, 3),
                                               lua_toboolean(state, 4) != 0));
  return 0;
}

int l_checkbox(lua_State* state) {
  overlay::ui::Section* section = section_arg(state, 1);
  add_widget(section, overlay::ui::make_checkbox(luaL_checkstring(state, 2), luaL_checkstring(state, 3),
                                                 lua_toboolean(state, 4) != 0));
  return 0;
}

int l_slider(lua_State* state) {
  overlay::ui::Section* section = section_arg(state, 1);
  const char* id = luaL_checkstring(state, 2);
  const char* label = luaL_checkstring(state, 3);
  const float value = static_cast<float>(luaL_optnumber(state, 4, 0.0));
  const float min = static_cast<float>(luaL_optnumber(state, 5, 0.0));
  const float max = static_cast<float>(luaL_optnumber(state, 6, 100.0));
  const char* format = luaL_optstring(state, 7, "%.0f");
  const char* alt = lua_isstring(state, 8) ? lua_tostring(state, 8) : nullptr;
  add_widget(section, overlay::ui::make_slider(id, label, value, min, max, format, alt));
  return 0;
}

int l_dropdown(lua_State* state) {
  overlay::ui::Section* section = section_arg(state, 1);
  const char* id = luaL_checkstring(state, 2);
  const char* label = luaL_checkstring(state, 3);
  luaL_checktype(state, 4, LUA_TTABLE);
  std::vector<std::string> items;
  const size_t count = lua_objlen(state, 4);
  for (size_t i = 1; i <= count; ++i) {
    lua_rawgeti(state, 4, static_cast<int>(i));
    items.emplace_back(luaL_checkstring(state, -1));
    lua_pop(state, 1);
  }
  add_widget(section, overlay::ui::make_dropdown(id, label, static_cast<int>(luaL_optinteger(state, 5, 0)), std::move(items)));
  return 0;
}

int l_keybind(lua_State* state) {
  overlay::ui::Section* section = section_arg(state, 1);
  add_widget(section, overlay::ui::make_keybind(luaL_checkstring(state, 2), luaL_checkstring(state, 3),
                                                static_cast<int>(luaL_optinteger(state, 4, 0))));
  return 0;
}

int l_text_input(lua_State* state) {
  overlay::ui::Section* section = section_arg(state, 1);
  add_widget(section, overlay::ui::make_textinput(luaL_checkstring(state, 2), luaL_checkstring(state, 3),
                                                  luaL_optstring(state, 4, "")));
  return 0;
}

int l_colorpicker(lua_State* state) {
  overlay::ui::Section* section = section_arg(state, 1);
  const char* id = luaL_checkstring(state, 2);
  const char* label = luaL_checkstring(state, 3);
  overlay::ui::Color color{1.0f, 1.0f, 1.0f, 1.0f};
  if (!color_arg(state, 4, color)) luaL_error(state, "expected a color");
  add_widget(section, overlay::ui::make_colorpicker(id, label, color));
  return 0;
}

int l_button(lua_State* state) {
  overlay::ui::Section* section = section_arg(state, 1);
  add_widget(section, overlay::ui::make_button(luaL_checkstring(state, 2), luaL_checkstring(state, 3)));
  return 0;
}

// ---- properties -----------------------------------------------------

int l_get_bool(lua_State* state) {
  overlay::ui::Widget* widget = find_widget(luaL_checkstring(state, 1));
  lua_pushboolean(state, widget && widget_bool(*widget) ? 1 : 0);
  return 1;
}

int l_set_bool(lua_State* state) {
  overlay::ui::Widget* widget = find_widget(luaL_checkstring(state, 1));
  if (widget) {
    if (bool* target = std::get_if<bool>(&widget->value)) *target = lua_toboolean(state, 2) != 0;
  }
  return 0;
}

int l_get_number(lua_State* state) {
  overlay::ui::Widget* widget = find_widget(luaL_checkstring(state, 1));
  lua_pushnumber(state, widget ? widget_float(*widget) : 0.0);
  return 1;
}

int l_set_number(lua_State* state) {
  overlay::ui::Widget* widget = find_widget(luaL_checkstring(state, 1));
  if (widget) {
    if (float* target = std::get_if<float>(&widget->value)) *target = static_cast<float>(luaL_checknumber(state, 2));
  }
  return 0;
}

int l_get_int(lua_State* state) {
  overlay::ui::Widget* widget = find_widget(luaL_checkstring(state, 1));
  lua_pushinteger(state, widget ? widget_int(*widget) : 0);
  return 1;
}

int l_set_int(lua_State* state) {
  overlay::ui::Widget* widget = find_widget(luaL_checkstring(state, 1));
  if (widget) {
    if (int* target = std::get_if<int>(&widget->value)) *target = static_cast<int>(luaL_checkinteger(state, 2));
  }
  return 0;
}

int l_get_text(lua_State* state) {
  overlay::ui::Widget* widget = find_widget(luaL_checkstring(state, 1));
  lua_pushstring(state, widget ? widget_string(*widget).c_str() : "");
  return 1;
}

int l_set_text(lua_State* state) {
  overlay::ui::Widget* widget = find_widget(luaL_checkstring(state, 1));
  if (widget) {
    if (std::string* target = std::get_if<std::string>(&widget->value)) *target = luaL_checkstring(state, 2);
  }
  return 0;
}

int l_get_key(lua_State* state) {
  overlay::ui::Widget* widget = find_widget(luaL_checkstring(state, 1));
  lua_pushinteger(state, widget ? widget_key(*widget).code : 0);
  return 1;
}

int l_set_key(lua_State* state) {
  overlay::ui::Widget* widget = find_widget(luaL_checkstring(state, 1));
  if (widget) {
    if (overlay::ui::KeyCombo* target = std::get_if<overlay::ui::KeyCombo>(&widget->value))
      target->code = static_cast<int>(luaL_checkinteger(state, 2));
  }
  return 0;
}

int l_get_color(lua_State* state) {
  overlay::ui::Widget* widget = find_widget(luaL_checkstring(state, 1));
  push_color(state, widget ? widget_color(*widget) : overlay::ui::Color{0, 0, 0, 1});
  return 1;
}

int l_set_color(lua_State* state) {
  overlay::ui::Widget* widget = find_widget(luaL_checkstring(state, 1));
  overlay::ui::Color color;
  if (!color_arg(state, 2, color)) luaL_error(state, "expected a color");
  if (widget) {
    if (overlay::ui::Color* target = std::get_if<overlay::ui::Color>(&widget->value)) *target = color;
  }
  return 0;
}

// ---- painters -------------------------------------------------------

int l_set_painter(lua_State* state) {
  const char* id = luaL_checkstring(state, 1);
  luaL_checktype(state, 2, LUA_TFUNCTION);
  overlay::ui::Widget* widget = find_widget(id);
  if (!widget) luaL_error(state, "unknown widget id '%s'", id);
  lua_pushvalue(state, 2);
  const int ref = lua_ref(state, -1); lua_pop(state, 1);
  g_painter_refs.push_back(ref);
  widget->painter = [ref](const overlay::ui::Widget& painted, overlay::ui::Canvas& canvas) {
    lua_State* host = g_state;
    lua_rawgeti(host, LUA_REGISTRYINDEX, ref);
    push_widget_state(host, painted);
    auto* lua_canvas = static_cast<LuaCanvas*>(lua_newuserdatataggedwithmetatable(host, sizeof(LuaCanvas), kCanvasTag));
    lua_canvas->canvas = &canvas;
    begin_budget();
    if (lua_pcall(host, 2, 0, 0) != LUA_OK) {
      log("script: painter error: %s", lua_tostring(host, -1));
      lua_pop(host, 1);
    }
  };
  return 0;
}

int l_clear_painter(lua_State* state) {
  overlay::ui::Widget* widget = find_widget(luaL_checkstring(state, 1));
  if (widget) widget->painter = nullptr;
  return 0;
}

// ---- misc -----------------------------------------------------------

int l_notify(lua_State* state) {
  overlay::ui::notify(luaL_checkstring(state, 1));
  return 0;
}

int l_event(lua_State* state) {
  overlay::ui::emit_event(luaL_checkstring(state, 1));
  return 0;
}

int l_save_config(lua_State* state) {
  lua_pushboolean(state, overlay::config::save(luaL_checkstring(state, 1)) ? 1 : 0);
  return 1;
}

int l_load_config(lua_State* state) {
  lua_pushboolean(state, overlay::config::load(luaL_checkstring(state, 1)) ? 1 : 0);
  return 1;
}

// ---- scheduling -----------------------------------------------------

void schedule(lua_State* thread, int ref, double wake) { g_sleepers.push_back({thread, ref, wake, 0}); }

int l_wait(lua_State* state) {
  const double seconds = luaL_optnumber(state, 1, 0.0);
  lua_pushnumber(state, seconds);
  return lua_yield(state, 1);
}

// The spawn prelude runs as the thread's main chunk; the function and its
// arguments arrive as a table in the first argument slot.
int spawn_common(lua_State* state, double delay) {
  const int nargs = lua_gettop(state);
  luaL_checktype(state, 1, LUA_TFUNCTION);
  lua_createtable(state, nargs, 0);
  for (int i = 1; i <= nargs; ++i) {
    lua_pushvalue(state, i);
    lua_rawseti(state, -2, i);
  }
  const int args_ref = lua_ref(state, -1);
  lua_pop(state, 1);
  lua_State* thread = lua_newthread(state);
  const int ref = lua_ref(state, -1);
  lua_pop(state, 1);
  luaL_sandboxthread(thread);
  if (g_spawn_bytecode) luau_load(thread, "task", g_spawn_bytecode, g_spawn_size, 0);
  lua_rawgeti(thread, LUA_REGISTRYINDEX, args_ref);
  lua_unref(state, args_ref);
  g_sleepers.push_back({thread, ref, g_last_now + delay, 1});
  return 0;
}

int l_spawn(lua_State* state) { return spawn_common(state, 0.0); }
int l_defer(lua_State* state) { return spawn_common(state, 0.0); }

int l_delay(lua_State* state) {
  const double seconds = luaL_checknumber(state, 1);
  lua_remove(state, 1);
  return spawn_common(state, seconds);
}

int l_on_click(lua_State* state) {
  const char* id = luaL_checkstring(state, 1);
  luaL_checktype(state, 2, LUA_TFUNCTION);
  auto existing = g_click_refs.find(id);
  if (existing != g_click_refs.end()) lua_unref(g_state, existing->second);
  lua_pushvalue(state, 2);
  g_click_refs[id] = lua_ref(state, -1); lua_pop(state, 1);
  return 0;
}

int l_print(lua_State* state) {
  std::string line;
  const int count = lua_gettop(state);
  for (int i = 1; i <= count; ++i) {
    if (i > 1) line += ' ';
    if (lua_isstring(state, i))
      line += lua_tostring(state, i);
    else
      line += luaL_typename(state, i);
  }
  log("script: %s", line.c_str());
  return 0;
}

// ---- scoped filesystem ---------------------------------------------

bool safe_name(const char* name) {
  if (!name || !*name) return false;
  size_t length = 0;
  for (const char* c = name; *c; ++c, ++length) {
    const bool ok = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '_' ||
                    *c == '-' || *c == '.';
    if (!ok) return false;
    if (c[0] == '.' && c[1] == '.') return false;
    if (length > 64) return false;
  }
  return true;
}

std::wstring fs_path(const char* name) {
  std::wstring wide(name, name + std::strlen(name));
  return module_directory() + L"configs\\" + wide;
}

int l_fs_read(lua_State* state) {
  const char* name = luaL_checkstring(state, 1);
  if (!safe_name(name)) luaL_error(state, "invalid file name");
  FILE* file = _wfopen(fs_path(name).c_str(), L"rb");
  if (!file) {
    lua_pushnil(state);
    return 1;
  }
  std::fseek(file, 0, SEEK_END);
  const long size = std::ftell(file);
  std::fseek(file, 0, SEEK_SET);
  if (size < 0 || size > 1024 * 1024) {
    std::fclose(file);
    luaL_error(state, "file too large");
  }
  std::string text(static_cast<size_t>(size), '\0');
  const size_t read = size > 0 ? std::fread(text.data(), 1, text.size(), file) : 0;
  std::fclose(file);
  if (read != text.size()) luaL_error(state, "read failed");
  lua_pushlstring(state, text.data(), text.size());
  return 1;
}

int l_fs_write(lua_State* state) {
  const char* name = luaL_checkstring(state, 1);
  size_t length = 0;
  const char* text = luaL_checklstring(state, 2, &length);
  if (!safe_name(name)) luaL_error(state, "invalid file name");
  FILE* file = _wfopen(fs_path(name).c_str(), L"wb");
  if (!file) luaL_error(state, "write failed");
  const size_t written = std::fwrite(text, 1, length, file);
  std::fclose(file);
  if (written != length) luaL_error(state, "write failed");
  return 0;
}

// ---- Drawing API (ESP-style primitives) -----------------------------

enum class DrawKind {
  Line,
  Text,
  Circle,
  Square,
  Triangle,
  Image,
};

struct DrawingObject {
  DrawKind kind = DrawKind::Line;
  bool visible = false;
  float zindex = 0.0f;
  float transparency = 0.0f;
  overlay::ui::Color color{1.0f, 1.0f, 1.0f, 1.0f};
  overlay::ui::Vec2 from{}, to{}, point_a{}, point_b{}, point_c{}, position{}, size{};
  float radius = 10.0f;
  float thickness = 1.0f;
  float text_size = 16.0f;
  bool filled = false;
  bool center = true;
  bool outline = false;
  std::string text;
  std::string data;
};
std::vector<DrawingObject*> g_drawings;

float clampf(float value, float lo, float hi) { return value < lo ? lo : (value > hi ? hi : value); }

bool table_vec2(lua_State* state, int index, overlay::ui::Vec2& out) {
  if (!lua_istable(state, index)) return false;
  lua_getfield(state, index, "X");
  out.x = static_cast<float>(lua_tonumber(state, -1));
  lua_pop(state, 1);
  lua_getfield(state, index, "Y");
  out.y = static_cast<float>(lua_tonumber(state, -1));
  lua_pop(state, 1);
  return true;
}

bool value_color(lua_State* state, int index, overlay::ui::Color& out) {
  if (lua_isstring(state, index)) {
    out.a = 1.0f;
    return parse_hex_color(lua_tostring(state, index), out);
  }
  if (lua_istable(state, index)) {
    lua_getfield(state, index, "R");
    out.r = static_cast<float>(lua_tonumber(state, -1));
    lua_pop(state, 1);
    lua_getfield(state, index, "G");
    out.g = static_cast<float>(lua_tonumber(state, -1));
    lua_pop(state, 1);
    lua_getfield(state, index, "B");
    out.b = static_cast<float>(lua_tonumber(state, -1));
    lua_pop(state, 1);
    out.a = 1.0f;
    if (out.r > 1.0f || out.g > 1.0f || out.b > 1.0f) {
      out.r /= 255.0f;
      out.g /= 255.0f;
      out.b /= 255.0f;
    }
    return true;
  }
  return false;
}

void push_vec2(lua_State* state, const overlay::ui::Vec2& value) {
  lua_createtable(state, 0, 2);
  lua_pushnumber(state, value.x);
  lua_setfield(state, -2, "X");
  lua_pushnumber(state, value.y);
  lua_setfield(state, -2, "Y");
}

void push_color3(lua_State* state, const overlay::ui::Color& value) {
  lua_createtable(state, 0, 3);
  lua_pushnumber(state, value.r);
  lua_setfield(state, -2, "R");
  lua_pushnumber(state, value.g);
  lua_setfield(state, -2, "G");
  lua_pushnumber(state, value.b);
  lua_setfield(state, -2, "B");
}

int l_drawing_new(lua_State* state) {
  const char* kind = luaL_checkstring(state, 1);
  DrawKind parsed;
  if (std::strcmp(kind, "Line") == 0)
    parsed = DrawKind::Line;
  else if (std::strcmp(kind, "Text") == 0)
    parsed = DrawKind::Text;
  else if (std::strcmp(kind, "Circle") == 0)
    parsed = DrawKind::Circle;
  else if (std::strcmp(kind, "Square") == 0)
    parsed = DrawKind::Square;
  else if (std::strcmp(kind, "Triangle") == 0)
    parsed = DrawKind::Triangle;
  else if (std::strcmp(kind, "Image") == 0)
    parsed = DrawKind::Image;
  else
    luaL_error(state, "Drawing: unknown kind '%s'", kind);

  auto* object = static_cast<DrawingObject*>(
      lua_newuserdatataggedwithmetatable(state, sizeof(DrawingObject), kDrawingTag));
  new (object) DrawingObject();
  object->kind = parsed;
  g_drawings.push_back(object);
  return 1;
}

int l_drawing_remove(lua_State* state) {
  auto* object = static_cast<DrawingObject*>(lua_touserdata(state, 1));
  if (object) {
    const auto found = std::find(g_drawings.begin(), g_drawings.end(), object);
    if (found != g_drawings.end()) g_drawings.erase(found);
    object->visible = false;
  }
  return 0;
}

int l_drawing_gc(lua_State* state) {
  auto* object = static_cast<DrawingObject*>(lua_touserdata(state, 1));
  if (object) {
    const auto found = std::find(g_drawings.begin(), g_drawings.end(), object);
    if (found != g_drawings.end()) g_drawings.erase(found);
    object->~DrawingObject();
  }
  return 0;
}

int l_drawing_index(lua_State* state) {
  auto* object = static_cast<DrawingObject*>(lua_touserdata(state, 1));
  if (!object) {
    lua_pushnil(state);
    return 1;
  }
  const char* key = luaL_checkstring(state, 2);
  if (std::strcmp(key, "Remove") == 0 || std::strcmp(key, "Destroy") == 0) {
    lua_pushcfunction(state, l_drawing_remove, "Remove");
  } else if (std::strcmp(key, "Visible") == 0) {
    lua_pushboolean(state, object->visible ? 1 : 0);
  } else if (std::strcmp(key, "ZIndex") == 0) {
    lua_pushnumber(state, object->zindex);
  } else if (std::strcmp(key, "Transparency") == 0) {
    lua_pushnumber(state, object->transparency);
  } else if (std::strcmp(key, "Color") == 0) {
    push_color3(state, object->color);
  } else if (std::strcmp(key, "Thickness") == 0) {
    lua_pushnumber(state, object->thickness);
  } else if (std::strcmp(key, "Radius") == 0) {
    lua_pushnumber(state, object->radius);
  } else if (std::strcmp(key, "Filled") == 0) {
    lua_pushboolean(state, object->filled ? 1 : 0);
  } else if (std::strcmp(key, "Center") == 0) {
    lua_pushboolean(state, object->center ? 1 : 0);
  } else if (std::strcmp(key, "Outline") == 0) {
    lua_pushboolean(state, object->outline ? 1 : 0);
  } else if (std::strcmp(key, "Text") == 0) {
    lua_pushstring(state, object->text.c_str());
  } else if (std::strcmp(key, "Data") == 0) {
    lua_pushstring(state, object->data.c_str());
  } else if (std::strcmp(key, "Size") == 0) {
    if (object->kind == DrawKind::Text)
      lua_pushnumber(state, object->text_size);
    else
      push_vec2(state, object->size);
  } else if (std::strcmp(key, "Position") == 0) {
    push_vec2(state, object->position);
  } else if (std::strcmp(key, "From") == 0) {
    push_vec2(state, object->from);
  } else if (std::strcmp(key, "To") == 0) {
    push_vec2(state, object->to);
  } else if (std::strcmp(key, "PointA") == 0) {
    push_vec2(state, object->point_a);
  } else if (std::strcmp(key, "PointB") == 0) {
    push_vec2(state, object->point_b);
  } else if (std::strcmp(key, "PointC") == 0) {
    push_vec2(state, object->point_c);
  } else {
    lua_pushnil(state);
  }
  return 1;
}

int l_drawing_newindex(lua_State* state) {
  auto* object = static_cast<DrawingObject*>(lua_touserdata(state, 1));
  if (!object) return 0;
  const char* key = luaL_checkstring(state, 2);
  if (std::strcmp(key, "Visible") == 0) {
    object->visible = lua_toboolean(state, 3) != 0;
  } else if (std::strcmp(key, "ZIndex") == 0) {
    object->zindex = static_cast<float>(luaL_checknumber(state, 3));
  } else if (std::strcmp(key, "Transparency") == 0) {
    object->transparency = static_cast<float>(luaL_checknumber(state, 3));
  } else if (std::strcmp(key, "Color") == 0) {
    overlay::ui::Color color;
    if (!value_color(state, 3, color)) luaL_error(state, "Drawing: expected a Color3 or hex string");
    color.a = object->color.a;
    object->color = color;
  } else if (std::strcmp(key, "Thickness") == 0) {
    object->thickness = static_cast<float>(luaL_checknumber(state, 3));
  } else if (std::strcmp(key, "Radius") == 0) {
    object->radius = static_cast<float>(luaL_checknumber(state, 3));
  } else if (std::strcmp(key, "Filled") == 0) {
    object->filled = lua_toboolean(state, 3) != 0;
  } else if (std::strcmp(key, "Center") == 0) {
    object->center = lua_toboolean(state, 3) != 0;
  } else if (std::strcmp(key, "Outline") == 0) {
    object->outline = lua_toboolean(state, 3) != 0;
  } else if (std::strcmp(key, "Text") == 0) {
    object->text = luaL_checkstring(state, 3);
  } else if (std::strcmp(key, "Data") == 0) {
    object->data = luaL_checkstring(state, 3);
  } else if (std::strcmp(key, "Size") == 0) {
    if (object->kind == DrawKind::Text)
      object->text_size = static_cast<float>(luaL_checknumber(state, 3));
    else if (!table_vec2(state, 3, object->size))
      luaL_error(state, "Drawing: Size expects a Vector2");
  } else if (std::strcmp(key, "Position") == 0) {
    if (!table_vec2(state, 3, object->position)) luaL_error(state, "Drawing: Position expects a Vector2");
  } else if (std::strcmp(key, "From") == 0) {
    if (!table_vec2(state, 3, object->from)) luaL_error(state, "Drawing: From expects a Vector2");
  } else if (std::strcmp(key, "To") == 0) {
    if (!table_vec2(state, 3, object->to)) luaL_error(state, "Drawing: To expects a Vector2");
  } else if (std::strcmp(key, "PointA") == 0) {
    if (!table_vec2(state, 3, object->point_a)) luaL_error(state, "Drawing: PointA expects a Vector2");
  } else if (std::strcmp(key, "PointB") == 0) {
    if (!table_vec2(state, 3, object->point_b)) luaL_error(state, "Drawing: PointB expects a Vector2");
  } else if (std::strcmp(key, "PointC") == 0) {
    if (!table_vec2(state, 3, object->point_c)) luaL_error(state, "Drawing: PointC expects a Vector2");
  }
  return 0;
}

// ---- reload / execution ---------------------------------------------

void unref_painters() {
  for (int ref : g_painter_refs) lua_unref(g_state, ref);
  g_painter_refs.clear();
}

void drop_sleepers() {
  for (const Sleeper& sleeper : g_sleepers) lua_unref(g_state, sleeper.ref);
  g_sleepers.clear();
}

void clear_menu() {
  overlay::ui::menu().pages.clear();
  overlay::ui::menu().current_page = 0;
  g_drawings.clear();
  unref_painters();
  drop_sleepers();
}

bool resume_thread(lua_State* thread, int ref, double now, int nargs) {
  begin_budget();
  const int status = lua_resume(thread, nullptr, nargs);
  if (status == LUA_YIELD) {
    double wait = 0.0;
    if (lua_gettop(thread) > 0 && lua_isnumber(thread, -1)) wait = lua_tonumber(thread, -1);
    lua_settop(thread, 0);
    schedule(thread, ref, now + (wait < 0.0 ? 0.0 : wait));
    return true;
  }
  if (status != LUA_OK) {
    log("script: error: %s", lua_tostring(thread, -1));
    lua_settop(thread, 0);
    lua_unref(g_state, ref);
    return false;
  }
  lua_settop(thread, 0);
  lua_unref(g_state, ref);
  return true;
}

bool run_source(const char* source, const char* chunk_name, double now) {
  size_t bytecode_size = 0;
  char* bytecode = luau_compile(source, std::strlen(source), nullptr, &bytecode_size);
  if (!bytecode) {
    log("script: compile failed for %s", chunk_name);
    return false;
  }
  lua_State* thread = lua_newthread(g_state);
  const int ref = lua_ref(g_state, -1); lua_pop(g_state, 1);
  luaL_sandboxthread(thread);
  const int load_status = luau_load(thread, chunk_name, bytecode, bytecode_size, 0);
  std::free(bytecode);
  if (load_status != 0) {
    log("script: load failed for %s: %s", chunk_name, lua_tostring(thread, -1));
    lua_settop(thread, 0);
    lua_unref(g_state, ref);
    return false;
  }
  return resume_thread(thread, ref, now, 0);
}

bool read_script_file(const std::wstring& path, std::string& out) {
  FILE* file = _wfopen(path.c_str(), L"rb");
  if (!file) return false;
  std::fseek(file, 0, SEEK_END);
  const long size = std::ftell(file);
  std::fseek(file, 0, SEEK_SET);
  if (size < 0 || size > 1024 * 1024) {
    std::fclose(file);
    return false;
  }
  out.resize(static_cast<size_t>(size));
  const size_t read = size > 0 ? std::fread(out.data(), 1, out.size(), file) : 0;
  std::fclose(file);
  return read == out.size();
}

unsigned long long scripts_stamp() {
  const std::wstring pattern = module_directory() + L"scripts\\*.lua";
  WIN32_FIND_DATAW data = {};
  HANDLE handle = FindFirstFileW(pattern.c_str(), &data);
  if (handle == INVALID_HANDLE_VALUE) return 0;
  unsigned long long stamp = 0;
  do {
    stamp += static_cast<unsigned long long>(data.ftLastWriteTime.dwLowDateTime) +
             (static_cast<unsigned long long>(data.ftLastWriteTime.dwHighDateTime) << 32) + data.nFileSizeLow;
  } while (FindNextFileW(handle, &data));
  FindClose(handle);
  return stamp;
}

void load_scripts(double now) {
  clear_menu();

  std::vector<std::wstring> files;
  const std::wstring pattern = module_directory() + L"scripts\\*.lua";
  WIN32_FIND_DATAW data = {};
  HANDLE handle = FindFirstFileW(pattern.c_str(), &data);
  if (handle != INVALID_HANDLE_VALUE) {
    do {
      files.push_back(module_directory() + L"scripts\\" + data.cFileName);
    } while (FindNextFileW(handle, &data));
    FindClose(handle);
  }
  std::sort(files.begin(), files.end());

  int loaded = 0;
  for (const std::wstring& path : files) {
    std::string source;
    if (!read_script_file(path, source)) continue;
    if (run_source(source.c_str(), "scripts", now)) ++loaded;
  }
  if (loaded > 0) log("script: loaded %d script(s)", loaded);
  g_scripts_stamp = scripts_stamp();
}

void run_click_callbacks() {
  if (g_click_refs.empty()) return;
  for (overlay::ui::Page& page : overlay::ui::menu().pages)
    for (overlay::ui::Section& section : page.sections)
      for (overlay::ui::Widget& widget : section.widgets) {
        if (widget.klass != overlay::ui::WidgetClass::Button || !widget.state.clicked) continue;
        const auto found = g_click_refs.find(widget.id);
        if (found == g_click_refs.end()) continue;
        lua_rawgeti(g_state, LUA_REGISTRYINDEX, found->second);
        begin_budget();
        if (lua_pcall(g_state, 0, 0, 0) != LUA_OK) {
          log("script: callback error: %s", lua_tostring(g_state, -1));
          lua_pop(g_state, 1);
        }
      }
}

}  // namespace

void start() {
  if (g_started) return;
  g_started = true;

  g_state = luaL_newstate();
  if (!g_state) {
    log("script: failed to create VM");
    return;
  }
  luaL_openlibs(g_state);
  lua_callbacks(g_state)->interrupt = interrupt;

  static const char kSpawnPrelude[] = "local t = ...\nt[1](table.unpack(t, 2, #t))";
  g_spawn_bytecode = luau_compile(kSpawnPrelude, sizeof(kSpawnPrelude) - 1, nullptr, &g_spawn_size);

  lua_pushcfunction(g_state, l_print, "l_print");
  lua_setglobal(g_state, "print");

  lua_newtable(g_state);
  lua_pushcfunction(g_state, l_wait, "l_wait");
  lua_setfield(g_state, -2, "wait");
  lua_pushcfunction(g_state, l_spawn, "l_spawn");
  lua_setfield(g_state, -2, "spawn");
  lua_pushcfunction(g_state, l_defer, "l_defer");
  lua_setfield(g_state, -2, "defer");
  lua_pushcfunction(g_state, l_delay, "l_delay");
  lua_setfield(g_state, -2, "delay");
  lua_setglobal(g_state, "task");

  luaL_newmetatable(g_state, "overlay.canvas");
  lua_pushvalue(g_state, -1);
  lua_setfield(g_state, -2, "__index");
  struct CanvasMethod {
    const char* name;
    lua_CFunction fn;
  };
  const CanvasMethod canvas_methods[] = {
      {"rect", l_canvas_rect},           {"outline", l_canvas_outline}, {"circle", l_canvas_circle},
      {"text", l_canvas_text},           {"text_centered", l_canvas_text_centered},
      {"measure", l_canvas_measure},     {"scale", l_canvas_scale},     {"clip_push", l_canvas_clip_push},
      {"clip_pop", l_canvas_clip_pop},
  };
  for (const CanvasMethod& method : canvas_methods) {
    lua_pushcfunction(g_state, method.fn, method.name);
    lua_setfield(g_state, -2, method.name);
  }
  lua_pushvalue(g_state, -1);
  lua_setuserdatametatable(g_state, kCanvasTag);
  lua_pop(g_state, 1);

  lua_newtable(g_state);
  struct UiMethod {
    const char* name;
    lua_CFunction fn;
  };
  const UiMethod ui_methods[] = {
      {"page", l_page},
      {"section", l_section},
      {"toggle", l_toggle},
      {"checkbox", l_checkbox},
      {"slider", l_slider},
      {"dropdown", l_dropdown},
      {"keybind", l_keybind},
      {"text_input", l_text_input},
      {"colorpicker", l_colorpicker},
      {"button", l_button},
      {"set_painter", l_set_painter},
      {"clear_painter", l_clear_painter},
      {"get_bool", l_get_bool},
      {"set_bool", l_set_bool},
      {"get_number", l_get_number},
      {"set_number", l_set_number},
      {"get_int", l_get_int},
      {"set_int", l_set_int},
      {"get_text", l_get_text},
      {"set_text", l_set_text},
      {"get_key", l_get_key},
      {"set_key", l_set_key},
      {"get_color", l_get_color},
      {"set_color", l_set_color},
      {"notify", l_notify},
      {"event", l_event},
      {"on_click", l_on_click},
      {"save_config", l_save_config},
      {"load_config", l_load_config},
  };
  for (const UiMethod& method : ui_methods) {
    lua_pushcfunction(g_state, method.fn, method.name);
    lua_setfield(g_state, -2, method.name);
  }
  lua_setglobal(g_state, "ui");

  lua_newtable(g_state);
  lua_pushcfunction(g_state, l_fs_read, "read");
  lua_setfield(g_state, -2, "read");
  lua_pushcfunction(g_state, l_fs_write, "write");
  lua_setfield(g_state, -2, "write");
  lua_setglobal(g_state, "fs");

  // Roblox-ish value types so Drawing scripts look like what users know.
  static const char kTypesPrelude[] =
      "Vector2 = {}\n"
      "function Vector2.new(x, y) return {X = x or 0, Y = y or 0} end\n"
      "Vector2.zero = {X = 0, Y = 0}\n"
      "Vector2.one = {X = 1, Y = 1}\n"
      "Color3 = {}\n"
      "function Color3.new(r, g, b) return {R = r or 0, G = g or 0, B = b or 0} end\n"
      "function Color3.fromRGB(r, g, b) return {R = (r or 0) / 255, G = (g or 0) / 255, B = (b or 0) / 255} end\n"
      "function Color3.fromHSV(h, s, v)\n"
      "  h = (h or 0) % 1; s = s or 0; v = v or 0\n"
      "  local i = math.floor(h * 6); local f = h * 6 - i\n"
      "  local p = v * (1 - s); local q = v * (1 - s * f); local t = v * (1 - s * (1 - f))\n"
      "  i = i % 6\n"
      "  if i == 0 then return {R = v, G = t, B = p} end\n"
      "  if i == 1 then return {R = q, G = v, B = p} end\n"
      "  if i == 2 then return {R = p, G = v, B = t} end\n"
      "  if i == 3 then return {R = p, G = q, B = v} end\n"
      "  if i == 4 then return {R = t, G = p, B = v} end\n"
      "  return {R = v, G = p, B = q}\n"
      "end\n";
  size_t types_size = 0;
  char* types_bytecode = luau_compile(kTypesPrelude, sizeof(kTypesPrelude) - 1, nullptr, &types_size);
  if (types_bytecode) {
    if (luau_load(g_state, "types", types_bytecode, types_size, 0) == 0) lua_pcall(g_state, 0, 0, 0);
    std::free(types_bytecode);
  }

  luaL_newmetatable(g_state, "overlay.drawing");
  lua_pushcfunction(g_state, l_drawing_index, "__index");
  lua_setfield(g_state, -2, "__index");
  lua_pushcfunction(g_state, l_drawing_newindex, "__newindex");
  lua_setfield(g_state, -2, "__newindex");
  lua_pushcfunction(g_state, l_drawing_gc, "__gc");
  lua_setfield(g_state, -2, "__gc");
  lua_pushvalue(g_state, -1);
  lua_setuserdatametatable(g_state, kDrawingTag);
  lua_pop(g_state, 1);

  lua_newtable(g_state);
  lua_pushcfunction(g_state, l_drawing_new, "new");
  lua_setfield(g_state, -2, "new");
  lua_setglobal(g_state, "Drawing");

  // Sandbox: freeze globals and remove anything that can reach outside the menu.
  const char* blocked[] = {"os",     "io",        "debug",      "dofile", "loadfile", "require",
                           "load",   "loadstring", "collectgarbage", "newproxy"};
  for (const char* name : blocked) {
    lua_pushnil(g_state);
    lua_setglobal(g_state, name);
  }
  luaL_sandbox(g_state);

  load_scripts(0.0);
}

void tick(double now) {
  if (!g_started) return;
  g_last_now = now;

  if (g_reload_pending || now >= g_reload_check_at) {
    const unsigned long long stamp = scripts_stamp();
    if (g_reload_pending || (stamp != g_scripts_stamp && stamp != 0)) {
      g_reload_pending = false;
      log("script: reloading");
      load_scripts(now);
    }
    g_reload_check_at = now + kReloadIntervalSeconds;
  }

  std::vector<Sleeper> due;
  for (size_t i = 0; i < g_sleepers.size();) {
    if (g_sleepers[i].wake <= now) {
      due.push_back(g_sleepers[i]);
      g_sleepers.erase(g_sleepers.begin() + static_cast<ptrdiff_t>(i));
    } else {
      ++i;
    }
  }
  for (const Sleeper& sleeper : due) resume_thread(sleeper.thread, sleeper.ref, now, sleeper.nargs);

  run_click_callbacks();
}

bool execute(const char* source, const char* chunk_name) {
  if (!g_state) start();
  if (!g_state) return false;
  clear_menu();
  return run_source(source, chunk_name ? chunk_name : "chunk", g_last_now);
}

void request_reload() { g_reload_pending = true; }

void set_texture_provider(void* (*provider)(const char*)) { g_texture_provider = provider; }

void render_drawing(overlay::ui::Canvas& canvas) {
  if (g_drawings.empty()) return;
  std::vector<DrawingObject*> ordered = g_drawings;
  std::stable_sort(ordered.begin(), ordered.end(),
                   [](const DrawingObject* a, const DrawingObject* b) { return a->zindex < b->zindex; });

  for (const DrawingObject* object : ordered) {
    if (!object->visible) continue;
    overlay::ui::Color color = object->color;
    color.a *= 1.0f - clampf(object->transparency, 0.0f, 1.0f);
    if (color.a <= 0.0f) continue;

    switch (object->kind) {
      case DrawKind::Line:
        canvas.stroke_line(object->from, object->to, color, object->thickness < 1.0f ? 1.0f : object->thickness);
        break;
      case DrawKind::Circle:
        if (object->filled)
          canvas.fill_circle(object->position, object->radius, color);
        else
          canvas.stroke_circle(object->position, object->radius, color, object->thickness);
        break;
      case DrawKind::Square: {
        const overlay::ui::Rect rect{object->position.x, object->position.y, object->size.x, object->size.y};
        if (object->filled)
          canvas.fill_rect(rect, color, 0.0f);
        else
          canvas.stroke_rect(rect, color, object->thickness, 0.0f);
        break;
      }
      case DrawKind::Triangle:
        if (object->filled) {
          canvas.fill_triangle(object->point_a, object->point_b, object->point_c, color);
        } else {
          const float thickness = object->thickness < 1.0f ? 1.0f : object->thickness;
          canvas.stroke_line(object->point_a, object->point_b, color, thickness);
          canvas.stroke_line(object->point_b, object->point_c, color, thickness);
          canvas.stroke_line(object->point_c, object->point_a, color, thickness);
        }
        break;
      case DrawKind::Text: {
        const overlay::ui::Vec2 measured = canvas.measure_text_sized(object->text.c_str(), object->text_size);
        overlay::ui::Vec2 position = object->position;
        if (object->center) {
          position.x -= measured.x * 0.5f;
          position.y -= measured.y * 0.5f;
        }
        if (object->outline) {
          const overlay::ui::Color outline{0.0f, 0.0f, 0.0f, color.a};
          canvas.text_sized({position.x - 1.0f, position.y}, outline, object->text_size, object->text.c_str());
          canvas.text_sized({position.x + 1.0f, position.y}, outline, object->text_size, object->text.c_str());
          canvas.text_sized({position.x, position.y - 1.0f}, outline, object->text_size, object->text.c_str());
          canvas.text_sized({position.x, position.y + 1.0f}, outline, object->text_size, object->text.c_str());
        }
        canvas.text_sized(position, color, object->text_size, object->text.c_str());
        break;
      }
      case DrawKind::Image: {
        if (!g_texture_provider || object->data.empty()) break;
        void* texture = g_texture_provider(object->data.c_str());
        if (!texture) break;
        canvas.image(texture, {object->position.x, object->position.y, object->size.x, object->size.y}, color);
        break;
      }
    }
  }
}

}  // namespace overlay::script
