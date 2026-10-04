# Wrap Dear ImGui instead of building a renderer

We wrap Dear ImGui (pinned to a known docking-branch commit) for the renderer, widget set, and Win32/DX11/DX12/Vulkan backends, adding our own facade, Retained Layer, theming, scripting, and hardening on top. A from-scratch immediate-mode engine would take months to reach parity minus a decade of battle-tested bug fixes, and writing our own renderer is exactly where the instability we are trying to avoid comes from.

Considered alternatives: forking Dear ImGui in-tree (accepted merge debt for no benefit yet); building a new engine (rejected as the obvious-but-wrong path). Revisit only if a required behavior demonstrably cannot be achieved from ImGui's public API.
