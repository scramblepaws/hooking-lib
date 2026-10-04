# Both immediate and retained public APIs

The library exposes an Immediate Layer (frame-driven calls, RAII scopes) and a Retained Layer (persistent Widget objects with Properties and Tweens). Both sit on one internal widget-state tree. The Retained Layer is what Stylesheets, config serialization, and the Luau Script Host bind to, because those need stable widget identity; the Immediate Layer is the escape hatch for native frame-driven code.

Considered alternatives: immediate-only (rejected: no stable identity for scripting, tweens, or persistence); retained-only (rejected: hostile to frame-driven native code and unlike the ImGui model users bring with them).
