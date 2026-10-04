#pragma once

#include <string>

namespace overlay::config {

// Named snapshots are stored as <module dir>/configs/<name>.json.
// Names allow [A-Za-z0-9_-], 1..64 characters.

bool save(const char* name);
bool load(const char* name);
bool remove(const char* name);
void list(void (*callback)(void* user, const char* name), void* user);

// The name of the config the menu currently points at (the "config_name"
// text widget), or "default" when absent. Used for autosave on eject.
std::string active_name();

}  // namespace overlay::config
