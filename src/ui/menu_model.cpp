#include "ui/menu_model.h"

namespace overlay::ui {

Menu& menu() {
  static Menu instance;
  return instance;
}

}  // namespace overlay::ui
