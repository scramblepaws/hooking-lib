# Widgets are behavior plus a replaceable Painter

The framework owns Widget behavior (state, hit-testing, hover/drag/focus, keybind capture, persistence) and ships neutral default Painters so widgets work out of the box, but every Widget's painting can be replaced by a user-supplied Painter receiving the Widget's state and a Canvas. Luau scripts are first-class Painter hosts from the first release.

Considered alternatives: behavior-only shells (rejected: users face a blank screen and every project re-solves hover/drag feedback); fully styled built-in widgets (rejected: forces one look, contradicts per-project theming and the GameSense-style requirement); primitives-only (rejected: no widget semantics, every project re-implements lists, keybinds, persistence).

Consequence: the Painter/Canvas API is a public surface from day one and must stay stable; deferred ideas like the skewed panel transform and custom widget effects ride on it rather than changing it.
