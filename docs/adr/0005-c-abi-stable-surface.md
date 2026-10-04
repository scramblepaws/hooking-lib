# The stable product surface is a C ABI

The library is consumed as an installed SDK by C++ projects (MinGW and MSVC) and, later, by C# and the Luau bindings. Neither `std::function`, `std::string`, exceptions, nor compiler-specific layout can cross a DLL boundary safely between those consumers, so the durable contract is a C ABI in `include/overlay/overlay_c.h`: opaque handles, fixed-width types, C function pointers with `void*` user data, status enums, and no STL. C++ consumers get header-only RAII wrappers built on the same C functions, guaranteeing one implementation path for every language.

Considered alternatives: a C++-only SDK (rejected: ABI breaks across compilers and STL versions, blocks C# P/Invoke); source-drop/header-only consumption (rejected: no binary product, no scripting host reuse); COM-style interfaces (rejected: heavier for no benefit at this API size).

## ABI rules

- Stability begins at 1.0. During 0.x the headers may still change; every break is documented in the changelog.
- From 1.0: no signature changes, no field removals, no enum renumbering. Evolution is additive only.
- Every public struct starts with `uint32_t size` = `sizeof(struct)`. The library reads only fields covered by `size`; new fields are appended, existing ones are never reused.
- Strings crossing the boundary are copied by the library; returned strings are two-call (buffer, capacity, required size) or have documented process lifetime.
- Allocations are library-owned; consumers never free library memory except through provided functions.
- Callbacks are C function pointers plus a `void*` user pointer. No exceptions may cross the boundary; all entry points catch internally and return `OverlayStatus`.
- Enums gain new members at the end only; sentinel members (`OVERLAY_*_COUNT`) exist for range checks.
- `OVERLAY_ABI_VERSION` increments on any break; consumers can check it at runtime against the header they compiled with.

## Consequences

- The widget/painter API is designed once, up front, because it is the hardest part to evolve (painter callbacks and canvas drawing are the most consumer-facing surface).
- Any new feature (config, notifications, scripting) must be expressible in C first; the C++ wrapper cannot lead.
- The Luau bindings and the future C# package target the same functions, so behavior stays identical across languages.
- Minor ABI discipline cost: no templates, no overloads, slightly more verbose internal glue.
