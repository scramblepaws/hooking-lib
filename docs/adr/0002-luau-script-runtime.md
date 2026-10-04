# Luau as the script runtime

User-facing UI is scripted in Luau embedded in the C++ core. Luau gives us Roblox-compatible syntax our target users already know, first-class sandboxing and interrupt support for untrusted scripts, and a small C++ embedding surface that builds with the same toolchain as the rest of the library.

Considered alternatives: Lua 5.4 (rejected: no Roblox syntax, weaker sandboxing primitives); LuaJIT (rejected: dormant project, no default sandboxing, painful x64 Windows embedding under an injected DLL).
