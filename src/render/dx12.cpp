#include "render/dx12.h"

#include <windows.h>
#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_4.h>

#include <cstdlib>
#include <cstring>
#include <vector>

#include "core/log.h"
#include "core/state.h"

#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

namespace overlay::render::dx12 {

namespace {

constexpr int kFramesInFlight = 3;
// The tail of the shader-visible heap is reserved for overlay (Drawing) images;
// ImGui's runtime texture allocator rotates through the head.
constexpr int kSrvHeapCount = 256;
constexpr int kOverlaySrvCount = 128;
constexpr int kOverlaySrvBase = kSrvHeapCount - kOverlaySrvCount;

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
int g_overlay_srv_next = 0;
std::vector<ID3D12Resource*> g_overlay_textures;

void srv_alloc(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
  auto* heap = info->SrvDescriptorHeap;
  const UINT size = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = heap->GetCPUDescriptorHandleForHeapStart();
  D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle = heap->GetGPUDescriptorHandleForHeapStart();
  cpu_handle.ptr += static_cast<SIZE_T>(size) * g_srv_next;
  gpu_handle.ptr += static_cast<UINT64>(size) * g_srv_next;
  g_srv_next = (g_srv_next + 1) % kOverlaySrvBase;
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

bool ready() { return g_device != nullptr; }

bool create_target(IDXGISwapChain* sc) {
  g_swapchain = sc;
  return make_targets(sc);
}

void release_target() {
  for (ID3D12Resource* buffer : g_buffers)
    if (buffer) buffer->Release();
  g_buffers.clear();
}

bool init(IDXGISwapChain* sc, void*) {
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

void* create_texture(const void* rgba, uint32_t width, uint32_t height) {
  if (!g_device || !g_queue || !g_srv_heap || !g_fence || !rgba || width == 0 || height == 0) return nullptr;
  if (g_overlay_srv_next >= kOverlaySrvCount) {
    static bool logged = false;
    if (!logged) {
      logged = true;
      log("dx12: overlay texture descriptors exhausted (%d)", kOverlaySrvCount);
    }
    return nullptr;
  }

  const UINT64 row_pitch = (static_cast<UINT64>(width) * 4 + 255) & ~static_cast<UINT64>(255);
  const UINT64 upload_size = row_pitch * height;

  D3D12_HEAP_PROPERTIES default_heap = {};
  default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC texture_desc = {};
  texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texture_desc.Width = width;
  texture_desc.Height = height;
  texture_desc.DepthOrArraySize = 1;
  texture_desc.MipLevels = 1;
  texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  texture_desc.SampleDesc.Count = 1;
  ID3D12Resource* texture = nullptr;
  if (FAILED(g_device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &texture_desc,
                                               D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                               __uuidof(ID3D12Resource), reinterpret_cast<void**>(&texture))))
    return nullptr;

  D3D12_HEAP_PROPERTIES upload_heap = {};
  upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC buffer_desc = {};
  buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buffer_desc.Width = upload_size;
  buffer_desc.Height = 1;
  buffer_desc.DepthOrArraySize = 1;
  buffer_desc.MipLevels = 1;
  buffer_desc.Format = DXGI_FORMAT_UNKNOWN;
  buffer_desc.SampleDesc.Count = 1;
  buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ID3D12Resource* upload = nullptr;
  if (FAILED(g_device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               __uuidof(ID3D12Resource), reinterpret_cast<void**>(&upload)))) {
    texture->Release();
    return nullptr;
  }

  void* mapped = nullptr;
  if (FAILED(upload->Map(0, nullptr, &mapped)) || !mapped) {
    upload->Release();
    texture->Release();
    return nullptr;
  }
  for (uint32_t y = 0; y < height; ++y)
    std::memcpy(static_cast<unsigned char*>(mapped) + y * row_pitch,
                static_cast<const unsigned char*>(rgba) + static_cast<size_t>(y) * width * 4, width * 4);
  upload->Unmap(0, nullptr);

  ID3D12CommandAllocator* allocator = nullptr;
  ID3D12GraphicsCommandList* list = nullptr;
  bool recorded = false;
  if (SUCCEEDED(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
                                                 reinterpret_cast<void**>(&allocator))) &&
      allocator &&
      SUCCEEDED(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr,
                                            __uuidof(ID3D12GraphicsCommandList), reinterpret_cast<void**>(&list))) &&
      list) {
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = texture;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = upload;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    src.PlacedFootprint.Footprint.Width = width;
    src.PlacedFootprint.Footprint.Height = height;
    src.PlacedFootprint.Footprint.Depth = 1;
    src.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(row_pitch);
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = texture;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    list->ResourceBarrier(1, &barrier);
    recorded = SUCCEEDED(list->Close());
  }
  if (recorded) {
    ID3D12CommandList* lists[] = {list};
    g_queue->ExecuteCommandLists(1, lists);
    ++g_fence_value;
    g_queue->Signal(g_fence, g_fence_value);
    if (g_fence->GetCompletedValue() < g_fence_value) {
      g_fence->SetEventOnCompletion(g_fence_value, g_fence_event);
      WaitForSingleObject(g_fence_event, INFINITE);
    }
  }
  if (list) list->Release();
  if (allocator) allocator->Release();
  upload->Release();
  if (!recorded) {
    texture->Release();
    return nullptr;
  }

  const UINT descriptor_size = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  const UINT slot = kOverlaySrvBase + static_cast<UINT>(g_overlay_srv_next++);
  D3D12_CPU_DESCRIPTOR_HANDLE cpu = g_srv_heap->GetCPUDescriptorHandleForHeapStart();
  cpu.ptr += static_cast<SIZE_T>(descriptor_size) * slot;
  D3D12_SHADER_RESOURCE_VIEW_DESC view = {};
  view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  view.Texture2D.MipLevels = 1;
  g_device->CreateShaderResourceView(texture, &view, cpu);
  D3D12_GPU_DESCRIPTOR_HANDLE gpu = g_srv_heap->GetGPUDescriptorHandleForHeapStart();
  gpu.ptr += static_cast<UINT64>(descriptor_size) * slot;
  g_overlay_textures.push_back(texture);
  return reinterpret_cast<void*>(static_cast<uintptr_t>(gpu.ptr));
}

void shutdown() {
  if (!g_device) return;
  ImGui_ImplDX12_Shutdown();
  release_target();
  for (ID3D12Resource* texture : g_overlay_textures)
    if (texture) texture->Release();
  g_overlay_textures.clear();
  g_overlay_srv_next = 0;
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
