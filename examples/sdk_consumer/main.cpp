#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include "overlay/overlay_c.h"

namespace {

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }
  if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

void chrome_painter(void* user, OverlayCanvas* canvas, OverlayMenu* menu_handle) {
  (void)user;
  const float s = overlay_canvas_scale(canvas);
  const OverlayRect panel = overlay_menu_rect(menu_handle);
  const OverlayColor white = {0.92f, 0.92f, 0.92f, 1.0f};
  const OverlayColor dim = {0.55f, 0.55f, 0.57f, 1.0f};
  const OverlayColor accent = {0.70f, 0.21f, 0.21f, 1.0f};

  overlay_canvas_fill_rect(canvas, panel, (OverlayColor){0.055f, 0.055f, 0.063f, 0.97f}, 8.0f * s);
  overlay_canvas_stroke_rect(canvas, panel, (OverlayColor){0.17f, 0.17f, 0.19f, 1.0f}, 1.0f, 8.0f * s);
  overlay_canvas_fill_rect(canvas, (OverlayRect){panel.x, panel.y, panel.w, 2.0f * s}, accent, 0.0f);
  overlay_canvas_text_sized(canvas, (OverlayVec2){panel.x + 22.0f * s, panel.y + 16.0f * s}, white, 20.0f * s,
                            "SDK Consumer");

  OverlayPage* page = overlay_menu_page(menu_handle, overlay_menu_page_index(menu_handle));
  if (!page) return;

  const int page_count = static_cast<int>(overlay_menu_page_count(menu_handle));
  float tab_x = panel.x + 22.0f * s;
  for (int i = 0; i < page_count; ++i) {
    OverlayPage* entry = overlay_menu_page(menu_handle, i);
    const char* title = overlay_page_title(entry);
    const OverlayVec2 size = overlay_canvas_measure_text(canvas, title);
    const OverlayRect tab{tab_x, panel.y + 52.0f * s, size.x + 26.0f * s, 28.0f * s};
    const bool active = i == overlay_menu_page_index(menu_handle);
    overlay_canvas_fill_rect(canvas, tab, active ? accent : (OverlayColor){0.12f, 0.12f, 0.14f, 1.0f}, 4.0f * s);
    overlay_canvas_text_centered(canvas, tab, active ? white : dim, title);
    tab_x += tab.w + 8.0f * s;
  }

  float y = panel.y + 100.0f * s;
  const size_t section_count = overlay_page_section_count(page);
  for (size_t si = 0; si < section_count; ++si) {
    OverlaySection* section = overlay_page_section(page, si);
    overlay_canvas_text(canvas, (OverlayVec2){panel.x + 22.0f * s, y}, dim, overlay_section_title(section));
    y += 26.0f * s;
    const size_t widget_count = overlay_section_widget_count(section);
    for (size_t wi = 0; wi < widget_count; ++wi) {
      OverlayWidget* widget = overlay_section_widget(section, wi);
      overlay_widget_set_rect(widget, (OverlayRect){panel.x + 22.0f * s, y, panel.w - 44.0f * s, 24.0f * s});
      y += 38.0f * s;
    }
    y += 12.0f * s;
  }
}

void build_menu() {
  OverlayInitInfo info;
  info.size = static_cast<uint32_t>(sizeof(info));
  info.backend = OVERLAY_BACKEND_DX11;
  info.flags = 0;
  info.target_hwnd = nullptr;
  info.menu_title = "sdk_consumer";
  if (overlay_init(&info) != OVERLAY_OK) {
    MessageBoxW(nullptr, L"overlay_init failed", L"sdk_consumer", MB_OK | MB_ICONERROR);
    return;
  }

  OverlayContext* context = overlay_context();
  OverlayPage* page = overlay_page_create(context, "Custom", "Root");
  OverlaySection* main_section = overlay_section_create(page, "MAIN");
  OverlayWidget* enabled = overlay_widget_create(main_section, OVERLAY_WIDGET_TOGGLE, "enabled", "Enabled");
  overlay_widget_set_bool(enabled, 1);
  OverlayWidget* strength = overlay_widget_create(main_section, OVERLAY_WIDGET_SLIDER, "strength", "Aim Strength");
  overlay_widget_set_range(strength, 0.0f, 100.0f);
  overlay_widget_set_format(strength, "%.0f", nullptr);
  overlay_widget_set_float(strength, 42.0f);

  OverlaySection* extras = overlay_section_create(page, "EXTRAS");
  overlay_widget_create(extras, OVERLAY_WIDGET_KEYBIND, "panic", "Panic Key");
  OverlayWidget* color = overlay_widget_create(extras, OVERLAY_WIDGET_COLOR_PICKER, "accent", "Accent");
  overlay_widget_set_color(color, (OverlayColor){0.70f, 0.21f, 0.21f, 1.0f});

  overlay_menu_set_chrome_painter(chrome_painter, nullptr);
  overlay_set_menu_open(1);
}

}  // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show) {
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = wnd_proc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = L"overlay_sdk_consumer";
  RegisterClassExW(&wc);

  RECT rect = {0, 0, 1100, 650};
  AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
  HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"SDK Consumer (Del=menu, End=eject, Esc=quit)",
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left,
                              rect.bottom - rect.top, nullptr, nullptr, instance, nullptr);
  ShowWindow(hwnd, show);
  SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

  DXGI_SWAP_CHAIN_DESC sd = {};
  sd.BufferCount = 2;
  sd.BufferDesc.Width = 1100;
  sd.BufferDesc.Height = 650;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hwnd;
  sd.SampleDesc.Count = 1;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  IDXGISwapChain* swapchain = nullptr;
  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* ctx = nullptr;
  D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                           D3D11_SDK_VERSION, &sd, &swapchain, &device, &level, &ctx))) {
    MessageBoxW(nullptr, L"D3D11 device creation failed", L"sdk_consumer", MB_OK | MB_ICONERROR);
    return 1;
  }

  ID3D11Texture2D* back = nullptr;
  ID3D11RenderTargetView* rtv = nullptr;
  swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back));
  device->CreateRenderTargetView(back, nullptr, &rtv);
  back->Release();

  build_menu();

  bool running = true;
  MSG msg = {};
  while (running) {
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) running = false;
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    const float color[4] = {0.09f, 0.11f, 0.14f, 1.0f};
    ctx->ClearRenderTargetView(rtv, color);
    swapchain->Present(1, 0);
  }

  rtv->Release();
  swapchain->Release();
  ctx->Release();
  device->Release();
  return 0;
}
