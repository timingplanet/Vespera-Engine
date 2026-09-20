# Current Limitations

Vespera deliberately supports a focused set of workflows. These boundaries are useful to know before designing around a feature the engine does not currently provide.

## Platform and renderer

- Windows x64 + Direct3D 12 remains the production-supported path.
- Vulkan is a preview backend and is not yet production-certified for full feature/visual parity with D3D12.
- Linux + Vulkan is experimental and receives automated/software-rendered validation, but broad distro/GPU/driver coverage is not yet claimed.

## 2D

The **2D / UI Foundation (Experimental)** is screen-space RmlUi plus C#. Vespera does not currently provide a complete orthographic 2D world editor, tilemap workflow, dedicated 2D scene gizmos, or a full 2D physics stack.

## Lua

Lua is project-level, not a per-entity component system. It provides a constrained semantic runtime environment and does not expose unrestricted standard-library filesystem/process/debug facilities.

Use C# for rich per-entity components and Inspector-authored gameplay.

## UI

RmlUi is the production runtime UI surface. Vespera is designed around one active startup/runtime RML surface rather than a general multi-surface browser-style UI system.

Legacy `.slui` remains for compatibility/tooling.

## Physics

The built-in gameplay physics surface is intentionally small: Cylinder Collider movement/trigger behavior plus 2D-style raycast, overlap-circle, and motion-resolution helpers. It is not a general rigid-body physics engine.

## Rendering features

Hardware ray tracing, DLSS/FSR-style upscalers, and advanced global-illumination systems are not part of the current production workflow.

## Visual scripting and multiplayer

There is no production visual-scripting graph system or large built-in multiplayer framework.

## Editor extensions

Vespera has a semantic extension registration surface, but arbitrary third-party native DLL/plugin loading is not currently the normal public extension workflow.

## Automation

Editor/runtime automation and MCP are developer-preview tooling intended for local workflows. Normal shipping games do not expose the automation endpoint unless explicitly launched for automation.
