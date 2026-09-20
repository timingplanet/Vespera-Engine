# Editor Guide

The Vespera Editor is organized around a project, an authored scene, project assets, and a runtime Play state. The safest workflow is to perform normal authoring through the editor rather than hand-editing serialized files.

## Project Hub and opening projects

Use `./vespera.ps1` to open the Project Hub. Create a starter project or open an existing `.vesperaproject` workspace.

Inside the editor, **File → Open Project…** changes workspaces. **File → Open Scene…** opens a scene directly. Saving uses `Ctrl+S`; Save As uses `Ctrl+Shift+S`.

## Hierarchy

The Hierarchy contains sectors and entities. Use it to:

- create empty entities, primitive mesh entities, sprite entities, trigger volumes, point lights, and sectors;
- select and reorder authored objects;
- parent, unparent, duplicate, and delete entities;
- access prefab Apply, Revert, and Unpack actions for prefab instances.

Useful shortcuts include `Ctrl+D` for duplicate and `Delete` for deletion.

## Scene view

The Scene view is for authoring. It supports View, Move, Rotate, and Scale tools.

When the Scene view is focused, the tool hotkeys are:

- `Q` — View
- `W` — Move
- `E` — Rotate
- `R` — Scale
- `F` — frame/focus the current authoring view

The free camera uses WASD for horizontal movement, `Q`/`E` vertically, and Shift for faster movement while navigating.

## Inspector

The Inspector edits the selected entity, component, asset, material, clip, or other authoring object.

For entities, the Inspector exposes identity fields such as name, tag, layer, enabled state, Transform, built-in components, prefab source state, and attached C# components. Fields marked with `[Expose]` in project C# appear here after managed metadata is built/refreshed.

## Project / Assets

The Project browser displays the Asset Catalog. Depending on type, you can open a scene, instantiate a prefab, open UI authoring, inspect import settings, copy a stable asset ID or relative path, and run **Move / Rename…** or delete-safety checks.

Use **Refresh Asset Catalog** after external filesystem changes. Prefer controlled editor moves for referenced assets.

## Play Mode

Play runs the current game state through the runtime path. Project input bindings drive Play Mode just as they drive standalone builds.

Use Stop to return to edit state. For gameplay changes, build project C# and re-enter Play as needed. Vespera preserves a last-good managed build when a new C# build fails so a compiler error does not replace working output.

## Project Settings

Project Settings contains the game's startup/runtime/build configuration. Important sections include:

- **General** — project name and project paths.
- **Startup & Runtime** — startup scene, startup RML UI, optional Lua entry, window settings, relative mouse, Escape behavior, VSync.
- **Input Actions** — key, gamepad button, and gamepad axis bindings with scale and deadzone.
- **Build & Package** — company/product metadata, executable name, build output, and .NET deployment mode.

## Build Game

Use **Build → Build Game…** or `Ctrl+Shift+B`. The Build menu also provides direct Development and Release actions plus Build & Run for Development.

A normal game developer should use this editor workflow. The PowerShell export scripts are useful for automation and source-tree workflows, but they are not required for ordinary project authoring.
