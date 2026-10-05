#include "ui/menu.h"

#include <windows.h>

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

const char* kIconCrosshair = u8"\uf05b";
const char* kIconAntiAim = u8"\uf0e2";
const char* kIconMouse = u8"\uf8cc";
const char* kIconUser = u8"\uf007";
const char* kIconGlobe = u8"\uf0ac";
const char* kIconLayers = u8"\uf5fd";
const char* kIconTools = u8"\uf7d9";
const char* kIconCode = u8"\uf121";
const char* kIconGear = u8"\uf013";
const char* kIconSave = u8"\uf0c7";
const char* kIconComment = u8"\uf086";
const char* kIconSearch = u8"\uf002";

constexpr float kSidebarWidth = 224.0f;
constexpr float kTopbarHeight = 66.0f;
constexpr float kCardPadX = 18.0f;
constexpr float kCardPadY = 14.0f;
constexpr float kCardTitleTop = 44.0f;
constexpr float kRowHeight = 24.0f;
constexpr float kRowGap = 14.0f;
constexpr float kColumnGap = 18.0f;

ImFont* g_ui_font = nullptr;
ImFont* g_logo_font = nullptr;
float g_scale = 1.0f;
float g_requested_scale = 1.0f;
float g_menu_scale = 0.0f;
int g_frame = 0;
bool g_starter_checked = false;
bool g_dragging = false;
Vec2 g_drag_anchor;

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

const char* page_icon(const std::string& title) {
  struct Entry {
    const char* title;
    const char* icon;
  };
  static const Entry kTable[] = {
      {"Ragebot", kIconCrosshair}, {"Anti Aim", kIconAntiAim}, {"Legitbot", kIconMouse}, {"Players", kIconUser},
      {"World", kIconGlobe},       {"Inventory", kIconLayers}, {"Main", kIconTools},      {"Scripts", kIconCode},
      {"Configs", kIconGear},
  };
  for (const Entry& entry : kTable)
    if (title == entry.title) return entry.icon;
  return kIconTools;
}

Widget& scope_widget() {
  static Widget widget = [] {
    Widget value = make_dropdown("##scope", "", 0, {"Global", "Ragebot", "Legitbot"});
    value.show_label = false;
    value.show_gear = false;
    value.control_width = 290.0f;
    return value;
  }();
  return widget;
}

float card_height(size_t rows) {
  return px(2.0f * kCardPadY + 30.0f + static_cast<float>(rows) * kRowHeight +
            static_cast<float>(rows - 1) * kRowGap);
}

void layout_card(std::deque<Widget>& widgets, const Rect& card) {
  for (size_t i = 0; i < widgets.size(); ++i)
    widgets[i].rect = {card.x + px(kCardPadX), card.y + px(kCardTitleTop) + static_cast<float>(i) * px(kRowHeight + kRowGap),
                       card.w - px(2.0f * kCardPadX), px(kRowHeight)};
}

Rect content_rect() {
  const Rect& menu_rect = menu().rect;
  const float x = menu_rect.x + px(kSidebarWidth) + px(24.0f);
  const float y = menu_rect.y + px(kTopbarHeight) + px(4.0f);
  const float w = menu_rect.w - px(kSidebarWidth) - px(48.0f);
  const float h = menu_rect.h - px(kTopbarHeight) - px(8.0f);
  return {x, y, w, h};
}

void layout_starter() {
  Menu& m = menu();
  const Rect topbar{m.rect.x + px(kSidebarWidth), m.rect.y, m.rect.w - px(kSidebarWidth), px(kTopbarHeight)};
  scope_widget().rect = {topbar.x + px(24.0f) + px(118.0f) + px(16.0f), topbar.y + px(21.0f), px(290.0f), px(kRowHeight)};

  g_cards.clear();
  Page* page = current_page();
  if (!page) return;

  std::vector<Section*> sections;
  for (Section& section : page->sections)
    if (!section.widgets.empty()) sections.push_back(&section);
  if (sections.empty()) return;

  const Rect content = content_rect();
  const size_t left_count = (sections.size() + 1) / 2;
  const float left_width = (content.w - px(kColumnGap)) * 0.56f;
  const float right_width = content.w - px(kColumnGap) - left_width;
  float left_y = content.y;
  float right_y = content.y;

  for (size_t i = 0; i < sections.size(); ++i) {
    const bool left = i < left_count;
    const float x = left ? content.x : content.x + left_width + px(kColumnGap);
    const float width = left ? left_width : right_width;
    const float height = card_height(sections[i]->widgets.size());
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

void paint_sidebar(Canvas& canvas, Input& input) {
  Menu& m = menu();
  const float sidebar_width = px(kSidebarWidth);

  canvas.text_sized({m.rect.x + px(14.0f), m.rect.y + px(22.0f)}, g_chrome.text, px(24.0f), "stupidnig");
  const Vec2 logo = canvas.measure_text_sized("stupidnig", px(24.0f));
  canvas.text_sized({m.rect.x + px(14.0f) + logo.x + px(7.0f), m.rect.y + px(31.0f)}, g_chrome.dim, px(14.0f), "CS2");

  float y = m.rect.y + px(68.0f);
  const char* last_section = nullptr;
  for (size_t i = 0; i < m.pages.size(); ++i) {
    Page& page = m.pages[i];
    if (!last_section || page.nav_section != last_section) {
      last_section = page.nav_section.c_str();
      y += px(14.0f);
      canvas.text({m.rect.x + px(14.0f), y}, g_chrome.dim, page.nav_section.c_str());
      y += px(18.0f);
    }
    const Rect row{m.rect.x + px(8.0f), y, sidebar_width - px(16.0f), px(36.0f)};
    const bool selected = static_cast<int>(i) == m.current_page;
    const bool hovered = row.contains(input.mouse);
    if (selected || hovered) {
      canvas.fill_rect(row, selected ? g_chrome.selected : g_chrome.hover, px(3.0f));
      if (selected) canvas.fill_rect({row.x, row.y, px(2.0f), row.h}, g_chrome.accent);
    }
    const char* icon = page_icon(page.title);
    const Vec2 icon_size = canvas.measure_text(icon);
    canvas.text({row.x + px(12.0f), row.y + (row.h - icon_size.y) * 0.5f}, selected ? g_chrome.text : g_chrome.dim, icon);
    const Vec2 label = canvas.measure_text(page.title.c_str());
    canvas.text({row.x + px(12.0f) + icon_size.x + px(12.0f), row.y + (row.h - label.y) * 0.5f},
                selected ? g_chrome.text : g_chrome.dim, page.title.c_str());
    if (hovered && input.mouse_clicked && !input.click_consumed) {
      m.current_page = static_cast<int>(i);
      input.click_consumed = true;
    }
    y += px(36.0f) + px(2.0f);
  }
}

void paint_profile(Canvas& canvas) {
  const Rect& menu_rect = menu().rect;
  const Vec2 base{menu_rect.x + px(16.0f), menu_rect.y + menu_rect.h - px(80.0f)};

  canvas.fill_circle({base.x + px(19.0f), base.y + px(19.0f)}, px(19.0f), g_chrome.accent);
  const Vec2 initial = canvas.measure_text_sized("L", px(18.0f));
  canvas.text_sized({base.x + px(19.0f) - initial.x * 0.5f, base.y + px(19.0f) - initial.y * 0.5f}, g_chrome.text,
                    px(18.0f), "L");

  canvas.text({base.x + px(48.0f), base.y + px(4.0f)}, g_chrome.text, "legendiffx");
  const Vec2 name = canvas.measure_text("legendiffx");
  const Rect badge{base.x + px(48.0f) + name.x + px(8.0f), base.y + px(3.0f), px(44.0f), px(17.0f)};
  canvas.stroke_rect(badge, g_chrome.border, 1.0f, px(2.0f));
  canvas.text_sized({badge.x + px(5.0f), badge.y + px(4.0f)}, g_chrome.dim, px(11.0f), "ADMIN");

  std::time_t now = std::time(nullptr);
  std::tm* local = std::localtime(&now);
  char time_text[64] = {};
  if (local) std::strftime(time_text, sizeof(time_text), "%d.%m  %H:%M", local);
  canvas.text({base.x + px(48.0f), base.y + px(26.0f)}, g_chrome.dim, time_text);
}

void paint_topbar(Canvas& canvas, Input& input) {
  const Rect& menu_rect = menu().rect;
  const Rect topbar{menu_rect.x + px(kSidebarWidth), menu_rect.y, menu_rect.w - px(kSidebarWidth), px(kTopbarHeight)};

  const Rect save{topbar.x + px(24.0f), topbar.y + px(14.0f), px(118.0f), px(38.0f)};
  const bool hovered = save.contains(input.mouse);
  canvas.fill_rect(save, hovered ? g_chrome.button_hover : g_chrome.button, px(3.0f));
  canvas.stroke_rect(save, g_chrome.border, 1.0f, px(3.0f));
  const Vec2 icon = canvas.measure_text(kIconSave);
  const Vec2 label = canvas.measure_text("Save");
  float x = save.x + (save.w - (icon.x + px(10.0f) + label.x)) * 0.5f;
  canvas.text({x, save.y + (save.h - icon.y) * 0.5f}, g_chrome.text, kIconSave);
  x += icon.x + px(10.0f);
  canvas.text({x, save.y + (save.h - label.y) * 0.5f}, g_chrome.text, "Save");
  if (hovered && input.mouse_clicked && !input.click_consumed) input.click_consumed = true;

  const float icons_right = topbar.right() - px(24.0f);
  const char* icons[] = {kIconComment, kIconGear, kIconSearch};
  for (int i = 0; i < 3; ++i) {
    const Rect box{icons_right - px(32.0f) * static_cast<float>(3 - i) - px(6.0f) * static_cast<float>(2 - i),
                   topbar.y + px(17.0f), px(32.0f), px(32.0f)};
    const bool icon_hovered = box.contains(input.mouse);
    const Vec2 size = canvas.measure_text(icons[i]);
    canvas.text({box.x + (box.w - size.x) * 0.5f, box.y + (box.h - size.y) * 0.5f}, icon_hovered ? g_chrome.text : g_chrome.dim,
                icons[i]);
  }
}

void paint_cards(Canvas& canvas) {
  for (const CardView& card : g_cards) {
    canvas.fill_rect(card.rect, g_chrome.panel, px(3.0f));
    canvas.stroke_rect(card.rect, g_chrome.border, 1.0f, px(3.0f));
    canvas.text({card.rect.x + px(kCardPadX), card.rect.y + px(kCardPadY)}, g_chrome.text, card.section->title.c_str());
    canvas.fill_rect({card.rect.x + px(kCardPadX), card.rect.y + px(37.0f), card.rect.w - px(2.0f * kCardPadX), 1.0f},
                     g_chrome.border);
    const std::deque<Widget>& widgets = card.section->widgets;
    for (size_t i = 0; i + 1 < widgets.size(); ++i)
      canvas.fill_rect({card.rect.x + px(kCardPadX), widgets[i].rect.bottom() + px(kRowGap) * 0.5f,
                        card.rect.w - px(2.0f * kCardPadX), 1.0f},
                       g_chrome.border);
  }
}

void paint_placeholder(Canvas& canvas) {
  Page* page = current_page();
  const Rect content = content_rect();
  canvas.text({content.x, content.y + px(20.0f)}, g_chrome.text, page ? page->title.c_str() : "Overlay");
  canvas.text({content.x, content.y + px(48.0f)}, g_chrome.dim,
              menu().chrome_painter ? "No sections on this page." : "This page arrives with the Script Host.");
}

void paint_default_chrome(Canvas& canvas, Input& input) {
  canvas.fill_rect(menu().rect, g_chrome.window, px(4.0f));
  canvas.stroke_rect(menu().rect, g_chrome.border, 1.0f, px(4.0f));
  paint_sidebar(canvas, input);
  paint_topbar(canvas, input);
  paint_cards(canvas);
  if (g_cards.empty()) paint_placeholder(canvas);
}

void handle_starter_config_actions() {
  Page* page = current_page();
  if (!page || page->title != "Configs") return;
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
  Widget& scope = scope_widget();
  if (scope.rect.w > 0.0f) fn(scope, user);
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
  float fit = viewport->WorkSize.x * 0.96f / 1230.0f;
  const float fit_height = viewport->WorkSize.y * 0.94f / 590.0f;
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
  if (!g_starter_checked) {
    if (m.pages.empty() && !m.chrome_painter) build_starter_menu(m);
    g_starter_checked = true;
  }

  if (m.rect.w <= 0.0f || g_menu_scale != g_scale) {
    ImVec2 size(viewport->WorkSize.x * 0.96f, viewport->WorkSize.y * 0.94f);
    const float cap_width = px(1260.0f);
    const float cap_height = px(800.0f);
    if (size.x > cap_width) size.x = cap_width;
    if (size.y > cap_height) size.y = cap_height;
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
    layout_starter();
  }

  update_active_widgets(input);
  resolve_focus(input);
  handle_starter_config_actions();

  if (!blank) paint_default_chrome(*canvas, input);

  paint_active_widgets(*canvas);
  if (!blank) paint_profile(*canvas);
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
