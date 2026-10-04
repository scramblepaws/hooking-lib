#include "overlay/overlay.h"

#include <windows.h>

#include <atomic>
#include <mutex>

#include "core/log.h"
#include "core/state.h"
#include "hook/hook.h"

namespace overlay {

namespace {

HANDLE g_host_event = nullptr;
std::atomic<bool> g_host_claimed{false};

DWORD WINAPI init_thread(LPVOID) {
  // Host-linked applications call an overlay_init entry point from main().
  // Give them a moment to claim; injected builds fall through to hook install.
  if (g_host_event && WaitForSingleObject(g_host_event, 750) == WAIT_OBJECT_0 && g_host_claimed.load()) {
    log("auto-init skipped (host manages init)");
    return 0;
  }
  init();
  return 0;
}

}  // namespace

void claim_host_init() {
  g_host_claimed.store(true);
  if (g_host_event) SetEvent(g_host_event);
}

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

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(instance);
    overlay::g_host_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    CreateThread(nullptr, 0, &overlay::init_thread, nullptr, 0, nullptr);
  }
  return TRUE;
}
