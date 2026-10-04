#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_4.h>

#include <cstdint>

namespace {

enum class Mode {
  Dx11,
  Dx12,
};

Mode g_mode = Mode::Dx11;

IDXGISwapChain* g_swapchain = nullptr;
IDXGISwapChain3* g_swapchain3 = nullptr;
ID3D11Device* g_device11 = nullptr;
ID3D11RenderTargetView* g_rtv11 = nullptr;
ID3D12Device* g_device12 = nullptr;
ID3D12DescriptorHeap* g_rtv_heap12 = nullptr;
ID3D12Resource* g_buffers12[8] = {};
bool g_presented12[8] = {};
UINT g_rtv_size12 = 0;
bool g_resize_pending = false;

void release_dx12_targets() {
  for (ID3D12Resource*& buffer : g_buffers12) {
    if (buffer) {
      buffer->Release();
      buffer = nullptr;
    }
  }
}

void create_dx12_targets() {
  for (bool& presented : g_presented12) presented = false;
  if (!g_device12 || !g_swapchain3 || !g_rtv_heap12) return;
  D3D12_CPU_DESCRIPTOR_HANDLE rtv = g_rtv_heap12->GetCPUDescriptorHandleForHeapStart();
  for (UINT i = 0; i < 2; ++i) {
    if (FAILED(g_swapchain3->GetBuffer(i, __uuidof(ID3D12Resource), reinterpret_cast<void**>(&g_buffers12[i])))) return;
    g_device12->CreateRenderTargetView(g_buffers12[i], nullptr, rtv);
    rtv.ptr += g_rtv_size12;
  }
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }
  if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
    PostQuitMessage(0);
    return 0;
  }
  if (msg == WM_SIZE && wp != SIZE_MINIMIZED) {
    const UINT width = LOWORD(lp);
    const UINT height = HIWORD(lp);
    if (width && height) g_resize_pending = true;
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

int run_dx11(HWND hwnd) {
  DXGI_SWAP_CHAIN_DESC sd = {};
  sd.BufferCount = 2;
  sd.BufferDesc.Width = 1280;
  sd.BufferDesc.Height = 720;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hwnd;
  sd.SampleDesc.Count = 1;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  ID3D11DeviceContext* ctx = nullptr;
  D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                                           &sd, &g_swapchain, &g_device11, &level, &ctx))) {
    MessageBoxW(nullptr, L"D3D11 device creation failed", L"test target", MB_OK | MB_ICONERROR);
    return 1;
  }

  ID3D11Texture2D* back = nullptr;
  g_swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back));
  g_device11->CreateRenderTargetView(back, nullptr, &g_rtv11);
  back->Release();

  float t = 0;
  bool running = true;
  MSG msg = {};
  while (running) {
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) running = false;
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (g_resize_pending) {
      g_resize_pending = false;
      RECT client = {};
      GetClientRect(hwnd, &client);
      if (client.right > 0 && client.bottom > 0) {
        if (g_rtv11) {
          g_rtv11->Release();
          g_rtv11 = nullptr;
        }
        if (SUCCEEDED(g_swapchain->ResizeBuffers(0, static_cast<UINT>(client.right),
                                                 static_cast<UINT>(client.bottom), DXGI_FORMAT_UNKNOWN, 0))) {
          ID3D11Texture2D* new_back = nullptr;
          g_swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&new_back));
          if (new_back) {
            g_device11->CreateRenderTargetView(new_back, nullptr, &g_rtv11);
            new_back->Release();
          }
        }
      }
    }
    t += 0.016f;
    const float color[4] = {0.10f + 0.03f * static_cast<float>(static_cast<int>(t) % 4), 0.14f, 0.20f, 1.0f};
    if (g_rtv11) {
      ctx->ClearRenderTargetView(g_rtv11, color);
      g_swapchain->Present(1, 0);
    }
  }

  g_rtv11->Release();
  g_swapchain->Release();
  ctx->Release();
  g_device11->Release();
  return 0;
}

int run_dx12(HWND hwnd) {
  IDXGIFactory4* factory = nullptr;
  if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), reinterpret_cast<void**>(&factory)))) return 1;
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
                               reinterpret_cast<void**>(&g_device12)))) {
    MessageBoxW(nullptr, L"D3D12 device creation failed", L"test target", MB_OK | MB_ICONERROR);
    return 1;
  }

  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  ID3D12CommandQueue* queue = nullptr;
  g_device12->CreateCommandQueue(&queue_desc, __uuidof(ID3D12CommandQueue), reinterpret_cast<void**>(&queue));

  DXGI_SWAP_CHAIN_DESC1 sd = {};
  sd.Width = 1280;
  sd.Height = 720;
  sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferCount = 2;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.SampleDesc.Count = 1;
  sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  IDXGISwapChain1* swapchain1 = nullptr;
  if (FAILED(factory->CreateSwapChainForHwnd(queue, hwnd, &sd, nullptr, nullptr, &swapchain1))) return 1;
  swapchain1->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(&g_swapchain3));
  swapchain1->Release();
  g_swapchain = g_swapchain3;

  D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
  heap_desc.NumDescriptors = 8;
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  g_device12->CreateDescriptorHeap(&heap_desc, __uuidof(ID3D12DescriptorHeap),
                                   reinterpret_cast<void**>(&g_rtv_heap12));
  g_rtv_size12 = g_device12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  create_dx12_targets();

  ID3D12CommandAllocator* allocator = nullptr;
  g_device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
                                     reinterpret_cast<void**>(&allocator));
  ID3D12GraphicsCommandList* list = nullptr;
  g_device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr,
                                __uuidof(ID3D12GraphicsCommandList), reinterpret_cast<void**>(&list));
  list->Close();

  ID3D12Fence* fence = nullptr;
  g_device12->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), reinterpret_cast<void**>(&fence));
  HANDLE fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  UINT64 fence_value = 0;

  float t = 0;
  bool running = true;
  MSG msg = {};
  while (running) {
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) running = false;
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (g_resize_pending) {
      g_resize_pending = false;
      RECT client = {};
      GetClientRect(hwnd, &client);
      if (client.right > 0 && client.bottom > 0) {
        if (fence->GetCompletedValue() < fence_value) {
          fence->SetEventOnCompletion(fence_value, fence_event);
          WaitForSingleObject(fence_event, INFINITE);
        }
        list->Reset(allocator, nullptr);
        release_dx12_targets();
        if (SUCCEEDED(g_swapchain3->ResizeBuffers(0, static_cast<UINT>(client.right),
                                                  static_cast<UINT>(client.bottom), DXGI_FORMAT_UNKNOWN, 0))) {
          create_dx12_targets();
        }
        list->Close();
        ID3D12CommandList* lists[] = {list};
        queue->ExecuteCommandLists(1, lists);
        queue->Signal(fence, ++fence_value);
      }
    }

    if (fence->GetCompletedValue() < fence_value) {
      fence->SetEventOnCompletion(fence_value, fence_event);
      WaitForSingleObject(fence_event, INFINITE);
    }
    allocator->Reset();
    list->Reset(allocator, nullptr);

    const UINT index = g_swapchain3->GetCurrentBackBufferIndex();
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = g_buffers12[index % 2];
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = g_presented12[index % 2] ? D3D12_RESOURCE_STATE_PRESENT : D3D12_RESOURCE_STATE_COMMON;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    list->ResourceBarrier(1, &barrier);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = g_rtv_heap12->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += g_rtv_size12 * (index % 2);
    list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    t += 0.016f;
    const float color[4] = {0.10f + 0.03f * static_cast<float>(static_cast<int>(t) % 4), 0.14f, 0.20f, 1.0f};
    list->ClearRenderTargetView(rtv, color, 0, nullptr);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    g_presented12[index % 2] = true;
    list->ResourceBarrier(1, &barrier);
    list->Close();

    ID3D12CommandList* lists[] = {list};
    queue->ExecuteCommandLists(1, lists);
    queue->Signal(fence, ++fence_value);
    g_swapchain3->Present(1, 0);
  }

  if (fence->GetCompletedValue() < fence_value) {
    fence->SetEventOnCompletion(fence_value, fence_event);
    WaitForSingleObject(fence_event, INFINITE);
  }
  release_dx12_targets();
  CloseHandle(fence_event);
  fence->Release();
  list->Release();
  allocator->Release();
  g_rtv_heap12->Release();
  g_swapchain3->Release();
  queue->Release();
  g_device12->Release();
  return 0;
}

}  // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show) {
  const bool dx12 = wcsstr(GetCommandLineW(), L"--dx12") != nullptr;
  g_mode = dx12 ? Mode::Dx12 : Mode::Dx11;

  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = wnd_proc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = L"overlay_test_target";
  RegisterClassExW(&wc);

  RECT rect = {0, 0, 1280, 720};
  AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
  HWND hwnd = CreateWindowExW(0, wc.lpszClassName,
                              dx12 ? L"Overlay Test Target (DX12) (Del=menu, End=eject, Esc=quit)"
                                   : L"Overlay Test Target (DX11) (Del=menu, End=eject, Esc=quit)",
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left,
                              rect.bottom - rect.top, nullptr, nullptr, instance, nullptr);
  ShowWindow(hwnd, show);
  SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

  if (wcsstr(GetCommandLineW(), L"--wait")) Sleep(4000);
  return dx12 ? run_dx12(hwnd) : run_dx11(hwnd);
}
