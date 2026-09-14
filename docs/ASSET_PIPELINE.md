# Vespera asset pipeline

## 0.8.8 Material assets

`.slmat` v1 is Vespera's first renderer-independent Material asset format:

```text
vespera_material 1
name "Concrete Lit"
shader "Vespera/Lit"
base_texture_asset "<stable-id>" "textures/concrete_brick.bmp"
base_color 0.92 0.96 1.0 1.0
emission 1.0 1.0 1.0 0.0
alpha_cutoff 0.5
end
```

Material source files receive normal `.vmeta` stable IDs. The catalog tracks Material -> Texture dependencies, and Scene/Prefab `mesh_material_asset` links become stable dependency edges. Fallback repair understands both levels, so moving a Material or its source texture can preserve identity and refresh readable paths.

The authored format deliberately contains no D3D12 descriptors, root parameters or compiled shader bytecode. `Vespera/Lit` and `Vespera/Unlit` are semantic built-in shader choices shared by current D3D12 and future backends. Custom shader assets/graphs and fuller PBR parameters remain later work.


0.7.x treats a game as a project workspace with stable source-asset identity. 0.7.6 made that identity authoritative in selected authored references; 0.7.7 adds safe authoring moves, fallback repair, and managed runtime asset handles over the same IDs.

## Project workspace v5

A project is described by a text `.vesperaproject` file. Stable-ID-first project roots were introduced in v4; v5 retains them and adds serialized semantic input bindings while remaining load-compatible with v1-v4:

```text
vespera_project 5
name "Vespera Reference Game"
assets "assets"
startup_scene_asset "6f0cf9d1daf63e7f10485c6f60ffd428" "scenes/connected_sectors.slscene"
managed_project "managed/ReferenceGame.Scripts.csproj"
managed_assembly "ReferenceGame.Scripts"
game_target "vespera_reference_game"
window_title "Vespera Reference Game"
window_width 1280
window_height 720
window_resizable 1
relative_mouse 1
escape_quits 1
input_bind "move_forward" "key" "W" 1.0 0.0
input_bind "move_forward" "key" "S" -1.0 0.0
build_include_asset "1ad1cecbe1042e6de07ab52d4ab3086c" "animations/watcher_walk.slspritesheet"
build_include_asset "bd302cfe220a824954093d0fe0ac9693" "audio/test_chime.slaudio"
end_project
```

Each stable record carries both an asset ID and a human-readable fallback path. The ID is authoritative when present. Path-only v1-v3 commands continue to load.

## `.vmeta` v2 identity and refresh cache

A current source sidecar looks like:

```text
vespera_meta 2
id "64c72394c4c9199bbb3258795e92140a"
importer "vespera.texture.bmp"
source_size 16438
source_hash "622f21249eb0a317"
source_mtime 1234567890
end_meta
```

The **asset ID** is durable identity. `source_hash` is the content fingerprint. `source_mtime` is only a refresh-cache hint.

Automatic source polling can reuse an existing content hash when all of these still match:

- metadata v2
- importer ID
- source byte size
- source modification stamp

If any differ, Vespera hashes the source again and updates metadata while preserving the asset ID. Explicit **Refresh / Reimport** uses `force_rehash` and recomputes content hashes even when the cache hint matches.

This makes lightweight source watching practical without redefining identity around filesystem timestamps. A copied project can simply rehash once when platform-specific timestamps differ.

## Metadata diagnostics

Catalog refresh reports now distinguish:

- created metadata
- updated metadata
- repaired invalid/duplicate metadata
- missing/unwritable metadata
- orphan `.vmeta` sidecars whose source no longer exists
- source hashes computed
- unchanged fast-path hits

Orphan metadata is reported, not automatically deleted. Cleanup remains an explicit future project-maintenance operation rather than a destructive background scan.

## Cataloged kinds

- Scene: `.slscene`
- Entity Prefab: `.slprefab`
- Texture: `.bmp`, `.tga`, `.png`, `.jpg`, `.jpeg`
- Audio: `.wav`, `.ogg`, `.mp3`
- Font: `.ttf`, `.otf`
- Material: `.slmat`

Recognition and decoding are separate. Importer IDs now distinguish implemented formats from pending ones rather than calling every non-BMP texture a generic pending texture.

## Decoded texture formats

### BMP

Uncompressed 24-bit BGR and 32-bit BGRA, top-down/bottom-up rows, RGBA8 output, with the existing all-zero BI_RGB alpha compatibility behavior.

### TGA

24/32-bit true-color type 2 and RLE type 10, all standard image-origin combinations, RGBA8 output and alpha preservation.

PNG/JPEG are decoded through Windows Imaging Component on the current Windows-first build. A portable decoder remains future work for Vulkan/Linux.

## Source watching vs cooked assets

0.7.1 automatic refresh updates the **source catalog** and metadata only. It is not yet a final cooked-artifact database and does not automatically rebuild every runtime GPU/audio resource in-place.

0.7.4-0.7.7 add dependency tracking, ID-first authored references, safe move/rename operations, and managed asset handles on top of this identity layer. Imported/cooked artifacts and wider scene/component reference migration should continue using the same asset IDs instead of inventing a second namespace.


## 0.7.4 dependency graph

After source discovery/metadata refresh, the catalog builds a lightweight dependency graph. Current tracked edges include scene/prefab texture references, scene prefab-source links, embedded scene sprite-clip texture references, and explicit texture paths inside `.slspriteclip` assets. Each edge records its source asset ID, reference text/reason, resolved target ID when available, and broken state.

This is intentionally groundwork for 0.9 packaging and MCP rather than a cooked-artifact database. The graph is queryable in both directions and the editor Inspector surfaces dependencies and reverse dependents.

## External sprite clip assets

`.slspriteclip` v1 externalizes a directional/frame animation without forcing Scene v11 to change immediately:

```text
vespera_sprite_clip 1
name "Watcher Walk"
directions 8
frames 2
fps 4.0
loop 1
texture "Watcher D0 F0" "textures/watcher_d0_f0.bmp"
...
end_sprite_clip
```

The logical resource name maps to the currently registered scene-world texture, while the project-relative path creates an explicit stable asset dependency. The editor can apply/replace the asset into the current scene. The reference game also applies the external Watcher clip after scene load, proving the runtime path while old embedded clips remain compatible.

## PNG/JPEG on Windows

On Windows, the renderer-independent texture importer now uses Windows Imaging Component for `.png`, `.jpg`, and `.jpeg`, converting the first frame to RGBA8. BMP/TGA remain native decoders. Non-Windows builds continue to report PNG/JPEG decoding as unavailable until the portable Vulkan/Linux-era image decoder is selected.


## 0.7.5 authored descriptors and build closure

`.slspritesheet` assets reference one source texture and declare frame size, directional rows, animation-frame columns, playback rate, margins and spacing. Import validates the complete grid, slices renderer-independent RGBA8 frames, then commits the derived textures to the target world.

`.slaudio` assets reference a raw audio source and carry default volume, looping and simple spatial attenuation settings. The current runtime requires WAV, but gameplay-facing clip metadata is now independent from the eventual source decoder/cooker.

Project format v3 adds repeatable `build_include` roots. Project v8 makes `startup_ui` a first-class stable build root, and Project v9 adds the optional stable `lua_entry` root. `build_project_asset_manifest()` walks the startup scene, optional startup UI, optional Lua entry, plus explicit roots through the stable dependency graph and returns a sorted transitive source-asset closure, missing roots, and broken edges. This is the source-side precursor to 0.9 standalone cooking/packaging.


## 0.7.6 stable references and move-aware refresh

`AssetReference` is the common ID-first/path-fallback source reference used by the upgraded project roots and authored descriptor formats. `AssetCatalog::resolve_reference()` prefers the stable `.vmeta` ID, then falls back to the authored project-relative path for legacy/recovery behavior.

Sprite Sheet v2 and Audio Clip v2 use `source_asset "<id>" "<path>"`; their v1 `source "<path>"` records remain supported. Project v4+ uses `startup_scene_asset` and `build_include_asset` with the same semantics; Project v5 additionally persists `input_bind` records.

Catalog refresh now compares previous and current records by stable ID. A source whose ID remains the same but whose path changes is reported as **Moved**, not removed+added. Dependency edges track whether they resolved by ID and whether the fallback path is stale. A stale path is diagnostic only when the stable ID resolves successfully.

The build manifest uses the same stable root resolution and reports stale project-root fallback paths separately from missing roots/broken dependencies. This is an important prerequisite for safe asset moves, future drag/drop reorganization, MCP asset operations, and standalone cooking.


## 0.7.7 authoring operations and managed asset handles

0.7.7 adds a guarded source-asset move/rename path. The editor moves an asset and then its `.vmeta` sidecar, preserving the stable asset ID; if the sidecar move fails it attempts rollback and reports rollback failure explicitly. Destination paths must remain inside the project Assets root and cannot change the source extension. File collisions are refused rather than overwritten.

`repair_stable_asset_fallbacks()` refreshes readable fallback paths for Project v4+ roots plus Sprite Sheet v2 and Audio Clip v2 `source_asset` records. Stable IDs remain authoritative and unchanged.

The managed host now receives the live project `AssetCatalog` and exposes ABI v6 asset resolution callbacks. `Vespera.NET` wraps them with `AssetReference` and `Assets`, allowing gameplay code to use stable IDs for scene loads, prefab instantiation, and audio playback. This is the intended direction for later scene/component asset-reference migration: one project asset identity system shared by editor, runtime, C#, future Lua, build cooking, and MCP.


## `.vmeta` v3 texture import intent

0.7.9 keeps the stable-ID/fingerprint fields from v2 and optionally adds renderer-independent texture source intent:

```text
vespera_meta 3
id "..."
importer "vespera.texture.png"
source_size 12345
source_hash "..."
source_mtime 1234567890
texture_usage "ui"
texture_filter "linear"
texture_wrap_u "clamp"
texture_wrap_v "clamp"
texture_color_space "srgb"
texture_alpha "blend"
texture_mipmaps "off"
texture_max_size 2048
end_meta
```

The settings describe import intent, not D3D12 objects: `usage` is world/sprite/ui/data; filter is nearest/linear; wrap is repeat/clamp; color space is sRGB/linear; alpha is auto/opaque/cutout/blend; mipmaps are auto/on/off; max size `0` means source size. Existing v1/v2 sidecars remain readable and use compatibility defaults until settings are authored. Current rendering is intentionally not rewritten to consume every setting yet; the point is to establish durable project metadata before runtime UI/material resource work.

## Font source assets

`.ttf` and `.otf` assets now have a real source-validation layer rather than only a pending catalog label. Vespera validates the TrueType/OpenType SFNT container and exposes it in the Asset Inspector. Glyph rasterization, shaping, atlas strategy and runtime Text components remain runtime-UI work so 0.7.x does not prematurely lock the engine to a font implementation.

## Runtime invalidation and build-index groundwork

`RuntimeAssetMonitor` can poll the same `AssetCatalog` and report Added/ContentChanged/Moved/Removed stable-ID changes without mutating Scene, D3D12 or SDL audio resources from the poll. Resource consumers can later schedule safe replacement at frame boundaries.

`write_build_asset_index()` serializes the deterministic build-manifest closure as stable ID + path + kind entries. This is source-side groundwork for 0.9 cooking/export, not a standalone cooker yet.

## 0.7.8 stable Scene prefab references

Scene v12 extends ID-first/path-fallback identity into prefab instance source links via `prefab_source_asset`. Scene v8-v11 path-only `prefab_source` records remain load-compatible, and the editor upgrades resolvable legacy links in memory.

The dependency graph now records Scene prefab links as stable edges. This means moving a prefab with its `.vmeta` sidecar can remain safe even while scenes reference it; stale readable fallbacks are repaired through the same authoring repair workflow used by Project roots and v2 descriptors.

The reference project's dynamically loaded alternate scene is now an explicit stable build root, closing the known source-manifest gap before standalone cooking begins.

## 0.9.1 UI Documents

`.slui` v1 is now a first-class `AssetKind::UiDocument` (`vespera.ui`). The dependency scanner records stable `image_asset` and `font_asset` references, so UI assets participate in reverse-dependency diagnostics, deterministic build closure, guarded asset moves and stable fallback-path repair. The reference `ui/reference_hud.slui` is an explicit build root and currently pulls its UI image through stable identity.
