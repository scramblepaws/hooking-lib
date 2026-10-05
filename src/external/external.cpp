#include "external/external.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <atomic>
#include <cstdlib>
#include <thread>

#include "core/log.h"
#include "core/salvage.h"
#include "core/state.h"
#include "script/script.h"
#include "ui/menu.h"

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

// The backend header keeps this in a '#if 0' block; callers forward-declare it themselves.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace overlay::external {

namespace {

constexpr const wchar_t* kWindowClass = L"overlay_external";
constexpr int kToggleKey = VK_DELETE;
constexpr int kExitKey = VK_END;

std::thread g_thread;
std::atomic<bool> g_running{false};
HWND g_hwnd = nullptr;
bool g_toggle_down = false;
bool g_exit_down = false;
bool g_interactive = false;

float ui_scale(HWND hwnd) {
  if (const char* forced = std::getenv("OVERLAY_UI_SCALE")) {
    const float value = static_cast<float>(std::atof(forced));
    if (value > 0.1f) return value;
  }
  return ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
}

void set_interactive(bool interactive) {
  if (!g_hwnd || interactive == g_interactive) return;
  g_interactive = interactive;
  LONG_PTR style = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
  if (interactive)
    style &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
  else
    style |= WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
  SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, style);
  if (interactive) {
    SetForegroundWindow(g_hwnd);
    log("external: menu interactive");
  } else {
    log("external: click-through");
  }
}

bool poll_key(int vk, bool& previous) {
  const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
  const bool pressed = down && !previous;
  previous = down;
  return pressed;
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_DESTROY || msg == WM_CLOSE) {
    PostQuitMessage(0);
    return 0;
  }
  if (ImGui::GetCurrentContext()) ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp);
  return DefWindowProcW(hwnd, msg, wp, lp);
}

struct FrameContext {
  ID3D11DeviceContext* ctx;
  ID3D11RenderTargetView* rtv;
  IDXGISwapChain* swapchain;
};

void frame_body(void* user) {
  FrameContext* frame = static_cast<FrameContext*>(user);
  ImGui_ImplDX11_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();
  ui::set_scale(ui_scale(g_hwnd));
  const bool menu_open = state().menu_open.load();
  if (menu_open) {
    ui::draw();
  } else {
    state().captured_key.store(0);
    state().char_count.store(0);
  }
  ui::draw_overlays();
  script::tick(ImGui::GetTime());
  ImGui::GetIO().MouseDrawCursor = menu_open;
  ImGui::Render();

  const float key_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  frame->ctx->OMSetRenderTargets(1, &frame->rtv, nullptr);
  frame->ctx->ClearRenderTargetView(frame->rtv, key_color);
  ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
  frame->swapchain->Present(1, 0);
}

void thread_main(HWND target, HANDLE ready_event, bool* ready_flag) {
  HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = wnd_proc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = kWindowClass;
  RegisterClassExW(&wc);

  RECT area = {};
  if (target && IsWindow(static_cast<HWND>(target))) {
    GetClientRect(static_cast<HWND>(target), &area);
    POINT origin = {0, 0};
    ClientToScreen(static_cast<HWND>(target), &origin);
    area.right += origin.x;
    area.bottom += origin.y;
    area.left = origin.x;
    area.top = origin.y;
  } else {
    MONITORINFO monitor = {};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromWindow(nullptr, MONITOR_DEFAULTTOPRIMARY), &monitor);
    area = monitor.rcMonitor;
  }

  g_hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
                           kWindowClass, L"Overlay", WS_POPUP, area.left, area.top, area.right - area.left,
                           area.bottom - area.top, nullptr, nullptr, instance, nullptr);
  if (!g_hwnd) {
    log("external: window creation failed (%lu)", GetLastError());
    SetEvent(ready_event);
    return;
  }
  SetLayeredWindowAttributes(g_hwnd, RGB(0, 0, 0), 255, LWA_COLORKEY);
  ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);

  DXGI_SWAP_CHAIN_DESC sd = {};
  sd.BufferCount = 2;
  sd.BufferDesc.Width = static_cast<UINT>(area.right - area.left);
  sd.BufferDesc.Height = static_cast<UINT>(area.bottom - area.top);
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = g_hwnd;
  sd.SampleDesc.Count = 1;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* ctx = nullptr;
  IDXGISwapChain* swapchain = nullptr;
  ID3D11RenderTargetView* rtv = nullptr;
  D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                           D3D11_SDK_VERSION, &sd, &swapchain, &device, &level, &ctx))) {
    log("external: D3D11 device creation failed");
    DestroyWindow(g_hwnd);
    g_hwnd = nullptr;
    SetEvent(ready_event);
    return;
  }

  ID3D11Texture2D* back = nullptr;
  swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back));
  device->CreateRenderTargetView(back, nullptr, &rtv);
  back->Release();

  if (!ImGui::GetCurrentContext()) ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.IniFilename = nullptr;
  ImGui::StyleColorsDark();
  ImGui_ImplWin32_Init(g_hwnd);
  ImGui_ImplDX11_Init(device, ctx);
  ui::init(ui_scale(g_hwnd));
  script::start();

  log("external: overlay window ready");
  *ready_flag = true;
  SetEvent(ready_event);

  MSG msg = {};
  bool faulted = false;
  while (g_running.load()) {
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) g_running.store(false);
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (!g_running.load()) break;

    if (poll_key(kToggleKey, g_toggle_down)) {
      const bool open = !state().menu_open.load();
      state().menu_open.store(open);
      log("menu %s", open ? "open" : "closed");
    }
    if (poll_key(kExitKey, g_exit_down)) g_running.store(false);

    set_interactive(state().menu_open.load());

    FrameContext frame{ctx, rtv, swapchain};
    if (!salvage::run(frame_body, &frame)) {
      log("external: frame faulted, rendering disabled");
      faulted = true;
      break;
    }
  }

  if (!faulted) {
    ui::save_active_config();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
  }
  rtv->Release();
  swapchain->Release();
  ctx->Release();
  device->Release();
  DestroyWindow(g_hwnd);
  g_hwnd = nullptr;
  UnregisterClassW(kWindowClass, instance);
}

}  // namespace

bool running() { return g_running.load(); }

bool start(void* target_hwnd) {
  if (g_running.exchange(true)) return true;
  salvage::install();
  HANDLE ready_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  bool ready = false;
  g_toggle_down = false;
  g_exit_down = false;
  g_interactive = false;
  state().menu_open.store(false);
  g_thread = std::thread(thread_main, static_cast<HWND>(target_hwnd), ready_event, &ready);
  WaitForSingleObject(ready_event, 10000);
  CloseHandle(ready_event);
  if (!ready) {
    g_running.store(false);
    if (g_thread.joinable()) g_thread.join();
    return false;
  }
  return true;
}

void stop() {
  if (!g_running.exchange(false)) return;
  if (g_hwnd) PostMessageW(g_hwnd, WM_NULL, 0, 0);
  if (g_thread.joinable()) g_thread.join();
}

}  // namespace overlay::external
