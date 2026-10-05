#pragma once

#include <deque>
#include <string>
#include <vector>

#include "ui/canvas.h"
#include "ui/widget.h"

namespace overlay::ui {

// Deques, not vectors: the C ABI hands out element pointers as opaque handles,
// so elements must not move when sibling pages/sections/widgets are added.
struct Section {
  std::string title;
  std::deque<Widget> widgets;
};

struct Page {
  std::string title;
  std::string nav_section;
  std::deque<Section> sections;
};

struct Menu {
  Rect rect;
  int current_page = 0;
  std::deque<Page> pages;
  void* chrome_user = nullptr;
  void (*chrome_painter)(void* user, void* canvas, void* menu_handle) = nullptr;
};

Menu& menu();

void build_starter_menu(Menu& menu);

}  // namespace overlay::ui
