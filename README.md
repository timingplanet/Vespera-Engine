# Vespera Engine

**Version:** `1.0.0`  
**Supported 1.0 platform:** Windows x64 + Direct3D 12

Vespera is an open-source C++20 game engine with a professional editor,
project-owned C# gameplay scripting, optional lightweight Lua, RmlUi runtime
UI, scenes, prefabs, stable asset references, audio, input, save data, and a
shared standalone player/export path.

Vespera 1.0 is focused on one complete Windows shipping workflow rather than a
large collection of partially supported platforms or subsystems.

## Quick start

Build Vespera from a fresh source checkout:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\build.ps1
```

Open the Project Hub:

```powershell
.\vespera.ps1
```

Create a project, open it in the editor, press **Play**, then use
**Build → Build Game…** when you are ready to produce a standalone build.

For the guided path, read:

1. [Getting Started](docs/GETTING_STARTED.md)
2. [Your First Vespera Game](docs/FIRST_GAME.md)
3. [Build & Ship](docs/BUILD_AND_SHIP.md)

A dependency-free HTML documentation site is included at [`docs/index.html`](docs/index.html).
The included GitHub Pages workflow publishes the `docs/` directory directly.

## Starter projects

### 3D / 2.5D Game

The primary 1.0 starter. It includes a sector-world scene, project-owned C#
controller code, collision, input, lighting, sprites, and the normal shared
player/export workflow.

### 2D / UI Foundation (Experimental)

A distinct playable screen-space 2D starter. RmlUi is the canvas and
project-owned C# handles movement, game state, scoring, input, and UI updates.
It is useful for card games, board/grid games, puzzle games, HUD-heavy games,
and other screen-space projects.

Vespera 1.0 does **not** claim a complete orthographic world-authoring mode,
dedicated 2D scene tooling, or a full 2D physics/editor workflow. See
[1.0 Limitations](docs/LIMITATIONS.md).

### Empty Project

A blank scene with a minimal project-owned C# bootstrap and no hidden gameplay.

## 1.0 feature set

- Native C++20 engine and editor
- Windows x64 / Direct3D 12 runtime
- Project Hub and starter templates
- Entity/component scene model
- Sector-based 2.5D world authoring
- Sprite-first rendering with optional mesh content
- Point lights, collision, triggers, overlap queries, and raycasts
- C# components with editor-exposed fields and lifecycle support
- Optional project-level Lua 5.4.9 scripting
- RmlUi 6.2 runtime UI with project-owned RML/RCSS
- Input actions, audio, timers, save data, scenes, prefabs, spawning/destruction
- Stable asset IDs and controlled move/rename repair
- Editor Play Mode
- Build Game workflow for Debug, Development, and Release
- Framework-dependent or portable private .NET deployment
- Release GUI executable behavior with persistent runtime/crash logs
- Localhost editor/runtime automation and MCP developer-preview tooling

## Build and test

Lightweight behavioral tests:

```powershell
.\test.ps1
```

Run the editor normally:

```powershell
.\run.ps1
```

Run the editor with localhost automation enabled:

```powershell
.\run.ps1 -Automation
```

Whole-checkout release-candidate validation:

```powershell
.\tools\run-rc-gate.ps1
```

The RC gate performs the ordinary release/public workflow and then reuses the
Release build for repeated Play/Stop, saved-scene switching, managed compiler
recovery, stable-reference move/save/reopen checks, runtime automation, MCP
managed-build coverage, and bounded entity/component stress.

## Standalone samples

- `examples/emberlight_guild` — UI-heavy sample game using C#, RmlUi, SaveData,
  scenes, assets, and the ordinary shared-player/export workflow.
- `examples/performance_lab` — repeatable benchmark/sample project exercising
  normal C#, UI, scene, entity, lighting, culling, and runtime telemetry paths.
- `examples/reference_game` — adversarial QA/reference project used by the test
  and automation tooling rather than a normal starter game.

## Documentation

- [Editor](docs/EDITOR.md)
- [Gameplay API](docs/GAMEPLAY_API.md)
- [Managed C#](docs/MANAGED_SCRIPTING.md)
- [Lua](docs/LUA_SCRIPTING.md)
- [Input](docs/INPUT.md)
- [RmlUi](docs/RMLUI.md)
- [Asset pipeline](docs/ASSET_PIPELINE.md)
- [Sprites](docs/SPRITES.md)
- [Exporting](docs/EXPORTING.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Testing](docs/TESTING.md)
- [1.0 Limitations](docs/LIMITATIONS.md)
- [Roadmap](docs/ROADMAP.md)

## 1.0 boundaries

Vespera 1.0 intentionally does not promise Vulkan/Linux, full orthographic 2D
world authoring, visual scripting, multiple simultaneous RmlUi surfaces,
per-entity Lua, advanced physics, hardware ray tracing, or upscaler integrations.
Those are post-1.0 directions, not hidden release requirements.

## License

Vespera Engine is released under the [MIT License](LICENSE).
Third-party dependencies retain their own licenses; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Copyright © 2026 Timingplanet.
