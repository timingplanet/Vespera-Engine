# Vespera Engine 1.0 limitations

Vespera 1.0 is intentionally focused. These boundaries are part of the 1.0
product scope rather than hidden unfinished features.

## Platform and renderer

- Windows x64 is the supported desktop platform for 1.0.
- Direct3D 12 is the supported renderer for 1.0.
- Vulkan and Linux support are post-1.0 work.

## 2D

The **2D / UI Foundation (Experimental)** template is a real playable
screen-space starter built with RmlUi and project-owned C#. It is useful for
UI-heavy games, card and board games, puzzle games, HUD-driven games, and
screen-space prototypes.

Vespera 1.0 does **not** provide a complete orthographic world-authoring mode,
dedicated 2D scene gizmos, or a full 2D physics/editor workflow. Those are
post-1.0 capabilities.

## Runtime UI

- RmlUi is the recommended production UI path.
- One active RmlUi surface per process is supported in 1.0.
- A dedicated visual RML/RCSS layout editor is not included in 1.0.
- Legacy `.slui` remains for compatibility and QA, not as the recommended new-project UI path.

## Scripting

- C# is the primary full-game scripting API.
- Lua is project-level lightweight scripting with `Start`, `Update`, and `Stop`.
- Per-entity Lua components and Lua `require`/module workflows are not part of 1.0.

## Assets and project moves

Moves performed through Vespera's controlled asset workflow preserve stable
references and repair supported fallbacks. Arbitrary moves performed directly
through the operating-system filesystem are not guaranteed to be recoverable,
particularly for RML/RCSS path relationships.

## Automation

MCP/editor automation is included as a developer-preview workflow. It is useful
for QA and tooling, but its tool surface is not yet promised as a permanently
frozen 1.x compatibility contract.

## Not 1.0 blockers

The following are deliberately post-1.0: hardware ray tracing, upscalers,
visual scripting, Vulkan, Linux, a large multiplayer framework, advanced
physics, full orthographic 2D authoring, and multiple simultaneous RmlUi
surfaces.
