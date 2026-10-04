#pragma once

namespace overlay::external {

// Creates the overlay's own topmost, color-keyed window and render loop on a
// worker thread. `target_hwnd` (optional) sizes the overlay to that window's
// client area; otherwise it covers the primary monitor.
bool start(void* target_hwnd);
void stop();
bool running();

}  // namespace overlay::external
