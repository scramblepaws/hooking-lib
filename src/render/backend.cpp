#include "render/backend.h"

#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi.h>

#include <cstdlib>

#include "core/log.h"
#include "core/state.h"

#include "imgui.h"
#include "imgui_impl_win32.h"

#include "render/dx11.h"
#include "render/dx12.h"
#include "render/textures.h"
#include "render/vulkan.h"
#include "script/script.h"
#include "ui/menu.h"

namespace overlay::render {

namespace {

enum class Api {
  Unknown,
  Dx11,
  Dx12,
  Vulkan,
};

Api g_api = Api::Unknown;
void* g_hwnd = nullptr;
IDXGISwapChain* g_swapchain = nullptr;
bool g_ready = false;

float ui_scale(void* hwnd) {
  if (const char* forced = std::getenv("OVERLAY_UI_SCALE")) {
    const float value = static_cast<float>(std::atof(forced));
    if (value > 0.1f) return value;
  }
  return ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
}

bool is_dx11(IDXGISwapChain* sc) {
  ID3D11Device* device = nullptr;
  const bool ok = SUCCEEDED(sc->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device))) && device;
  if (device) device->Release();
  return ok;
}

bool is_dx12(IDXGISwapChain* sc) {
  ID3D12Device* device = nullptr;
  const bool ok = SUCCEEDED(sc->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&device))) && device;
  if (device) device->Release();
  return ok;
}

}  // namespace

bool ready() { return g_ready; }

static void begin_ui() {
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
}

bool init(IDXGISwapChain* sc, void* hwnd) {
  if (g_ready) return true;
  g_hwnd = hwnd;
  g_swapchain = sc;

  if (!ImGui::GetCurrentContext()) ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.IniFilename = nullptr;  // never write imgui.ini into the Target's directory
  ImGui::StyleColorsDark();
  if (!ImGui_ImplWin32_Init(hwnd)) {
    log("render: win32 backend init failed");
    ImGui::DestroyContext();
    return false;
  }

  bool backend_ok = false;
  if (is_dx11(sc)) {
    g_api = Api::Dx11;
    backend_ok = dx11::init(sc, hwnd);
  } else if (is_dx12(sc)) {
    g_api = Api::Dx12;
    backend_ok = dx12::init(sc, hwnd);
  } else {
    log("render: swapchain device is neither DX11 nor DX12");
  }
  if (!backend_ok) {
    g_api = Api::Unknown;
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    return false;
  }

  ui::init(ui_scale(hwnd));
  script::start();
  script::set_texture_provider(&texture_for);
  g_ready = true;
  log("render backend: %s", g_api == Api::Dx11 ? "dx11" : "dx12");
  return true;
}

void frame(IDXGISwapChain* sc) {
  if (!g_ready) return;

  if (sc != g_swapchain) {
    // The Target recreated its swapchain; rebuild our targets against the new one.
    release_target();
    g_swapchain = sc;
    create_target(sc);
  }

  switch (g_api) {
    case Api::Dx11: dx11::new_frame(); break;
    case Api::Dx12: dx12::new_frame(); break;
    default: return;
  }

  begin_ui();

  switch (g_api) {
    case Api::Dx11: dx11::render(); break;
    case Api::Dx12: dx12::render(); break;
    default: break;
  }
}

bool init_vulkan(void* hwnd, const vulkan::Target& target) {
  if (g_ready) return g_api == Api::Vulkan;
  g_hwnd = hwnd;

  if (!ImGui::GetCurrentContext()) ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.IniFilename = nullptr;
  ImGui::StyleColorsDark();
  if (!ImGui_ImplWin32_Init(hwnd)) {
    log("render: win32 backend init failed");
    ImGui::DestroyContext();
    return false;
  }

  g_api = Api::Vulkan;
  if (!vulkan::init(hwnd, target)) {
    g_api = Api::Unknown;
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    return false;
  }

  ui::init(ui_scale(hwnd));
  script::start();
  script::set_texture_provider(&texture_for);
  g_ready = true;
  log("render backend: vulkan");
  return true;
}

void frame_vulkan(void* queue, void* swapchain, uint32_t image_index) {
  if (!g_ready || g_api != Api::Vulkan) return;
  vulkan::new_frame();
  begin_ui();
  vulkan::render(queue, swapchain, image_index);
}

void shutdown() {
  if (!g_ready) return;
  g_ready = false;
  ui::save_active_config();
  script::set_texture_provider(nullptr);
  release_textures();
  switch (g_api) {
    case Api::Dx11: dx11::shutdown(); break;
    case Api::Dx12: dx12::shutdown(); break;
    case Api::Vulkan: vulkan::shutdown(); break;
    default: break;
  }
  ImGui_ImplWin32_Shutdown();
  if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
  g_api = Api::Unknown;
  g_swapchain = nullptr;
  log("render shutdown");
}

void release_target() {
  switch (g_api) {
    case Api::Dx11: dx11::release_target(); break;
    case Api::Dx12: dx12::release_target(); break;
    default: break;
  }
}

bool create_target(IDXGISwapChain* sc) {
  switch (g_api) {
    case Api::Dx11: return dx11::create_target(sc);
    case Api::Dx12: return dx12::create_target(sc);
    default: return false;
  }
}

}  // namespace overlay::render
