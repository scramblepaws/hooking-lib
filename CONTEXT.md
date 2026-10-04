# Overlay Library

An internal library that renders a scriptable UI (Luau, Roblox-style) on top of a game or app process, across DX11/DX12/Vulkan, in injected and external modes.

## Language

**Overlay**:
An instance of this library's UI attached to one Target. _Avoid_: instance, menu, GUI.

**Context**:
The runtime object owning one Overlay's state, including its own ImGui context. _Avoid_: runtime, singleton.

**Target**:
The process or window the Overlay attaches to. _Avoid_: game, host.

**Backend**:
The graphics API adapter (DX11, DX12, Vulkan) used to render an Overlay. _Avoid_: renderer, API.

**Hook**:
The trampoline patching of a Target function (e.g. `Present`) that drives an Overlay. _Avoid_: patch, detour.

**Inject**:
Loading the overlay module into a Target. _Avoid_: attach.

**Attach**:
The Overlay connecting to a Target's swapchain or window. _Avoid_: inject.

**Eject**:
Tearing down hooks and unloading the overlay module from a Target. _Avoid_: unload, detach.

**Immediate Layer**:
The public API where widgets are called every frame. _Avoid_: raw layer, IMGUI API.

**Retained Layer**:
The public API where widgets are persistent objects with Properties. _Avoid_: scene graph, object layer.

**Widget**:
A UI element exposed by the Immediate or Retained Layer. _Avoid_: control, component.

**Property**:
A named, typed, tweenable attribute of a Widget (color, size, text...). _Avoid_: field, attribute.

**Tween**:
An animation interpolating a Property toward a target over time. _Avoid_: animation.

**Stylesheet**:
CSS-subset source text that assigns Properties to widget classes and ids. _Avoid_: theme, skin.

**Theme**:
The resolved set of default Property values for an Overlay. _Avoid_: stylesheet, skin.

**Script Host**:
The embedded Luau VM that executes user scripts against the Retained Layer. _Avoid_: runtime, VM.

**Sandbox**:
The default-deny rule set a script runs under. _Avoid_: jail, permission system.

**Capability Grant**:
A per-script permission enabling a Sandbox capability (scoped filesystem, unrestricted filesystem, HTTP). _Avoid_: permission, access level.

**Drawing**:
Script-facing immediate draw primitives (Circle, Line, Text, Image...) rendered above regular UI. _Avoid_: paint, ESP layer.

**Canvas**:
The backend-agnostic drawing surface a Painter draws into (rects, text, lines, gradients, images, clipping). Distinct from Drawing. _Avoid_: draw list, drawlist, pipeline.

**Painter**:
A function that draws one Widget into a Canvas from the Widget's state and Properties, replacing the framework's default drawing. _Avoid_: renderer, skin.

**Toast**:
A transient notification shown stacked in a screen corner. _Avoid_: popup, alert.

**Event Feed**:
The persistent bottom overlay line showing recent game events emitted by scripts. _Avoid_: console, log, chat.

**Keybind**:
A Property kind whose value is a key or mouse combination, captured by pressing it. _Avoid_: hotkey, shortcut.

**Config**:
A named, persisted snapshot of Widget Properties, saveable and loadable per Overlay. _Avoid_: profile, settings.

**Service Stub**:
An inert fake of a Roblox service returned by `game:GetService` for UNC/SUNC compatibility. _Avoid_: mock, shim.

**UNC/SUNC compatibility**:
The explicitly bounded subset of executor standard-library naming this library implements; not full executor parity. _Avoid_: executor support, Roblox compatibility.
