#include "hook/hook.h"

#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <MinHook.h>

#include <atomic>
#include <cstdint>
#include <thread>

#include "core/log.h"
#include "core/state.h"
#include "render/backend.h"

#include "imgui.h"
#include "imgui_impl_win32.h"

// The backend header keeps this in a '#if 0' block; callers forward-declare it themselves.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace overlay::hook {

namespace {

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using CreateSwapChainFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
using CreateFactory1Fn = HRESULT(STDMETHODCALLTYPE*)(REFIID, void**);
using CreateFactory2Fn = HRESULT(STDMETHODCALLTYPE*)(UINT, REFIID, void**);
using D3D12CreateDeviceFn = HRESULT(STDMETHODCALLTYPE*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
using CreateQueueFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, const D3D12_COMMAND_QUEUE_DESC*, REFIID, void**);
using CreateSwapChainForHwndFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);

PresentFn g_present = nullptr;
ResizeFn g_resize = nullptr;
CreateSwapChainFn g_create_swapchain = nullptr;
CreateSwapChainForHwndFn g_create_swapchain_hwnd = nullptr;
CreateFactory1Fn g_create_factory1 = nullptr;
CreateFactory2Fn g_create_factory2 = nullptr;
D3D12CreateDeviceFn g_d3d12_create_device = nullptr;
CreateQueueFn g_create_queue = nullptr;
WNDPROC g_wndproc = nullptr;
HWND g_hwnd = nullptr;
HANDLE g_eject_event = nullptr;
unsigned g_init_attempts = 0;
int g_init_fail_logs = 0;
bool g_faulted = false;

// Every entry into our code (detours, window proc) holds this counter. Eject
// disables hooks, then waits for it to reach zero before freeing or unloading.
std::atomic<int> g_in_flight{0};
std::atomic<bool> g_shutting_down{false};
std::atomic<bool> g_host_mode{false};

struct InFlightGuard {
  InFlightGuard() { g_in_flight.fetch_add(1, std::memory_order_acq_rel); }
  ~InFlightGuard() { g_in_flight.fetch_sub(1, std::memory_order_acq_rel); }
};

// Hotkeys are polled, never read from WndProc: Games reset their window proc and
// raw-input games suppress WM_KEYDOWN entirely, but key state is always readable.
constexpr int kToggleKey = VK_DELETE;
constexpr int kEjectKey = VK_END;
bool g_toggle_down = false;
bool g_eject_down = false;

void poll_hotkeys() {
  ImGuiIO* io = ImGui::GetCurrentContext() ? &ImGui::GetIO() : nullptr;
  if (io && io->WantTextInput) return;  // let text fields consume these keys while editing

  State& st = state();
  const bool toggle_down = (GetAsyncKeyState(kToggleKey) & 0x8000) != 0;
  if (toggle_down && !g_toggle_down) {
    const bool open = !st.menu_open.load();
    st.menu_open.store(open);
    log("menu %s", open ? "open" : "closed");
  }
  g_toggle_down = toggle_down;

  const bool eject_down = (GetAsyncKeyState(kEjectKey) & 0x8000) != 0;
  if (eject_down && !g_eject_down) {
    log("eject key pressed");
    st.eject_requested.store(true);
  }
  g_eject_down = eject_down;
}

bool is_swallowed(UINT msg) {
  switch (msg) {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_CHAR:
    case WM_SYSCHAR:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
      return true;
    default:
      return false;
  }
}

LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  InFlightGuard guard;
  if (g_shutting_down.load()) return CallWindowProcW(g_wndproc, hwnd, msg, wp, lp);

  State& st = state();
  if (msg == WM_KEYDOWN && (lp & (1u << 30)) == 0) {
    st.captured_key.store(static_cast<int>(wp));
  } else if (msg == WM_LBUTTONDOWN) {
    st.captured_key.store(-1);
  } else if (msg == WM_RBUTTONDOWN) {
    st.captured_key.store(-2);
  } else if (msg == WM_MBUTTONDOWN) {
    st.captured_key.store(-3);
  } else if (msg == WM_XBUTTONDOWN) {
    st.captured_key.store(GET_XBUTTON_WPARAM(wp) == XBUTTON1 ? -4 : -5);
  } else if (msg == WM_CHAR) {
    const int count = st.char_count.load();
    if (count < 64) {
      st.chars[count].store(static_cast<unsigned int>(wp));
      st.char_count.store(count + 1);
    }
  }
  if (ImGui::GetCurrentContext()) ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp);
  if (st.menu_open.load()) {
    if (msg == WM_SETCURSOR) return TRUE;
    if (is_swallowed(msg)) return 1;
  }
  return CallWindowProcW(g_wndproc, hwnd, msg, wp, lp);
}

bool run_firewalled(void (*fn)(void*), void* user) {
#if defined(_MSC_VER)
  __try {
    fn(user);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
#else
  // ponytail: GCC has no __try; this covers C++ throws only. SEH faults still
  // reach the process; an MSVC build gets the full SEH firewall.
  try {
    fn(user);
    return true;
  } catch (...) {
    return false;
  }
#endif
}

void fault(const char* where) {
  log("%s faulted - disabling overlay rendering", where);
  render::shutdown();
  state().render_ready.store(false);
  g_faulted = true;
}

void begin_shutdown() {
  render::shutdown();
  if (g_wndproc) SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_wndproc));
  g_shutting_down.store(true);
  SetEvent(g_eject_event);
}

void eject_worker(HMODULE self) {
  WaitForSingleObject(g_eject_event, INFINITE);
  log("eject: disabling hooks");
  MH_DisableHook(MH_ALL_HOOKS);
  for (int i = 0; i < 4000 && g_in_flight.load() > 0; ++i) Sleep(1);
  log("eject: drained (in_flight=%d)", g_in_flight.load());
  MH_Uninitialize();
  if (g_host_mode.load()) {
    log("eject: host mode, hooks removed, module stays loaded");
    return;
  }
  log("eject: unloading module");
  FreeLibraryAndExitThread(self, 0);
}

void capture_queue(ID3D12CommandQueue* queue, bool authoritative) {
  if (!queue) return;
  D3D12_COMMAND_QUEUE_DESC desc = {};
  queue->GetDesc(&desc);
  if (desc.Type != D3D12_COMMAND_LIST_TYPE_DIRECT) return;
  if (!authoritative && state().dx12_queue.load()) return;  // first created wins (ours is created later)
  if (!state().dx12_queue.load()) log("dx12: present queue captured");
  state().dx12_queue.store(queue);
}

void capture_dx12_queue(IUnknown* device) {
  if (!device) return;
  ID3D12CommandQueue* queue = nullptr;
  if (SUCCEEDED(device->QueryInterface(__uuidof(ID3D12CommandQueue), reinterpret_cast<void**>(&queue))) && queue) {
    capture_queue(queue, true);
    queue->Release();
  }
}

HRESULT STDMETHODCALLTYPE hk_create_swapchain(IDXGIFactory* factory, IUnknown* device, DXGI_SWAP_CHAIN_DESC* desc,
                                              IDXGISwapChain** out) {
  InFlightGuard guard;
  if (!g_shutting_down.load()) capture_dx12_queue(device);
  return g_create_swapchain(factory, device, desc, out);
}

HRESULT STDMETHODCALLTYPE hk_create_swapchain_hwnd(IDXGIFactory2* factory, IUnknown* device, HWND hwnd,
                                                   const DXGI_SWAP_CHAIN_DESC1* desc,
                                                   const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen,
                                                   IDXGIOutput* output, IDXGISwapChain1** out) {
  InFlightGuard guard;
  if (!g_shutting_down.load()) capture_dx12_queue(device);
  return g_create_swapchain_hwnd(factory, device, hwnd, desc, fullscreen, output, out);
}

HRESULT STDMETHODCALLTYPE hk_create_queue(ID3D12Device* device, const D3D12_COMMAND_QUEUE_DESC* desc, REFIID iid,
                                          void** out);

// D3D12 objects expose per-instance stubs that MinHook cannot safely patch,
// so these entries are swapped in the object's own vtable instead.
bool hook_vtable_entry(void** vtbl, int index, void* detour, void** original) {
  DWORD old_protect = 0;
  if (!VirtualProtect(&vtbl[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &old_protect)) return false;
  *original = vtbl[index];
  vtbl[index] = detour;
  VirtualProtect(&vtbl[index], sizeof(void*), old_protect, &old_protect);
  return true;
}

void hook_target_device(ID3D12Device* device) {
  // ID3D12Device : ID3D12Object with slots 3-5 Get/SetPrivateData, 6 SetName,
  // 7 GetNodeCount, so CreateCommandQueue is slot 8.
  void** vtbl = *reinterpret_cast<void***>(device);
  void* original = nullptr;
  if (hook_vtable_entry(vtbl, 8, reinterpret_cast<void*>(&hk_create_queue), &original))
    g_create_queue = reinterpret_cast<CreateQueueFn>(original);
  else
    log("dx12: device vtable hook failed");
}

void hook_target_factory(IUnknown* unknown) {
  IDXGIFactory2* factory = nullptr;
  if (!unknown ||
      FAILED(unknown->QueryInterface(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory))) ||
      !factory)
    return;
  void** vtbl = *reinterpret_cast<void***>(factory);
  void* original = nullptr;
  if (hook_vtable_entry(vtbl, 9, reinterpret_cast<void*>(&hk_create_swapchain), &original))
    g_create_swapchain = reinterpret_cast<CreateSwapChainFn>(original);
  if (hook_vtable_entry(vtbl, 14, reinterpret_cast<void*>(&hk_create_swapchain_hwnd), &original))
    g_create_swapchain_hwnd = reinterpret_cast<CreateSwapChainForHwndFn>(original);
  factory->Release();
}

HRESULT STDMETHODCALLTYPE hk_create_queue(ID3D12Device* device, const D3D12_COMMAND_QUEUE_DESC* desc, REFIID iid,
                                          void** out) {
  InFlightGuard guard;
  const HRESULT hr = g_create_queue(device, desc, iid, out);
  if (!g_shutting_down.load() && SUCCEEDED(hr) && out && *out && desc &&
      desc->Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
    capture_queue(static_cast<ID3D12CommandQueue*>(*out), false);
  return hr;
}

HRESULT STDMETHODCALLTYPE hk_create_factory1(REFIID iid, void** out) {
  InFlightGuard guard;
  const HRESULT hr = g_create_factory1(iid, out);
  if (SUCCEEDED(hr) && out && *out) hook_target_factory(static_cast<IUnknown*>(*out));
  return hr;
}

HRESULT STDMETHODCALLTYPE hk_create_factory2(UINT flags, REFIID iid, void** out) {
  InFlightGuard guard;
  const HRESULT hr = g_create_factory2(flags, iid, out);
  if (SUCCEEDED(hr) && out && *out) hook_target_factory(static_cast<IUnknown*>(*out));
  return hr;
}

HRESULT STDMETHODCALLTYPE hk_d3d12_create_device(IUnknown* adapter, D3D_FEATURE_LEVEL level, REFIID iid, void** out) {
  InFlightGuard guard;
  const HRESULT hr = g_d3d12_create_device(adapter, level, iid, out);
  if (SUCCEEDED(hr) && out && *out) {
    ID3D12Device* device = nullptr;
    if (SUCCEEDED(static_cast<IUnknown*>(*out)->QueryInterface(__uuidof(ID3D12Device),
                                                               reinterpret_cast<void**>(&device))) &&
        device) {
      hook_target_device(device);
      device->Release();
    }
  }
  return hr;
}

void hook_target_device(ID3D12Device* device);

HRESULT STDMETHODCALLTYPE hk_resize(IDXGISwapChain* sc, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags) {
  InFlightGuard guard;
  if (g_shutting_down.load()) return g_resize(sc, count, width, height, format, flags);
  if (render::ready()) render::release_target();
  const HRESULT hr = g_resize(sc, count, width, height, format, flags);
  if (SUCCEEDED(hr) && render::ready()) render::create_target(sc);
  return hr;
}

HRESULT STDMETHODCALLTYPE hk_present(IDXGISwapChain* sc, UINT sync, UINT flags) {
  InFlightGuard guard;
  if (g_shutting_down.load()) return g_present(sc, sync, flags);

  State& st = state();
  poll_hotkeys();

  if (st.eject_requested.load()) {
    begin_shutdown();
    return g_present(sc, sync, flags);
  }

  if (!render::ready() && !g_faulted) {
    // retry periodically: the swapchain may not be renderable yet (mode switches, device resets)
    if (g_init_attempts++ % 60 == 0) {
      DXGI_SWAP_CHAIN_DESC desc = {};
      if (SUCCEEDED(sc->GetDesc(&desc))) {
        g_hwnd = desc.OutputWindow;
        struct InitArgs {
          IDXGISwapChain* swapchain;
          HWND hwnd;
          bool ok;
        } args{sc, g_hwnd, false};
        const bool survived = run_firewalled(
            [](void* user) {
              InitArgs* a = static_cast<InitArgs*>(user);
              a->ok = render::init(a->swapchain, a->hwnd);
            },
            &args);
        if (!survived) {
          fault("render::init");
          return g_present(sc, sync, flags);
        }
        if (args.ok) {
          g_wndproc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&wndproc)));
          st.render_ready.store(true);
          log("render ready (hwnd=%p)", g_hwnd);
        } else if (g_init_fail_logs < 3) {
          ++g_init_fail_logs;
          log("render init failed, will retry");
        }
      }
    }
  }

  if (render::ready()) {
    // games often replace their window proc on device/window transitions; take it back
    if (GetWindowLongPtrW(g_hwnd, GWLP_WNDPROC) != reinterpret_cast<LONG_PTR>(&wndproc)) {
      g_wndproc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&wndproc)));
      log("wndproc re-installed");
    }
    struct FrameArgs {
      IDXGISwapChain* swapchain;
    } frame_args{sc};
    if (!run_firewalled([](void* user) { render::frame(static_cast<FrameArgs*>(user)->swapchain); }, &frame_args))
      fault("render::frame");
  }

  const HRESULT hr = g_present(sc, sync, flags);
  if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
    log("device removed/reset (0x%08lx) - rendering will re-initialize", hr);
    if (render::ready()) render::shutdown();
    st.render_ready.store(false);
  }
  return hr;
}

bool create_hooks() {
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"overlay_dummy_window";
  RegisterClassExW(&wc);

  HWND dummy = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 4, 4, nullptr, nullptr, wc.hInstance, nullptr);
  if (!dummy) {
    log("dummy window failed (%lu)", GetLastError());
    return false;
  }

  DXGI_SWAP_CHAIN_DESC sd = {};
  sd.BufferCount = 1;
  sd.BufferDesc.Width = 4;
  sd.BufferDesc.Height = 4;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = dummy;
  sd.SampleDesc.Count = 1;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  IDXGISwapChain* sc = nullptr;
  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* ctx = nullptr;
  D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  const HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                                   D3D11_SDK_VERSION, &sd, &sc, &device, &level, &ctx);
  if (FAILED(hr)) {
    log("dummy device failed (0x%08lx)", hr);
    DestroyWindow(dummy);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return false;
  }

  void** vtbl = *reinterpret_cast<void***>(sc);
  const MH_STATUS s1 = MH_CreateHook(vtbl[8], reinterpret_cast<LPVOID>(&hk_present), reinterpret_cast<LPVOID*>(&g_present));
  const MH_STATUS s2 = MH_CreateHook(vtbl[13], reinterpret_cast<LPVOID>(&hk_resize), reinterpret_cast<LPVOID*>(&g_resize));
  const MH_STATUS s3 = MH_EnableHook(MH_ALL_HOOKS);

  sc->Release();
  device->Release();
  ctx->Release();
  DestroyWindow(dummy);
  UnregisterClassW(wc.lpszClassName, wc.hInstance);

  if (s1 != MH_OK || s2 != MH_OK || s3 != MH_OK) {
    log("hook creation failed (%d, %d, %d)", static_cast<int>(s1), static_cast<int>(s2), static_cast<int>(s3));
    return false;
  }
  return true;
}

// Exports that create the objects whose per-instance functions we need to hook
// to find the Target's DX12 present queue. Requires the DLL to be present
// before the Target creates its device (early injection / host mode).
void create_d3d12_capture_hooks() {
  if (HMODULE dxgi = GetModuleHandleW(L"dxgi.dll")) {
    if (FARPROC fn = GetProcAddress(dxgi, "CreateDXGIFactory1"))
      MH_CreateHook(reinterpret_cast<LPVOID>(fn), reinterpret_cast<LPVOID>(&hk_create_factory1), reinterpret_cast<LPVOID*>(&g_create_factory1));
    if (FARPROC fn = GetProcAddress(dxgi, "CreateDXGIFactory2"))
      MH_CreateHook(reinterpret_cast<LPVOID>(fn), reinterpret_cast<LPVOID>(&hk_create_factory2), reinterpret_cast<LPVOID*>(&g_create_factory2));
  }
  if (HMODULE d3d12 = LoadLibraryW(L"d3d12.dll")) {
    if (FARPROC fn = GetProcAddress(d3d12, "D3D12CreateDevice"))
      MH_CreateHook(reinterpret_cast<LPVOID>(fn), reinterpret_cast<LPVOID>(&hk_d3d12_create_device),
                    reinterpret_cast<LPVOID*>(&g_d3d12_create_device));
  }
  MH_EnableHook(MH_ALL_HOOKS);
}

}  // namespace

void mark_host_mode() { g_host_mode.store(true); }

bool install() {
  HMODULE self = nullptr;
  const auto address = reinterpret_cast<uintptr_t>(&install);
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCWSTR>(address), &self);

  if (MH_Initialize() != MH_OK) {
    log("MH_Initialize failed");
    return false;
  }
  g_eject_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!create_hooks()) return false;
  create_d3d12_capture_hooks();

  std::thread(eject_worker, self).detach();
  log("hooks installed (Present=%p ResizeBuffers=%p)", reinterpret_cast<void*>(g_present), reinterpret_cast<void*>(g_resize));
  return true;
}

}  // namespace overlay::hook
