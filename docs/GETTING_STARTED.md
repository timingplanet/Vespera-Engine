# Getting Started

Vespera's production-supported path is **Windows x64 + Direct3D 12**. Vulkan is available as a preview backend on Windows, while Linux + Vulkan remains experimental. The normal workflow is: build the engine once, use the Project Hub to create or open a project, work in the editor, test with Play Mode, then use **Build → Build Game…** for a standalone build.

## What you need

For a source build, install:

- Visual Studio 2022 or 2026 with C++ desktop/CMake tooling.
- CMake. Vespera can use CMake from your PATH or the copy bundled with Visual Studio.
- The .NET 8+ SDK if you want C# gameplay scripting.

Vespera's build script will warn if .NET is missing. Native engine/editor binaries can still build, but C# gameplay will not be available until .NET is installed.

## Build the engine

Open PowerShell in the Vespera source folder:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\build.ps1
```

The script configures an x64 Visual Studio CMake build and builds the editor, Project Hub, shared player, reference game, and managed scripting support.

## Open the Project Hub

```powershell
.\vespera.ps1
```

Create one of the available starter projects:

- **3D / 2.5D Game** — the main starter for scene-based games.
- **2D / UI Foundation (Experimental)** — a screen-space game starter built around RmlUi and C#.
- **Empty Project** — a minimal scene and C# bootstrap.

The Hub creates project-owned assets and managed code. Your game does not borrow scripts from Vespera's reference project.

## Open the editor and play

Open your project from the Hub. The editor loads the project's startup scene, asset catalog, C# metadata, and project settings.

Use the **Play** controls to enter the runtime. Play Mode uses the same project input map and gameplay code used by standalone builds. Stop returns you to your authored scene state.

<div class="notice"><strong>Good first test:</strong> create the 3D starter, press Play, move the starter actor with WASD, then Stop. That proves the project, input map, C# assembly, scene, and runtime path are all connected.</div>

## Build a standalone game

Use **Build → Build Game…** or press `Ctrl+Shift+B`.

Choose a configuration and build. Vespera saves the project, validates the package closure, builds project C# when configured, packages the shared player and required assets, and writes the result to the project's build output directory.

For a build intended for another machine, choose **Portable (.NET bundled)** in Project Settings if you do not want to require a compatible .NET runtime on that machine.

## Where to go next

Read **Your First Game** for a small project walkthrough. Use **Project Structure** and **File Types** when you want to understand what Vespera stores on disk.
