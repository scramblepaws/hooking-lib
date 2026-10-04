#pragma once

namespace overlay::hook {

// Creates the trampolines on the DXGI swapchain vtable and spawns the eject worker.
bool install();

// Marks this module as host-linked (not injected): eject removes hooks but keeps
// the module loaded instead of unloading it.
void mark_host_mode();

// Hooks ExecuteCommandLists on the given D3D12 device's queue vtable so the
// renderer can submit on the Target's own queue. `device` is an ID3D12Device*.
bool install_dx12_queue_capture(void* device);

}  // namespace overlay::hook
