#include "ui/menu_model.h"

namespace overlay::ui {

namespace {

Widget toggle(const char* id, const char* label, bool value) { return make_toggle(id, label, value); }

}  // namespace

void build_starter_menu(Menu& target) {
  Page ragebot;
  ragebot.title = "Ragebot";
  ragebot.nav_section = "Aimbot";

  Section main;
  main.title = "MAIN";
  main.widgets.push_back(toggle("enabled", "Enabled", false));
  main.widgets.push_back(toggle("peek_assist", "Peek Assist", false));
  main.widgets.push_back(toggle("hide_shots", "Hide Shots", false));
  main.widgets.push_back(toggle("double_tap", "Double Tap", false));
  main.widgets.push_back(make_slider("fov", "Field of View", 180.0f, 0.0f, 180.0f, u8"%.0f\xc2\xb0"));
  main.widgets.push_back(make_checkbox("visualize", "Visualize", false));
  main.widgets.push_back(make_keybind("peak_key", "Peak Key", 0));

  Section accuracy;
  accuracy.title = "ACCURACY";
  accuracy.widgets.push_back(toggle("auto_scope", "Auto Scope", false));
  accuracy.widgets.push_back(toggle("auto_stop", "Auto Stop", false));
  accuracy.widgets.push_back(make_colorpicker("tracer_color", "Tracer Color", {0.70f, 0.21f, 0.21f, 1.0f}));

  Section selection;
  selection.title = "SELECTION";
  const std::vector<std::string> hitboxes = {"Head", "Stomach", "Head, Stomach", "Nearest", "Chest"};
  selection.widgets.push_back(make_dropdown("hitboxes", "Hitboxes", 2, hitboxes));
  selection.widgets.push_back(make_dropdown("multipoint", "Multipoint", 2, hitboxes));
  selection.widgets.push_back(make_slider("hit_chance", "Hit Chance", 65.0f, 0.0f, 100.0f, "%.0f%%"));
  selection.widgets.push_back(make_slider("min_damage", "Min. Damage", 0.0f, 0.0f, 100.0f, "%.0f", "Auto"));
  selection.widgets.push_back(toggle("penetrate_walls", "Penetrate Walls", true));

  Section safety;
  safety.title = "SAFETY";
  safety.widgets.push_back(make_dropdown("body_aim", "Body Aim", 0, {"Default", "Nearest", "Head", "Chest"}));
  safety.widgets.push_back(make_dropdown("safe_points", "Safe Points", 0, {"Default", "Strict", "Aggressive"}));
  safety.widgets.push_back(make_dropdown("hitbox_safety", "Ensure Hitbox Safety", 0, {"Select", "Always", "On Key"}));

  ragebot.sections.push_back(std::move(main));
  ragebot.sections.push_back(std::move(accuracy));
  ragebot.sections.push_back(std::move(selection));
  ragebot.sections.push_back(std::move(safety));
  target.pages.push_back(std::move(ragebot));

  const char* placeholders[][2] = {
      {"Anti Aim", "Aimbot"}, {"Legitbot", "Aimbot"},   {"Players", "Visuals"}, {"World", "Visuals"},
      {"Inventory", "Visuals"}, {"Main", "Miscellaneous"}, {"Scripts", "Miscellaneous"},
  };
  for (const auto& entry : placeholders) {
    Page page;
    page.title = entry[0];
    page.nav_section = entry[1];
    target.pages.push_back(std::move(page));
  }

  Page configs;
  configs.title = "Configs";
  configs.nav_section = "Miscellaneous";
  Section config_section;
  config_section.title = "CONFIGS";
  config_section.widgets.push_back(make_textinput("config_name", "Config Name", "default"));
  config_section.widgets.push_back(make_button("config_save", "Save"));
  config_section.widgets.push_back(make_button("config_load", "Load"));
  configs.sections.push_back(std::move(config_section));
  target.pages.push_back(std::move(configs));
}

}  // namespace overlay::ui
