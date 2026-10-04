#pragma once

namespace overlay {

// Appends to overlay.log next to the module. Never throws, never blocks on failure.
void log(const char* fmt, ...);

}  // namespace overlay
