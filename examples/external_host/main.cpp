#include <windows.h>

#include "overlay/overlay_c.h"

// External mode: no injection, no hooks. The library owns a topmost,
// click-through window over the target (or the primary monitor) and runs the
// same UI + script stack on its own thread. Menu content comes from the
// scripts folder next to the DLL (scripts/starter.lua builds the default menu).

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  OverlayInitInfo info = {};
  info.size = sizeof(info);
  info.backend = OVERLAY_BACKEND_DX11;
  info.flags = OVERLAY_FLAG_EXTERNAL;
  info.target_hwnd = nullptr;
  info.menu_title = "external";
  if (overlay_init(&info) != OVERLAY_OK) {
    MessageBoxW(nullptr, L"overlay_init (external) failed", L"external host", MB_OK | MB_ICONERROR);
    return 1;
  }

  overlay_set_menu_open(1);

  while (overlay_is_ready()) Sleep(200);
  return 0;
}
