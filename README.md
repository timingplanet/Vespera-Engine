# Vespera Engine

**Version:** `1.1.0`  
**Production platform:** Windows x64 + Direct3D 12

Vespera is an open-source C++20 game engine built around a native editor, C# gameplay scripting, optional Lua, RmlUi, scenes, prefabs, audio, input, save data, and a standalone game export workflow.

The Windows + Direct3D 12 path is the production baseline. Vulkan is available as an opt-in preview backend on Windows, and Linux + Vulkan remains experimental while cross-platform validation continues.

## Highlights

- Native C++20 engine and editor
- Windows x64 / Direct3D 12 production renderer
- Optional Vulkan renderer for parity testing
- Project-owned C# gameplay scripting
- Optional project-level Lua scripting
- Entity/component scenes with sector-based 2.5D world authoring
- Sprites, materials, textures, point lights, collision, triggers, overlaps, and raycasts
- RmlUi runtime UI with project-owned RML/RCSS
- Prefabs, scenes, input actions, audio, timers, save data, and runtime spawning/destruction
- Stable asset IDs with move/rename repair
- Editor Play Mode
- Debug, Development, and Release game builds
- Framework-dependent or portable private .NET deployment
- Localhost editor/runtime automation and MCP developer tooling

## Quick start

Build Vespera from source:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\build.ps1
```

Open the Project Hub:

```powershell
.\vespera.ps1
```

Create a project, open it in the editor, press **Play**, and use **Build → Build Game…** when you are ready to make a standalone build.

For the guided setup, see:

1. [Getting Started](docs/GETTING_STARTED.md)
2. [Your First Vespera Game](docs/FIRST_GAME.md)
3. [Build & Ship](docs/BUILD_AND_SHIP.md)

A dependency-free documentation site is also included in [`website/`](website/) and can be published with GitHub Pages.

## Platform status

| Platform / renderer | Status |
| --- | --- |
| Windows x64 + Direct3D 12 | Production-supported |
| Windows x64 + Vulkan | Preview / parity testing |
| Linux + Vulkan | Experimental |

D3D12 remains the default renderer on Windows. To run the reference game with Vulkan:

```powershell
.\run-game.ps1 -Renderer vulkan
```

Use `-Renderer d3d12` to force Direct3D 12 or `-Renderer auto` to use the platform default.

On Linux, the current development path is:

```bash
./build-linux.sh
./run-game-linux.sh
```

Linux support is still experimental and should not be treated as production-certified across distributions, GPUs, or drivers.

## Starter projects

### 3D / 2.5D Game

The main starter project. It includes a sector-world scene, project-owned C# controller code, collision, input, lighting, sprites, and the normal standalone export workflow.

### 2D / UI Foundation (Experimental)

A screen-space 2D starter built around RmlUi and C#. It works well for card games, board/grid games, puzzle games, HUD-heavy projects, and other UI-driven games.

It is **not** a complete orthographic world-authoring mode. Vespera does not currently ship a tilemap workflow, dedicated 2D scene tools, or a full 2D physics stack. See [Current Limitations](docs/LIMITATIONS.md).

### Empty Project

A blank scene with a minimal project-owned C# bootstrap and no hidden gameplay.

## Build and test

Run the lightweight behavioral tests:

```powershell
.\test.ps1
```

Run the editor:

```powershell
.\run.ps1
```

Run the editor with localhost automation enabled:

```powershell
.\run.ps1 -Automation
```

Run the full Windows release validation:

```powershell
.\test-rc.ps1
```

The release validation runner performs the automated release/export/editor/runtime checks first, then launches D3D12 and Vulkan editor/game smoke tests for manual visual confirmation.

## Included samples

- `examples/emberlight_guild` — UI-heavy sample game using C#, RmlUi, SaveData, scenes, assets, and the shared player/export workflow.
- `examples/performance_lab` — benchmark/sample project covering C#, UI, scenes, entities, lighting, culling, and runtime telemetry.
- `examples/reference_game` — reference and stress-test project used by the test and automation tooling.

## Documentation

- [Editor](docs/EDITOR.md)
- [Gameplay API](docs/GAMEPLAY_API.md)
- [Managed C#](docs/MANAGED_SCRIPTING.md)
- [Lua](docs/LUA_SCRIPTING.md)
- [Input](docs/INPUT.md)
- [RmlUi](docs/RMLUI.md)
- [Asset Pipeline](docs/ASSET_PIPELINE.md)
- [Sprites](docs/SPRITES.md)
- [Exporting](docs/EXPORTING.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Testing](docs/TESTING.md)
- [Current Limitations](docs/LIMITATIONS.md)
- [Roadmap](docs/ROADMAP.md)

## 💬 Join the Vespera Community

Join the **official Vespera Engine Discord** to stay up to date with the latest news, development updates, and releases.

👉 [**Join the Vespera Engine Discord**](https://discord.gg/8rJYSUnEkK)

## Current boundaries

Vespera does not currently claim production-certified Linux/Vulkan support, a complete orthographic 2D world editor, visual scripting, multiple simultaneous RmlUi surfaces, per-entity Lua components, advanced rigid-body physics, hardware ray tracing, or built-in upscaler integrations.

These are documented limits, not hidden requirements for the Windows/D3D12 production workflow.

## License

Vespera Engine is released under the [MIT License](LICENSE). Third-party dependencies retain their own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Copyright © 2026 Timingplanet.
