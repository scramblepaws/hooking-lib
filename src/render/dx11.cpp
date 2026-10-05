#include "render/dx11.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include "core/log.h"

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

namespace overlay::render::dx11 {

namespace {

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;

bool make_target(IDXGISwapChain* sc) {
  ID3D11Texture2D* back = nullptr;
  if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back))) || !back) return false;
  const HRESULT hr = g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
  back->Release();
  return SUCCEEDED(hr);
}

}  // namespace

ID3D11Device* device() { return g_device; }

bool create_target(IDXGISwapChain* sc) { return make_target(sc); }

void release_target() {
  if (g_rtv) {
    g_rtv->Release();
    g_rtv = nullptr;
  }
}

bool init(IDXGISwapChain* sc, void*) {
  if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&g_device))) || !g_device) return false;
  g_device->GetImmediateContext(&g_ctx);
  if (!g_ctx || !make_target(sc)) return false;

  if (!ImGui_ImplDX11_Init(g_device, g_ctx)) {
    log("ImGui dx11 backend init failed");
    return false;
  }
  return true;
}

void new_frame() {
  ImGui_ImplDX11_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();
}

void render() {
  ImGui::Render();
  g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
  ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void shutdown() {
  ImGui_ImplDX11_Shutdown();
  release_target();
  if (g_ctx) {
    g_ctx->Release();
    g_ctx = nullptr;
  }
  if (g_device) {
    g_device->Release();
    g_device = nullptr;
  }
}

}  // namespace overlay::render::dx11
