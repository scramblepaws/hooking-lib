#pragma once

#include <string>
#include <vector>

#include "ui/canvas.h"
#include "ui/widget.h"

namespace overlay::ui {

struct Section {
  std::string title;
  std::vector<Widget> widgets;
};

struct Page {
  std::string title;
  std::string nav_section;
  std::vector<Section> sections;
};

struct Menu {
  Rect rect;
  int current_page = 0;
  std::vector<Page> pages;
  void* chrome_user = nullptr;
  void (*chrome_painter)(void* user, void* canvas, void* menu_handle) = nullptr;
};

Menu& menu();

void build_starter_menu(Menu& menu);

}  // namespace overlay::ui
