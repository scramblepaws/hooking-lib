#include <windows.h>

#include <cmath>
#include <cstdio>
#include <string>

#include "core/config.h"
#include "core/paths.h"
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

void on_list(void* user, const char* name) {
  auto* found = static_cast<bool*>(user);
  if (std::string(name) == "unit_test") *found = true;
}

}  // namespace

int main() {
  using namespace overlay;
  using namespace overlay::ui;

  config::remove("unit_test");
  config::remove("unit_test_corrupt");

  Menu& m = menu();
  m.pages.clear();
  Page page;
  page.title = "T";
  page.nav_section = "S";
  Section section;
  section.title = "X";
  section.widgets.push_back(make_toggle("t_bool", "B", false));
  section.widgets.push_back(make_slider("t_float", "F", 0.0f, 0.0f, 10.0f, "%.1f"));
  section.widgets.push_back(make_dropdown("t_int", "I", 0, {"a", "b", "c"}));
  section.widgets.push_back(make_textinput("t_str", "S", ""));
  section.widgets.push_back(make_colorpicker("t_col", "C", {0, 0, 0, 1}));
  section.widgets.push_back(make_keybind("t_key", "K", 0));
  page.sections.push_back(std::move(section));
  m.pages.push_back(std::move(page));

  auto find = [&](const char* id) -> Widget* {
    for (Page& p : m.pages)
      for (Section& s : p.sections)
        for (Widget& w : s.widgets)
          if (w.id == id) return &w;
    return nullptr;
  };

  std::get<bool>(find("t_bool")->value) = true;
  std::get<float>(find("t_float")->value) = 3.5f;
  std::get<int>(find("t_int")->value) = 2;
  std::get<std::string>(find("t_str")->value) = "hello world";
  std::get<Color>(find("t_col")->value) = Color{0.1f, 0.2f, 0.3f, 0.4f};
  std::get<KeyCombo>(find("t_key")->value) = KeyCombo{-3};

  check(config::save("unit_test"), "save succeeds");
  check(config::load("unit_test"), "load succeeds");

  bool listed = false;
  config::list(&on_list, &listed);
  check(listed, "list finds saved config");

  check(std::get<bool>(find("t_bool")->value) == true, "bool round trip");
  check(std::fabs(std::get<float>(find("t_float")->value) - 3.5f) < 1e-4f, "float round trip");
  check(std::get<int>(find("t_int")->value) == 2, "int round trip");
  check(std::get<std::string>(find("t_str")->value) == "hello world", "string round trip");
  const Color color = std::get<Color>(find("t_col")->value);
  check(std::fabs(color.r - 0.1f) < 1e-4f && std::fabs(color.g - 0.2f) < 1e-4f && std::fabs(color.b - 0.3f) < 1e-4f &&
            std::fabs(color.a - 0.4f) < 1e-4f,
        "color round trip");
  check(std::get<KeyCombo>(find("t_key")->value).code == -3, "key round trip");

  std::get<bool>(find("t_bool")->value) = false;
  const std::wstring corrupt_path = module_directory() + L"configs\\unit_test_corrupt.json";
  FILE* file = _wfopen(corrupt_path.c_str(), L"wb");
  if (file) {
    const char garbage[] = "not json{{{";
    std::fwrite(garbage, 1, sizeof(garbage) - 1, file);
    std::fclose(file);
  }
  check(!config::load("unit_test_corrupt"), "corrupt file rejected");
  check(std::get<bool>(find("t_bool")->value) == false, "corrupt load leaves values untouched");

  check(!config::save("../evil"), "path traversal name rejected");
  check(!config::load("bad name"), "name with space rejected");

  config::remove("unit_test");
  config::remove("unit_test_corrupt");

  if (g_failures == 0) {
    std::printf("unit_config: all checks passed\n");
    return 0;
  }
  std::printf("unit_config: %d failure(s)\n", g_failures);
  return 1;
}
