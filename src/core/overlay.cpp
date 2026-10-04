#include "overlay/overlay.h"

#include <windows.h>

#include <mutex>

#include "core/log.h"
#include "core/state.h"
#include "hook/hook.h"

namespace overlay {

State& state() {
  static State s;
  return s;
}

Status init() {
  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);
  State& st = state();
  if (st.hooks_installed.load()) return Status::already_running;
  if (!hook::install()) return Status::hook_failed;
  st.hooks_installed.store(true);
  log("overlay::init ok");
  return Status::ok;
}

Status shutdown() {
  State& st = state();
  if (!st.hooks_installed.load()) return Status::not_running;
  // Teardown runs on the render thread at the next Present, then the module ejects.
  st.eject_requested.store(true);
  return Status::ok;
}

bool running() { return state().render_ready.load(); }

}  // namespace overlay

namespace {

DWORD WINAPI init_thread(LPVOID) {
  overlay::init();
  return 0;
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(instance);
    CreateThread(nullptr, 0, &init_thread, nullptr, 0, nullptr);
  }
  return TRUE;
}
