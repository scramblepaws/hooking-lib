#include "ui/menu.h"

#include <windows.h>

#include <cctype>
#include <cstdio>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include "imgui.h"

#include "c_api/bridge.h"
#include "core/config.h"
#include "core/paths.h"
#include "core/state.h"
#include "script/script.h"
#include "ui/canvas.h"
#include "ui/menu_model.h"
#include "ui/notifications.h"
#include "ui/theme.h"
#include "ui/widget.h"

namespace overlay::ui {

namespace {

const char* kIconChevron = u8"\uf0d7";

constexpr float kHeaderHeight = 64.0f;
constexpr float kCardPadX = 16.0f;
constexpr float kCardTitleTop = 46.0f;
constexpr float kRowHeight = 24.0f;
constexpr float kRowGap = 14.0f;
constexpr float kColumnGap = 18.0f;

ImFont* g_ui_font = nullptr;
ImFont* g_logo_font = nullptr;
float g_scale = 1.0f;
float g_requested_scale = 1.0f;
float g_menu_scale = 0.0f;
int g_frame = 0;
bool g_dragging = false;
Vec2 g_drag_anchor;

// Tab transition: the active-tab box slides from the previous tab and the page
// content nudges in from the direction of travel.
float g_page_anim = 1.0f;
int g_anim_page = -1;
float g_page_dir = 1.0f;
bool g_pill_valid = false;
Rect g_pill_from{};
Rect g_pill_to{};

float ease_out(float t) { return t * t * (3.0f - 2.0f * t); }

struct CardView {
  Rect rect;
  const Section* section;
};
std::vector<CardView> g_cards;

std::string to_utf8(const std::wstring& text) {
  if (text.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
  return out;
}

float px(float value) { return value * g_scale; }

Color lighten(Color color, float amount) {
  color.r += amount;
  color.g += amount;
  color.b += amount;
  color.r = color.r > 1.0f ? 1.0f : color.r;
  color.g = color.g > 1.0f ? 1.0f : color.g;
  color.b = color.b > 1.0f ? 1.0f : color.b;
  return color;
}

struct ChromeColors {
  Color window, panel, border, text, dim, accent, hover, selected, button, button_hover;
};
ChromeColors g_chrome;

void refresh_chrome() {
  Theme& t = theme();
  g_chrome.window = theme_color("window-bg", t.window_bg);
  g_chrome.panel = theme_color("panel", t.panel);
  g_chrome.border = theme_color("border", t.border);
  g_chrome.text = theme_color("text", t.text);
  g_chrome.dim = theme_color("text-dim", t.text_dim);
  g_chrome.accent = theme_color("accent", t.accent);
  g_chrome.hover = theme_color("row-hover", t.row_hover);
  g_chrome.selected = theme_color("nav-selected", t.nav_selected);
  g_chrome.button = theme_color("button", t.button);
  g_chrome.button_hover = theme_color("button-hover", t.button_hover);
}

Page* current_page() {
  Menu& m = menu();
  if (m.pages.empty()) return nullptr;
  if (m.current_page < 0 || m.current_page >= static_cast<int>(m.pages.size())) m.current_page = 0;
  return &m.pages[static_cast<size_t>(m.current_page)];
}

float card_height(const std::deque<Widget>& widgets) {
  float rows = 0.0f;
  for (size_t i = 0; i < widgets.size(); ++i) {
    rows += widgets[i].height > 0.0f ? widgets[i].height : kRowHeight;
    if (i + 1 < widgets.size()) rows += kRowGap;
  }
  return px(kCardTitleTop + rows + 14.0f);
}

void layout_card(std::deque<Widget>& widgets, const Rect& card) {
  float y = card.y + px(kCardTitleTop);
  for (Widget& widget : widgets) {
    const float inset = px(kCardPadX + 14.0f * static_cast<float>(widget.indent));
    const float height = widget.height > 0.0f ? px(widget.height) : px(kRowHeight);
    widget.rect = {card.x + inset, y, card.w - inset - px(kCardPadX), height};
    y += height + px(kRowGap);
  }
}

Rect content_rect() {
  const Rect& menu_rect = menu().rect;
  const float x = menu_rect.x + px(24.0f);
  const float y = menu_rect.y + px(kHeaderHeight) + px(16.0f);
  const float w = menu_rect.w - px(48.0f);
  const float h = menu_rect.h - px(kHeaderHeight) - px(40.0f);
  return {x, y, w, h};
}

void layout_default() {
  g_cards.clear();
  Page* page = current_page();
  if (!page) return;

  std::vector<Section*> sections;
  for (Section& section : page->sections)
    if (!section.widgets.empty()) sections.push_back(&section);
  if (sections.empty()) return;

  const Rect content = content_rect();
  const float slide = (1.0f - ease_out(g_page_anim)) * px(16.0f) * g_page_dir;
  const size_t left_count = (sections.size() + 1) / 2;
  const float left_width = (content.w - px(kColumnGap)) * 0.60f;
  const float right_width = content.w - px(kColumnGap) - left_width;
  float left_y = content.y;
  float right_y = content.y;

  for (size_t i = 0; i < sections.size(); ++i) {
    const bool left = i < left_count;
    const float x = (left ? content.x : content.x + left_width + px(kColumnGap)) + slide;
    const float width = left ? left_width : right_width;
    const float height = card_height(sections[i]->widgets);
    const float y = left ? left_y : right_y;
    const Rect card{x, y, width, height};
    layout_card(sections[i]->widgets, card);
    g_cards.push_back({card, sections[i]});
    if (left)
      left_y += height + px(kColumnGap);
    else
      right_y += height + px(kColumnGap);
  }
}

void paint_widget(const Widget& widget, Canvas& canvas) {
  if (widget.c_painter) {
    OverlayCanvas wrapper{&canvas};
    widget.c_painter(widget.c_painter_user, &widget, &wrapper);
    return;
  }
  widget_paint(widget, canvas);
}

void paint_header(Canvas& canvas, Input& input) {
  Menu& m = menu();
  const Rect header{m.rect.x, m.rect.y, m.rect.w, px(kHeaderHeight)};
  const float center_y = header.y + header.h * 0.5f;

  const float brand_size = px(22.0f);
  const Vec2 brand = canvas.measure_text_sized(m.title.c_str(), brand_size);
  canvas.text_sized({header.x + px(24.0f), center_y - brand.y * 0.5f}, g_chrome.text, brand_size, m.title.c_str());

  // Profile, anchored right: avatar with an online dot, config name, chevron.
  const std::string name = config::active_name();
  const char initial_text[2] = {
      static_cast<char>(std::toupper(static_cast<unsigned char>(name.empty() ? 'o' : name[0]))), 0};
  const float avatar = px(34.0f);
  const Vec2 name_size = canvas.measure_text(name.c_str());
  const Vec2 chevron = canvas.measure_text(kIconChevron);
  const float profile_x = header.right() - px(24.0f) - (avatar + px(10.0f) + name_size.x + px(8.0f) + chevron.x);
  const Rect avatar_rect{profile_x, center_y - avatar * 0.5f, avatar, avatar};
  canvas.fill_rect(avatar_rect, g_chrome.button, px(8.0f));
  canvas.stroke_rect(avatar_rect, g_chrome.border, 1.0f, px(8.0f));
  const Vec2 initial = canvas.measure_text_sized(initial_text, px(16.0f));
  canvas.text_sized({avatar_rect.x + (avatar - initial.x) * 0.5f, avatar_rect.y + (avatar - initial.y) * 0.5f},
                    g_chrome.text, px(16.0f), initial_text);
  const Vec2 dot{avatar_rect.x + avatar - px(5.0f), avatar_rect.y + avatar - px(5.0f)};
  canvas.fill_circle(dot, px(5.5f), g_chrome.window);
  canvas.fill_circle(dot, px(4.0f), Color{0.30f, 0.85f, 0.50f, 1.0f});
  canvas.text({profile_x + avatar + px(10.0f), center_y - name_size.y * 0.5f}, g_chrome.text, name.c_str());
  canvas.text({profile_x + avatar + px(10.0f) + name_size.x + px(8.0f), center_y - chevron.y * 0.5f}, g_chrome.dim,
              kIconChevron);

  // Page tabs, right-aligned before the profile.
  float tabs_width = 0.0f;
  for (size_t i = 0; i < m.pages.size(); ++i)
    tabs_width += canvas.measure_text_sized(m.pages[i].title.c_str(), px(15.0f)).x + px(32.0f);
  float x = profile_x - px(16.0f) - tabs_width;
  const float min_x = header.x + px(24.0f) + brand.x + px(24.0f);
  if (x < min_x) x = min_x;  // ponytail: no overflow scrolling

  Rect tabs[32] = {};
  const size_t tab_count = m.pages.size() < 32 ? m.pages.size() : 32;
  for (size_t i = 0; i < tab_count; ++i) {
    const Vec2 label = canvas.measure_text_sized(m.pages[i].title.c_str(), px(15.0f));
    tabs[i] = {x, center_y - px(17.0f), label.x + px(28.0f), px(34.0f)};
    x += tabs[i].w + px(4.0f);
  }

  // Active-tab box: slides from the previous tab while the page transition runs.
  const int selected_index =
      m.current_page >= 0 && m.current_page < static_cast<int>(tab_count) ? m.current_page : -1;
  if (selected_index >= 0) {
    if (!g_pill_valid) {
      g_pill_from = tabs[selected_index];
      g_pill_to = tabs[selected_index];
      g_pill_valid = true;
    } else {
      g_pill_to = tabs[selected_index];
    }
    const float t = ease_out(g_page_anim);
    const Rect pill{g_pill_from.x + (g_pill_to.x - g_pill_from.x) * t,
                    g_pill_from.y + (g_pill_to.y - g_pill_from.y) * t,
                    g_pill_from.w + (g_pill_to.w - g_pill_from.w) * t,
                    g_pill_from.h + (g_pill_to.h - g_pill_from.h) * t};
    canvas.fill_rect(pill, g_chrome.selected, px(4.0f));
    canvas.stroke_rect(pill, lighten(g_chrome.border, 0.06f), 1.0f, px(4.0f));
  }

  for (size_t i = 0; i < tab_count; ++i) {
    const char* title = m.pages[i].title.c_str();
    const Vec2 label = canvas.measure_text_sized(title, px(15.0f));
    const bool selected = static_cast<int>(i) == m.current_page;
    const bool hovered = tabs[i].contains(input.mouse);
    if (hovered && !selected) canvas.fill_rect(tabs[i], g_chrome.hover, px(4.0f));
    canvas.text_sized({tabs[i].x + (tabs[i].w - label.x) * 0.5f, center_y - label.y * 0.5f},
                      selected ? g_chrome.text : g_chrome.dim, px(15.0f), title);
    if (hovered && input.mouse_clicked && !input.click_consumed) {
      m.current_page = static_cast<int>(i);
      input.click_consumed = true;
    }
  }

  canvas.fill_rect({header.x, header.bottom() - 1.0f, header.w, 1.0f}, g_chrome.border);
}

void paint_cards(Canvas& canvas) {
  for (const CardView& card : g_cards) {
    canvas.fill_rect(card.rect, g_chrome.panel, 0.0f);
    canvas.stroke_rect(card.rect, g_chrome.border, 1.0f, 0.0f);
    canvas.text_sized({card.rect.x + px(kCardPadX), card.rect.y + px(12.0f)}, g_chrome.text, px(15.0f),
                      card.section->title.c_str());
    const std::deque<Widget>& widgets = card.section->widgets;
    for (size_t i = 0; i + 1 < widgets.size(); ++i)
      canvas.fill_rect({card.rect.x, widgets[i].rect.bottom() + px(kRowGap) * 0.5f, card.rect.w, 1.0f},
                       g_chrome.border);
  }
}

void paint_placeholder(Canvas& canvas) {
  Page* page = current_page();
  const Rect content = content_rect();
  canvas.text({content.x, content.y + px(20.0f)}, g_chrome.text, page ? page->title.c_str() : "Overlay");
  canvas.text({content.x, content.y + px(48.0f)}, g_chrome.dim, "No content yet.");
}

void paint_default_chrome(Canvas& canvas, Input& input) {
  canvas.fill_rect(menu().rect, g_chrome.window, 0.0f);
  canvas.stroke_rect(menu().rect, g_chrome.border, 1.0f, 0.0f);
  if (menu().pages.empty()) {
    paint_placeholder(canvas);
    return;
  }
  paint_header(canvas, input);
  paint_cards(canvas);
  if (g_cards.empty()) paint_placeholder(canvas);
}

void handle_config_actions() {
  Page* page = current_page();
  if (!page) return;
  const std::string name = config::active_name();
  for (Section& section : page->sections) {
    for (Widget& widget : section.widgets) {
      if (widget.klass != WidgetClass::Button || !widget.state.clicked) continue;
      if (widget.id == "config_save") {
        if (config::save(name.c_str())) {
          const std::string message = "Saved config \"" + name + "\"";
          notify(message.c_str());
          emit_event(message.c_str());
        } else {
          notify("Save failed");
        }
      } else if (widget.id == "config_load") {
        if (config::load(name.c_str())) {
          const std::string message = "Loaded config \"" + name + "\"";
          notify(message.c_str());
          emit_event(message.c_str());
        } else {
          notify("Load failed");
        }
      }
    }
  }
}

void handle_drag(Input& input) {
  if (input.mouse_clicked && !input.click_consumed && menu().rect.contains(input.mouse)) {
    g_dragging = true;
    g_drag_anchor = {input.mouse.x - menu().rect.x, input.mouse.y - menu().rect.y};
    input.click_consumed = true;
  }
  if (g_dragging) {
    if (!input.mouse_down) {
      g_dragging = false;
    } else {
      menu().rect.x = input.mouse.x - g_drag_anchor.x;
      menu().rect.y = input.mouse.y - g_drag_anchor.y;
    }
  }
}

void for_each_active_widget(void (*fn)(Widget&, void*), void* user) {
  Page* page = current_page();
  if (page) {
    for (Section& section : page->sections)
      for (Widget& widget : section.widgets)
        if (widget.rect.w > 0.0f) fn(widget, user);
  }
}

void update_active_widgets(Input& input) {
  for_each_active_widget(
      [](Widget& widget, void* user) { widget_update(widget, *static_cast<Input*>(user)); }, &input);
}

void resolve_focus(Input& input) {
  for_each_active_widget(
      [](Widget& widget, void* user) {
        Input& in = *static_cast<Input*>(user);
        if (in.focus_target)
          widget.state.focused = &widget == in.focus_target;
        else if (in.mouse_clicked && !in.click_consumed)
          widget.state.focused = false;
      },
      &input);
}

void paint_active_widgets(Canvas& canvas) {
  for_each_active_widget(
      [](Widget& widget, void* user) { paint_widget(widget, *static_cast<Canvas*>(user)); }, &canvas);
}

void paint_active_popups(Canvas& canvas) {
  for_each_active_widget(
      [](Widget& widget, void* user) {
        const Widget& w = widget;
        widget_paint_popup(w, *static_cast<Canvas*>(user));
      },
      &canvas);
}

void apply_scale(float scale) {
  if (scale < 0.5f) scale = 0.5f;
  if (scale > 4.0f) scale = 4.0f;
  if (scale == g_scale) return;
  g_scale = scale;
  ImGui::GetStyle().FontSizeBase = 16.0f * scale;
}

void ensure_scale() {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  float fit = viewport->WorkSize.x * 0.96f / 900.0f;
  const float fit_height = viewport->WorkSize.y * 0.94f / 640.0f;
  if (fit_height < fit) fit = fit_height;
  apply_scale(g_requested_scale < fit ? g_requested_scale : fit);
}

}  // namespace

void set_scale(float scale) { g_requested_scale = scale; }

void save_active_config() {
  if (menu().pages.empty()) return;
  const std::string name = config::active_name();
  config::save(name.c_str());
}

void init(float scale) {
  ImGuiIO& io = ImGui::GetIO();
  const std::string fonts = to_utf8(module_directory() + L"assets\\fonts\\");
  g_ui_font = io.Fonts->AddFontFromFileTTF((fonts + "Roboto-Medium.ttf").c_str(), 16.0f);
  ImFontConfig merge;
  merge.MergeMode = true;
  static const ImWchar icon_range[] = {0xf000, 0xf8ff, 0};
  io.Fonts->AddFontFromFileTTF((fonts + "fa-solid-900.ttf").c_str(), 15.0f, &merge, icon_range);
  g_logo_font = io.Fonts->AddFontFromFileTTF((fonts + "Roboto-Medium.ttf").c_str(), 27.0f);
  g_requested_scale = scale;
  apply_scale(scale);
  style_load_file(module_directory() + L"assets\\theme.css");
}

void draw() {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ensure_scale();
  refresh_chrome();

  Menu& m = menu();

  const int page_now = static_cast<int>(m.current_page);
  if (page_now != g_anim_page) {
    g_page_dir = (g_anim_page >= 0 && page_now < g_anim_page) ? -1.0f : 1.0f;
    g_anim_page = page_now;
    g_page_anim = 0.0f;
  }
  if (g_page_anim < 1.0f) {
    g_page_anim += static_cast<float>(ImGui::GetIO().DeltaTime) * 9.0f;
    if (g_page_anim > 1.0f) g_page_anim = 1.0f;
  }

  if (m.rect.w <= 0.0f || g_menu_scale != g_scale) {
    // Fixed design size so the menu is identical in every Target; shrunk only
    // when the Target window cannot fit it.
    float size_x = px(900.0f);
    float size_y = px(640.0f);
    const float max_x = viewport->WorkSize.x * 0.96f;
    const float max_y = viewport->WorkSize.y * 0.94f;
    if (size_x > max_x) size_x = max_x;
    if (size_y > max_y) size_y = max_y;
    ImVec2 size(size_x, size_y);
    if (m.rect.w <= 0.0f) {
      m.rect = {viewport->WorkPos.x + (viewport->WorkSize.x - size.x) * 0.5f,
                viewport->WorkPos.y + (viewport->WorkSize.y - size.y) * 0.5f, size.x, size.y};
    } else {
      const float center_x = m.rect.x + m.rect.w * 0.5f;
      const float center_y = m.rect.y + m.rect.h * 0.5f;
      m.rect = {center_x - size.x * 0.5f, center_y - size.y * 0.5f, size.x, size.y};
    }
    g_menu_scale = g_scale;
  }

  std::unique_ptr<Canvas> canvas = make_canvas(ImGui::GetForegroundDrawList(), g_scale);

  Input input;
  const ImGuiIO& io = ImGui::GetIO();
  State& st = state();
  input.mouse = {io.MousePos.x, io.MousePos.y};
  input.mouse_clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  input.mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
  input.dt = io.DeltaTime;
  input.scale = g_scale;
  input.frame = g_frame++;
  input.time = static_cast<float>(ImGui::GetTime());
  input.bounds = m.rect;
  input.captured_key = st.captured_key.load();
  const int char_count = st.char_count.load();
  for (int i = 0; i < char_count && i < 64 && input.char_count < 32; ++i)
    input.chars[input.char_count++] = st.chars[i].load();

  const bool blank = m.chrome_painter != nullptr;
  if (blank) {
    OverlayCanvas wrapper{canvas.get()};
    m.chrome_painter(m.chrome_user, &wrapper, &m);
  } else {
    layout_default();
  }

  update_active_widgets(input);
  resolve_focus(input);
  handle_config_actions();

  if (!blank) paint_default_chrome(*canvas, input);

  paint_active_widgets(*canvas);
  paint_active_popups(*canvas);

  handle_drag(input);
  st.captured_key.store(0);
  st.char_count.store(0);
}

void draw_overlays() {
  ensure_scale();
  std::unique_ptr<Canvas> canvas = make_canvas(ImGui::GetForegroundDrawList(), g_scale);
  script::render_drawing(*canvas);
  paint_notifications(*canvas);
}

}  // namespace overlay::ui
