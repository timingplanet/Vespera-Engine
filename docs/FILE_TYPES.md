# File Types

This page is a practical map of the files you will encounter while using Vespera.

## Project and world files

| Extension | Purpose |
|---|---|
| `.vesperaproject` | Project-wide settings, startup assets, input bindings, scripting/build configuration. |
| `.slscene` | Scene Text file containing world/sector data, entities, components, script attachments, and scene resources. |
| `.slprefab` | Reusable entity prefab. Instances receive fresh scene entity IDs. |
| `.vmeta` | Asset metadata sidecar containing the stable asset ID and importer settings. |

## Content assets

| Extension | Purpose |
|---|---|
| `.slmat` | Material asset. Can reference a base texture by stable asset identity. |
| `.slspriteclip` | Sprite animation clip. |
| `.slspritesheet` | Sprite-sheet descriptor pointing at a source texture. |
| `.slaudio` | Audio-clip descriptor. The current clip importer resolves a WAV source asset. |
| `.png`, `.jpg`, `.jpeg`, `.bmp`, `.tga` | Texture sources recognized by the asset catalog. |
| `.wav`, `.ogg`, `.mp3` | Audio sources recognized by the asset catalog. Direct engine audio playback support depends on the runtime path you use; `.slaudio` currently resolves WAV sources. |
| `.ttf`, `.otf` | Font sources. |

## UI files

| Extension | Purpose |
|---|---|
| `.rml` | RmlUi document for production runtime UI. |
| `.rcss` | RmlUi style sheet. |
| `.slui` | Legacy Vespera UI document. Kept for compatibility and tooling; RmlUi is the production UI path. |

## Gameplay code

| Extension | Purpose |
|---|---|
| `.cs` | Project-owned C# gameplay code using `Vespera.NET`. |
| `.csproj` | Managed project built into the game's configured assembly. |
| `.lua` | Optional project-level Lua entry script. |

## Generated package files

A standalone export writes a package manifest and package report beside the built game. These files describe what Vespera packaged and are useful when diagnosing missing assets or deployment problems.

<div class="notice"><strong>Stable identity matters:</strong> when moving assets, use the editor's <strong>Move / Rename…</strong> workflow where possible. It can preserve stable IDs and repair references that would otherwise become stale.</div>
