#include "render/dx12.h"

#include <windows.h>
#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_4.h>

#include <cstdlib>
#include <vector>

#include "core/log.h"
#include "core/state.h"

#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

namespace overlay::render::dx12 {

namespace {

constexpr int kFramesInFlight = 3;
constexpr int kSrvHeapCount = 128;

struct Frame {
  ID3D12CommandAllocator* allocator = nullptr;
  UINT64 fence_value = 0;
};

ID3D12Device* g_device = nullptr;
ID3D12CommandQueue* g_queue = nullptr;
ID3D12GraphicsCommandList* g_list = nullptr;
Frame g_frames[kFramesInFlight];
int g_frame_index = 0;
ID3D12Fence* g_fence = nullptr;
UINT64 g_fence_value = 0;
HANDLE g_fence_event = nullptr;
ID3D12DescriptorHeap* g_rtv_heap = nullptr;
ID3D12DescriptorHeap* g_srv_heap = nullptr;
UINT g_rtv_size = 0;
std::vector<ID3D12Resource*> g_buffers;
IDXGISwapChain* g_swapchain = nullptr;
IDXGISwapChain3* g_swapchain3 = nullptr;
int g_srv_next = 0;

void srv_alloc(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
  auto* heap = info->SrvDescriptorHeap;
  const UINT size = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = heap->GetCPUDescriptorHandleForHeapStart();
  D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle = heap->GetGPUDescriptorHandleForHeapStart();
  cpu_handle.ptr += static_cast<SIZE_T>(size) * g_srv_next;
  gpu_handle.ptr += static_cast<UINT64>(size) * g_srv_next;
  g_srv_next = (g_srv_next + 1) % kSrvHeapCount;
  *cpu = cpu_handle;
  *gpu = gpu_handle;
}

void srv_free(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE) {}

void wait_for_frame() {
  const Frame& frame = g_frames[g_frame_index];
  if (frame.fence_value != 0 && g_fence->GetCompletedValue() < frame.fence_value) {
    g_fence->SetEventOnCompletion(frame.fence_value, g_fence_event);
    WaitForSingleObject(g_fence_event, INFINITE);
  }
}

bool make_targets(IDXGISwapChain* sc) {
  for (ID3D12Resource* buffer : g_buffers)
    if (buffer) buffer->Release();
  g_buffers.clear();

  DXGI_SWAP_CHAIN_DESC desc = {};
  if (FAILED(sc->GetDesc(&desc))) return false;
  g_buffers.resize(desc.BufferCount, nullptr);

  D3D12_CPU_DESCRIPTOR_HANDLE rtv = g_rtv_heap->GetCPUDescriptorHandleForHeapStart();
  for (UINT i = 0; i < desc.BufferCount; ++i) {
    if (FAILED(sc->GetBuffer(i, __uuidof(ID3D12Resource), reinterpret_cast<void**>(&g_buffers[i])))) return false;
    g_device->CreateRenderTargetView(g_buffers[i], nullptr, rtv);
    rtv.ptr += g_rtv_size;
  }
  return true;
}

}  // namespace

bool create_target(IDXGISwapChain* sc) {
  g_swapchain = sc;
  return make_targets(sc);
}

void release_target() {
  for (ID3D12Resource* buffer : g_buffers)
    if (buffer) buffer->Release();
  g_buffers.clear();
}

bool init(IDXGISwapChain* sc, void* hwnd) {
  if (FAILED(sc->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&g_device))) || !g_device) { log("dx12: GetDevice failed"); return false; }
  g_swapchain = sc;

  DXGI_SWAP_CHAIN_DESC swapchain_desc = {};
  if (FAILED(sc->GetDesc(&swapchain_desc))) { log("dx12: GetDesc failed"); return false; }
  if (FAILED(sc->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(&g_swapchain3)))) { log("dx12: SwapChain3 unavailable"); return false; }

  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (FAILED(g_device->CreateCommandQueue(&queue_desc, __uuidof(ID3D12CommandQueue), reinterpret_cast<void**>(&g_queue)))) { log("dx12: queue failed"); return false; }

  for (Frame& frame : g_frames) {
    if (FAILED(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
                                                reinterpret_cast<void**>(&frame.allocator))))
      return false;
  }
  if (FAILED(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_frames[0].allocator, nullptr,
                                         __uuidof(ID3D12GraphicsCommandList), reinterpret_cast<void**>(&g_list))))
    return false;
  g_list->Close();

  if (FAILED(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), reinterpret_cast<void**>(&g_fence))))
    return false;
  g_fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);

  D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {};
  rtv_desc.NumDescriptors = swapchain_desc.BufferCount;
  rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  if (FAILED(g_device->CreateDescriptorHeap(&rtv_desc, __uuidof(ID3D12DescriptorHeap), reinterpret_cast<void**>(&g_rtv_heap)))) { log("dx12: rtv heap failed"); return false; }
  g_rtv_size = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

  D3D12_DESCRIPTOR_HEAP_DESC srv_desc = {};
  srv_desc.NumDescriptors = kSrvHeapCount;
  srv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  srv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(g_device->CreateDescriptorHeap(&srv_desc, __uuidof(ID3D12DescriptorHeap),
                                            reinterpret_cast<void**>(&g_srv_heap))))
    return false;

  if (!make_targets(sc)) { log("dx12: target creation failed"); return false; }

  if (!ImGui_ImplWin32_Init(hwnd)) {
    log("ImGui win32 backend init failed");
    return false;
  }

  ImGui_ImplDX12_InitInfo info;
  info.Device = g_device;
  info.CommandQueue = g_queue;
  info.NumFramesInFlight = kFramesInFlight;
  info.RTVFormat = swapchain_desc.BufferDesc.Format;
  info.DSVFormat = DXGI_FORMAT_UNKNOWN;
  info.SrvDescriptorHeap = g_srv_heap;
  info.SrvDescriptorAllocFn = srv_alloc;
  info.SrvDescriptorFreeFn = srv_free;
  if (!ImGui_ImplDX12_Init(&info)) {
    log("ImGui dx12 backend init failed");
    return false;
  }
  return true;
}

void new_frame() {
  wait_for_frame();
  g_frames[g_frame_index].allocator->Reset();
  ImGui_ImplDX12_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();
}

void render() {
  if (!g_swapchain || g_buffers.empty()) return;
  ImGui::Render();

  // Submit on the Target's queue: same-queue ordering keeps our backbuffer
  // transitions after the Target's own frame. Skip until one is captured.
  ID3D12CommandQueue* queue = static_cast<ID3D12CommandQueue*>(state().dx12_queue.load());
  if (!queue) {
    static bool logged = false;
    if (!logged) {
      logged = true;
      log("dx12: waiting for the target queue");
    }
    return;
  }

  g_list->Reset(g_frames[g_frame_index].allocator, nullptr);
  const UINT index = g_swapchain3->GetCurrentBackBufferIndex() % static_cast<UINT>(g_buffers.size());

  // ponytail: the Target owns the buffer state; we assume PRESENT on entry and
  // restore it. Correct for the usual Present-ordered flow; a fully racy
  // multi-queue engine would need to poll the Target's fence instead.
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = g_buffers[index];
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
  g_list->ResourceBarrier(1, &barrier);

  D3D12_CPU_DESCRIPTOR_HANDLE rtv = g_rtv_heap->GetCPUDescriptorHandleForHeapStart();
  rtv.ptr += g_rtv_size * index;
  g_list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  g_list->SetDescriptorHeaps(1, &g_srv_heap);
  ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_list);

  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
  g_list->ResourceBarrier(1, &barrier);
  g_list->Close();

  ID3D12CommandList* lists[] = {g_list};
  queue->ExecuteCommandLists(1, lists);
  g_frames[g_frame_index].fence_value = ++g_fence_value;
  queue->Signal(g_fence, g_fence_value);
  g_frame_index = (g_frame_index + 1) % kFramesInFlight;
}

void shutdown() {
  if (!g_device) return;
  ImGui_ImplDX12_Shutdown();
  release_target();
  for (Frame& frame : g_frames) {
    if (frame.allocator) {
      frame.allocator->Release();
      frame.allocator = nullptr;
    }
  }
  if (g_list) {
    g_list->Release();
    g_list = nullptr;
  }
  if (g_queue) {
    g_queue->Release();
    g_queue = nullptr;
  }
  if (g_fence) {
    g_fence->Release();
    g_fence = nullptr;
  }
  if (g_fence_event) {
    CloseHandle(g_fence_event);
    g_fence_event = nullptr;
  }
  if (g_rtv_heap) {
    g_rtv_heap->Release();
    g_rtv_heap = nullptr;
  }
  if (g_srv_heap) {
    g_srv_heap->Release();
    g_srv_heap = nullptr;
  }
  g_device->Release();
  g_device = nullptr;
  if (g_swapchain3) {
    g_swapchain3->Release();
    g_swapchain3 = nullptr;
  }
  g_swapchain = nullptr;
  g_srv_next = 0;
  g_fence_value = 0;
  g_frame_index = 0;
}

}  // namespace overlay::render::dx12
