# Project Structure

A Vespera game is a project folder with one `.vesperaproject` file plus project-owned assets and, when used, managed C# code.

A typical project looks like this:

```text
MyGame/
├─ MyGame.vesperaproject
├─ assets/
│  ├─ scenes/
│  │  └─ main.slscene
│  ├─ prefabs/
│  ├─ textures/
│  ├─ audio/
│  ├─ ui/
│  │  ├─ main.rml
│  │  └─ theme.rcss
│  └─ scripts/
│     └─ game.lua
├─ managed/
│  ├─ VesperaGame.Scripts.csproj
│  └─ Game.cs
└─ builds/
```

The exact folder names under `assets/` are your choice. Vespera treats the asset root configured by the project as the catalog boundary.

## The `.vesperaproject` file

The project file stores project-wide settings such as:

- project name and asset root;
- startup scene and startup RML document;
- optional Lua entry script;
- window title, size, resizing, mouse, Escape, and VSync behavior;
- input action bindings;
- managed C# project and assembly names;
- executable name and build output directory;
- framework-dependent or portable .NET deployment;
- build roots that must be included even when dependency scanning cannot infer them.

Most of these settings can be edited from the editor's **Project Settings** tab. Prefer the editor for routine changes so stable asset IDs and fallback paths stay synchronized.

## Assets and `.vmeta`

Files inside the asset root are indexed by the Asset Catalog. Supported assets receive a `.vmeta` sidecar containing a stable asset ID and importer metadata. Code and serialized assets can therefore keep a stable identity even if an asset is moved through Vespera's controlled move/rename workflow.

Do not casually delete `.vmeta` files. Regenerating metadata can change identity and break stable references.

## Managed code

C# gameplay belongs to the game's managed project, not to the engine source tree. Starter projects point their `.vesperaproject` file at a project-owned `.csproj` and assembly name.

Vespera builds the game assembly and reflects exposed component fields for the Inspector. The runtime loads that project assembly in Play Mode and in exported games.

## Build output

`builds/` is the default game-package output folder in the starter projects. It is not source content. A package contains the game executable, project assets selected by the build closure, managed output when used, legal/branding support files, and package reports.

Keep generated packages separate from `assets/` so they are never imported back into the project catalog.
