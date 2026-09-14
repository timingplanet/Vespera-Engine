# Getting Started with Vespera

This guide is the shortest path from a fresh Vespera source checkout to an editable project. It describes the current **Windows / Direct3D 12** workflow.

## 1. Build Vespera once

Open PowerShell in the Vespera checkout and run:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\build.ps1
```

Vespera's build scripts locate the Visual Studio-bundled CMake automatically. A successful build produces the editor, Project Hub, shared `vespera_player`, reference QA game and packager.

For later engine regression work, the cheap behavioral suite is available separately:

```powershell
.\test.ps1
```

Normal game development does not require running the QA suite.

## 2. Open the Project Hub

```powershell
.\vespera.ps1
```

Choose a starter:

- **3D / 2.5D Game** — recommended first project. It includes one sector room, a point light, project-owned C# starter gameplay and RmlUi HUD.
- **2D / UI Foundation (Experimental)** — playable screen-space 2D starter with project-owned C# and RmlUi. It includes WASD/arrow movement, a simple collect/reset loop, and a `START-HERE.md` walkthrough. Vespera 1.0 still does not advertise a complete dedicated orthographic world-authoring workflow.
- **Empty Project** — minimal valid project and scene with no hidden gameplay assumptions.

Pick a project name and location, then create the project. The editor opens the copied project rather than the Vespera reference QA project.

## 3. Know the main editor areas

Vespera follows a familiar game-editor layout:

- **Hierarchy** — scene entities and parent/child structure.
- **Scene / Sector views** — world authoring and 2.5D sector editing.
- **Game** — Play Mode output and project RmlUi.
- **Inspector** — selected entity/component/project properties.
- **Project** — authored assets and RML/RCSS source editing.
- **Console** — validation, managed build and runtime diagnostics.

Press **Ctrl+P** to enter/leave Play Mode, or use the Play controls in the editor.

## 4. Scripts and UI

C# is Vespera's primary full-game scripting path. Starter projects own their `.csproj` and source files; they do not borrow the reference game's assembly.

Project v10 may also name one lightweight Lua entry script. Lua is intended for small runtime/mod logic and runs alongside C# through the same gameplay concepts rather than replacing C#.

New runtime UI should use **RmlUi** (`.rml` + `.rcss`). Select those files in the Project browser to edit their source and inspect dependencies. Legacy `.slui` remains for compatibility/QA only.

## 5. Build the game from Vespera

You should not need a terminal for normal standalone builds.

Open **Build -> Build Game...** or press **Ctrl+Shift+B**. Choose Debug, Development or Release, inspect the preflight details, then use **Build** or **Build & Run**.

Vespera packages only the project's deterministic asset closure, project-owned managed assembly when configured, and either the shared `vespera_player` or an authored custom runtime target.

Continue with [Your First Game](FIRST_GAME.md), then [Build & Ship](BUILD_AND_SHIP.md).

## Current support boundary

Vespera's current validated shipping target is **Windows x64 / Direct3D 12**. Vulkan/Linux and other larger platform work are post-1.0 roadmap items unless promoted later by testing.

## Explore a complete sample

Open `examples/emberlight_guild/EmberlightGuild.vesperaproject` through **Open Existing Project** to inspect a UI-heavy sample that uses project-owned C#, external RML/RCSS, SaveData and the same shared-player/build path as a normal game. It is intentionally separate from Vespera's QA reference project.
