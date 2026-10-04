#include <chrono>
#include <cstdio>
#include <cstring>

#include "script/script.h"
#include "ui/canvas.h"
#include "ui/menu_model.h"
#include "ui/widget.h"

namespace {

int g_failures = 0;

void check(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    ++g_failures;
  }
}

overlay::ui::Widget* find_widget(const char* id) {
  for (overlay::ui::Page& page : overlay::ui::menu().pages)
    for (overlay::ui::Section& section : page.sections)
      for (overlay::ui::Widget& widget : section.widgets)
        if (widget.id == id) return &widget;
  return nullptr;
}

class MockCanvas : public overlay::ui::Canvas {
 public:
  float scale() const override { return 1.0f; }
  overlay::ui::Vec2 measure_text(const char*) const override { return {10.0f, 10.0f}; }
  overlay::ui::Vec2 measure_text_sized(const char*, float) const override { return {10.0f, 10.0f}; }
  void fill_rect(const overlay::ui::Rect&, overlay::ui::Color, float) override { rects++; }
  void stroke_rect(const overlay::ui::Rect&, overlay::ui::Color, float, float) override { rects++; }
  void fill_circle(overlay::ui::Vec2, float, overlay::ui::Color) override { circles++; }
  void stroke_circle(overlay::ui::Vec2, float, overlay::ui::Color, float) override { circles++; }
  void stroke_line(overlay::ui::Vec2, overlay::ui::Vec2, overlay::ui::Color, float) override { lines++; }
  void fill_triangle(overlay::ui::Vec2, overlay::ui::Vec2, overlay::ui::Vec2, overlay::ui::Color) override {
    triangles++;
  }
  void text(overlay::ui::Vec2, overlay::ui::Color, const char*) override { texts++; }
  void text_sized(overlay::ui::Vec2, overlay::ui::Color, float, const char*) override { texts++; }
  void text_centered(const overlay::ui::Rect&, overlay::ui::Color, const char*) override { texts++; }
  void push_clip(const overlay::ui::Rect&) override {}
  void pop_clip() override {}
  int rects = 0;
  int circles = 0;
  int lines = 0;
  int triangles = 0;
  int texts = 0;
};

}  // namespace

int main() {
  using namespace overlay;

  script::start();
  ui::menu().pages.clear();

  // Creation, property writes and reads.
  check(script::execute(R"(
    local page = ui.page("Home", "Root")
    local section = ui.section(page, "MAIN")
    ui.toggle(section, "sb", "SB", false)
    ui.slider(section, "sn", "SN", 5, 0, 10, "%.0f")
    ui.text_input(section, "st", "ST", "hi")
    ui.button(section, "sbtn", "Go")
    ui.set_bool("sb", true)
    ui.set_number("sn", 7)
    ui.set_text("st", "changed")
  )",
                           "create"), "create chunk runs");
  check(find_widget("sb") && ui::widget_bool(*find_widget("sb")), "lua wrote bool");
  check(find_widget("sn") && ui::widget_float(*find_widget("sn")) == 7.0f, "lua wrote number");
  check(find_widget("st") && ui::widget_string(*find_widget("st")) == "changed", "lua wrote text");

  // Widget state observed from Lua.
  check(script::execute(R"(
    local page = ui.page("H2", "R")
    local section = ui.section(page, "S")
    ui.toggle(section, "flag", "Flag", false)
    if ui.get_bool("flag") ~= false then error("expected false") end
    ui.set_bool("flag", true)
    if ui.get_bool("flag") ~= true then error("expected true") end
  )",
                           "read"), "property round trip through lua");

  // Button callbacks run on tick.
  check(script::execute(R"(
    local page = ui.page("H3", "R")
    local section = ui.section(page, "S")
    ui.toggle(section, "cb", "CB", true)
    ui.button(section, "go", "Go")
    ui.on_click("go", function() ui.set_bool("cb", false) end)
  )",
                           "click"), "click chunk runs");
  check(find_widget("go") != nullptr, "button exists");
  check(find_widget("cb") && ui::widget_bool(*find_widget("cb")), "callback not fired yet");
  find_widget("go")->state.clicked = true;
  script::tick(10.0);
  check(find_widget("cb") && !ui::widget_bool(*find_widget("cb")), "callback fired on tick");

  // task.wait resumes later.
  check(script::execute(R"(
    local page = ui.page("H4", "R")
    local section = ui.section(page, "S")
    ui.toggle(section, "tasked", "T", false)
    task.spawn(function()
      task.wait(0.1)
      ui.set_bool("tasked", true)
    end)
  )",
                           "task"), "task chunk runs");
  check(find_widget("tasked") && !ui::widget_bool(*find_widget("tasked")), "task waits");
  script::tick(10.05);
  check(find_widget("tasked") && !ui::widget_bool(*find_widget("tasked")), "task not due yet");
  script::tick(10.5);
  check(find_widget("tasked") && ui::widget_bool(*find_widget("tasked")), "task resumed");

  // Sandbox and containment.
  check(!script::execute("os.exit()", "sandbox"), "os is unavailable");
  check(!script::execute("error('boom')", "error"), "script error reported");
  const auto start = std::chrono::steady_clock::now();
  check(!script::execute("while true do end", "spin"), "infinite loop interrupted");
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  check(elapsed.count() < 2000, "interrupt fired quickly");

  // Custom painter draws through the canvas binding.
  check(script::execute(R"(
    local page = ui.page("H5", "R")
    local section = ui.section(page, "S")
    ui.toggle(section, "painted", "Painted", false)
    ui.set_painter("painted", function(state, canvas)
      canvas:rect(0, 0, 10, 10, "#ff0000", 3)
    end)
  )",
                           "painter"), "painter chunk runs");
  ui::Widget* painted = find_widget("painted");
  check(painted != nullptr, "painted widget exists");
  MockCanvas mock;
  if (painted) ui::widget_paint(*painted, mock);
  check(mock.rects == 1, "lua painter drew");

  ui::menu().pages.clear();

  // Drawing API: create, set, read back, remove, render.
  check(script::execute(R"(
    local circle = Drawing.new("Circle")
    circle.Visible = true
    circle.Position = Vector2.new(50, 60)
    circle.Radius = 12
    circle.Color = Color3.fromRGB(255, 0, 0)
    circle.Filled = true
    if circle.Radius ~= 12 then error("radius read back wrong") end
    if circle.Filled ~= true then error("filled read back wrong") end
    local gone = Drawing.new("Circle")
    gone.Visible = true
    gone:Remove()
    local line = Drawing.new("Line")
    line.Visible = true
    line.From = Vector2.new(0, 0)
    line.To = Vector2.new(100, 100)
    line.Transparency = 0.5
    local text = Drawing.new("Text")
    text.Visible = true
    text.Position = Vector2.new(10, 20)
    text.Text = "hello"
    text.Outline = true
    local square = Drawing.new("Square")
    square.Visible = true
    square.Filled = true
  )",
                           "drawing"), "drawing chunk runs");
  MockCanvas drawing_mock;
  script::render_drawing(drawing_mock);
  check(drawing_mock.circles == 1, "visible circle drawn, removed one skipped");
  check(drawing_mock.lines == 1, "line drawn");
  check(drawing_mock.texts == 5, "outlined text drawn as 4 offsets plus main");
  check(drawing_mock.rects == 1, "filled square drawn");

  if (g_failures == 0) {
    std::printf("unit_script: all checks passed\n");
    return 0;
  }
  std::printf("unit_script: %d failure(s)\n", g_failures);
  return 1;
}
