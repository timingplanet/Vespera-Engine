# Vespera Engine architecture notes

## Core principles

1. **The showcase game is a first-class consumer of the public engine API.** It must not reach into engine internals to get normal game work done.
2. **Modern renderer, constrained world model.** Sector authoring is a strength, not a nostalgic implementation constraint.
3. **Sprite-first, not sprite-only.** 3D meshes may exist, but a complete game must be possible without bespoke 3D character models.
4. **Simple things should be obvious; advanced things may be advanced.** The target learning curve is a professional engine such as Unity, not a no-code level maker.
5. **One command model later.** Editor undo/redo, scripting/editor automation, and MCP should eventually sit on the same structured command layer.
6. **Visual scripting is post-1.0.** When added, it should expose the same gameplay API as Lua rather than inventing a second engine.
7. **Vendor graphics features are plugins/options, not engine identity.** Hardware RT is renderer-level; DLSS/FSR are optional upscaler integrations.
8. **No game-specific systems in engine core unless multiple games/examples prove they are generally useful.**

## Renderer strategy

SDL3 owns window creation, events, input, and platform plumbing. Vespera owns graphics.

The renderer is abstracted behind `RenderBackend`. D3D12 is the first real backend. Vulkan is planned only after the renderer interface has survived real world rendering work.

Future renderer capabilities should include:

- raster feature level
- hardware ray tracing support
- HDR support
- async compute support
- mesh-shader support (optional)
- upscaler plugin support

Do not design the runtime around any single vendor feature.

## World strategy (planned)

Vespera's scene system should eventually distinguish:

- scene/entity/component objects
- authored 2.5D sector geometry
- generated render geometry

A sector is a native engine primitive, not a separate 'level maker' file format bolted onto the side.

## AI / MCP strategy (planned)

MCP is not an AI generator inside the renderer. The editor should eventually expose a structured command API. MCP can securely expose selected commands such as:

- inspect scene / selection
- create/delete/duplicate entity
- add/remove component
- get/set serialized property
- import asset
- create material/prefab
- open/save scene
- run/stop game
- read build/runtime diagnostics

Writes should be project-scoped, permissioned, logged, and undoable as transactions.

## 0.0.2 renderer bring-up note

The first visible graphics pipeline compiles a tiny built-in HLSL shader with `D3DCompile`. This is intentionally a bring-up mechanism, not the final shader toolchain. The renderer owns the shader/pipeline boundary so the implementation can migrate to DXC without changing game-facing APIs.

Capability queries are informational in 0.0.2. Detecting hardware ray tracing does not yet enable any RT render path.

The Windows build script resolves CMake from PATH first, then Visual Studio's bundled CMake via `vswhere`, so normal PowerShell is sufficient once the Visual Studio components are installed.

## 0.0.3 world-space bring-up note

0.0.3 deliberately keeps its test geometry inside the renderer while proving the world-space pieces needed by the real world system: perspective projection, view matrices, model transforms, indexed geometry, and depth-tested multiple-object rendering.

This is temporary scaffolding. The renderer must not remain responsible for authoring game scenes. In 0.1.x, the world layer will own sector data and generate renderable geometry; the D3D12 backend should only consume render data produced by engine systems.

The current per-draw tint is a minimal material boundary for bring-up, not the final material asset format. Texture/material resources will be designed after the first sector geometry exists so the API is driven by real world usage rather than speculative abstraction.

## 0.1.0 sector-world ownership note

0.1.0 removes the temporary renderer-authored demo scene. `Application` now owns a game-facing `Scene`; the reference game populates `Scene::world` and controls `Scene::camera`, while `RenderBackend` receives the scene as read-only render input.

Sector meshing is renderer-independent. `build_sector_mesh()` lives in the world layer so D3D12, future Vulkan, editor previews, validation tools, and offline tooling can share the same generated geometry rules. The D3D12 backend is responsible only for uploading and drawing the resulting mesh.

The initial sector representation deliberately supports convex X/Z polygons, independent floor/ceiling heights, material references, and per-edge side metadata. `adjacent_sector` is reserved for portal connectivity. 0.1.0 recognizes a portal-designated edge during meshing by leaving it open, but full portal wall-band generation, sector crossing, and height transition collision belong to the next world iteration.

The first-person controller in the reference game is intentionally game-side code using public engine input/world APIs. This is an early dogfooding rule: reusable input and collision primitives may live in engine core, but a specific FPS controller should not silently become mandatory engine behavior.


## 0.1.1 connected-sector note

0.1.1 makes `adjacent_sector` operational. A portal edge is not simply omitted: the world mesher computes the vertical overlap between neighboring sectors and emits lower/upper wall bands for any non-overlapping height ranges. This keeps the 2.5D sector relationship renderer-independent. The first character controller can cross full-edge portals when vertical clearance and step-up constraints are satisfied, while solid edges still use convex circle projection.

## 0.1.2 texture-material note

0.1.2 adds renderer-independent CPU `TextureData` (RGBA8), material texture references, UV scale, and UV/material-layer data in generated sector meshes. D3D12 consumes those generic inputs by packing current world textures into a sampled `Texture2DArray` with a guaranteed white fallback layer. Texture coordinates are generated in the world mesher: floors/ceilings use world X/Z mapping and walls use edge distance plus world height.

The reference game intentionally creates its small test textures procedurally so this milestone proves the resource boundary without introducing an image-decoder dependency yet. File import, compression, mip generation, arbitrary texture sizes, filtering options, and the dedicated GPU resource abstraction remain later asset/renderer work.



## 0.1.3 scene-serialization note

0.1.3 introduces `Sectorline Scene Text v1` (`.slscene`) as a versioned, human-readable interchange for the current Scene/sector world. The format stores camera state, materials, sector polygons, floor/ceiling heights, per-edge materials, and portal adjacency. Texture payloads are deliberately not embedded: materials reference already-registered texture assets by stable name, keeping scene structure separate from image/GPU resource ownership.

`load_scene_text()` parses and validates the entire document, including references and adjacency indices, before replacing the live scene. This transactional behavior is important for the future editor: opening a malformed asset must not partially corrupt the currently-open scene. `save_scene_text()` writes deterministic text suitable for source control, debugging, editor save operations, and later MCP tooling.

The reference game now loads its actual connected world from `assets/scenes/connected_sectors.slscene`; only the temporary procedural texture payloads remain in C++ until the asset importer/resource database exists.


## 0.1.4 input/action mapping note

0.1.4 separates physical device state from game-facing control names. SDL3 scancodes and gamepad handles remain inside `Application`; game code sees Vespera `Key`, `GamepadButton`, `GamepadAxis`, and named `InputMap` actions. A single action can combine digital and analog bindings with scaling and per-axis deadzones.

The reference first-person controller now dogfoods this API: WASD and the left stick feed the same movement actions, while Left Shift and L3 feed the same sprint action. Right-stick look is action-mapped; high-resolution mouse look remains raw frame delta because mouse motion and normalized stick axes have intentionally different semantics.

This is a foundation, not the final settings UX. Serialized project bindings, user rebinding UI, Lua exposure, and editor Input settings should build on the same action API later rather than teaching game code about SDL. The runtime currently owns one active gamepad; multi-player device assignment should wait for a game that requires it.


## 0.2.1 editor-foundation note

0.2.1 intentionally starts the professional editor earlier than the original roadmap. `Vespera Editor` links the public engine library and manipulates the same `Scene`, `SectorWorld`, `SpriteActor`, and scene-I/O types used by runtime games. It is not a separate level-maker data model.

The first Scene View is top-down X/Z because that directly represents Vespera's 2.5D authoring model and requires no renderer-private access. Dear ImGui uses SDL3's SDL_Renderer backend for tooling UI only; this does **not** replace or abstract the game's D3D12 renderer. A 3D editor viewport should wait for a real offscreen render-target/resource abstraction that can later serve D3D12 and Vulkan consistently.

0.2.1 used direct property mutations as a bootstrap. 0.2.2 adds the first edit-transaction/history layer so direct Scene View drags become single undoable operations and Inspector edits participate in history. The implementation still stores scene snapshots; a later typed command API should become the shared source of truth for UI, undo/redo, automation, and MCP.

## 0.2.2 editor-authoring note

Direct top-down authoring now treats sector vertices as editable geometry while preserving Vespera's convex-sector constraint. Coincident vertices are welded across sectors during movement so shared portal corners do not tear. Saves are validated through the public scene loader before replacing the source file, and one previous-save backup is kept for rollback.


## 0.2.3 authoring-command note

Create/duplicate/delete sprite operations are centralized behind editor command helpers rather than being scattered through Hierarchy, menus, and Inspector code. The current implementation still stores whole-scene snapshots for history, but every UI surface calls the same authoring helpers. This is an intentional bridge toward the later typed command model that will also back automation and MCP transactions.


## 0.2.5 sprite-animation note

Directional frame and animation-time resolution lives in `scene/sprite_animation.*`, not in D3D12. A `SpriteAnimationClip` is scene data and `resolve_sprite_frame()` produces the texture frame for a camera/time. This prevents renderer backends, editor previews, and future tools from independently reimplementing sprite-direction rules.


## 0.2.8 sprite-authoring identity note

Scene Text v4 gives sprite actors stable non-zero `SceneObjectId` values and an explicit enabled state. Older v1-v3 scenes remain loadable; the loader assigns migrated ids in file order and refreshes the scene allocator before committing the scene. New editor-created/duplicated actors always receive fresh ids, and the D3D12 renderer skips disabled actors.

Sprite animation clips remain named scene resources for now, but 0.2.8 makes them directly authorable in the Project/Assets + Inspector workflow: create, duplicate, delete, rename with actor-reference repair, change 1/4/8 direction layouts, resize frame counts, set FPS/looping, and assign each direction/frame texture. The editor still uses scene-snapshot history; the later typed command model will replace that implementation without changing the user-facing authoring concepts.

0.2.8 also adds a renderer/editor-independent `validate_scene()` API. Scene saving refuses validation errors, while editor tooling can surface the same issues without duplicating rules. This is intentionally reusable by future build tools and MCP commands.

## Scripting/API exposure strategy

Vespera's engine core remains C++20. C# is the primary full-game managed programming layer; Lua 5.4 is the lightweight runtime/mod scripting option; visual scripting remains a later layer over the same semantic gameplay API.

The important architectural rule is **one source of truth for gameplay/editor API metadata**. C#, Lua, documentation, MCP operations, Inspector metadata, and future visual-script nodes should be generated or adapted from the same underlying API descriptions wherever practical. Do not independently invent incompatible scripting APIs.

0.5.0 hosts modern .NET from the native engine and exposes the first `Vespera.NET` assembly with C# component Start/Update lifecycle and native Entity Transform access. Inspector-exposed arbitrary managed fields, editor compiler diagnostics, and hot reload remain later 0.5.x steps after basic assembly loading/lifetime rules are proven reliable.

## Editor UI strategy (planned)

Dear ImGui is the tooling foundation, not the intended visual identity. During early development the editor favors functional panels and workflow validation. Once major workflows settle, Vespera should receive a dedicated UI/UX pass: custom styling, spacing, iconography, hierarchy/property presentation, dialogs, status feedback, and coherent interaction patterns. The 1.0 editor should look and feel like Vespera rather than a stock ImGui application.


## 0.3.0 entity/component ownership note

0.3.0 replaces the standalone `SpriteActor` scene-object model with the first general `Entity` model. An Entity owns stable identity, name, enabled state, and a required `TransformComponent`; render behavior is supplied by the optional `SpriteRendererComponent`. This makes a transform-only marker just as valid as a rendered watcher and stops scene identity from being tied to one rendering feature.

This is deliberately a small built-in component foundation, not a prematurely generic ECS. The immediate goal is to establish stable ownership, serialization, validation, editor authoring, and runtime consumption before introducing reflection, arbitrary gameplay components, scripting lifecycles, or prefabs.

Scene Text v5 serializes entities as blocks with required Transform and optional component records. The loader keeps v1-v4 scenes usable by migrating legacy sprite records into Entity + Transform + Sprite Renderer data transactionally. Saving writes v5.

Directional sprite facing now reads Transform Y rotation, while the Sprite Renderer owns presentation/animation properties. D3D12 consumes this renderer-independent scene model; future Vulkan must consume the same Entity/component inputs rather than creating backend-specific scene objects.

The component boundary is also intentional groundwork for the shared API strategy: future C#, Lua, MCP/editor commands, Inspector metadata, docs, and visual scripting should refer to the same Entity/Transform/component concepts rather than parallel incompatible object models.


## 0.3.3 component identity and collider note

0.3.3 gives every built-in component a stable key (`sectorline.transform`, `sectorline.sprite_renderer`, `sectorline.cylinder_collider`) and routes generic built-in add/remove/query operations through `BuiltinComponentType`. This is not the final reflection system, but it intentionally establishes identifiers that can remain common across the native Inspector, future C# and Lua bindings, documentation generation, MCP commands, and post-1.0 visual scripting.

`CylinderColliderComponent` is the first gameplay-oriented optional component beyond rendering. Its authored shape is a vertical cylinder with radius, height, local center, and trigger state. The initial public collision helpers resolve X/Z circles against enabled non-trigger colliders; the reference FPS controller consumes those helpers instead of embedding collider knowledge in the renderer. Full physics, vertical filtering, rotated/general shapes, rigid bodies, and trigger-event dispatch are later systems.

The editor's **Add Component...** menu now reads built-in component metadata instead of being a one-off Sprite Renderer button. That keeps the UI moving toward a metadata-driven Inspector before arbitrary/reflected component registration exists.

`run-game.ps1` now synchronizes source project assets into the built reference-game directory immediately before launch. CMake's post-build asset copy remains useful for initial builds, but authoring no longer requires rebuilding or manually copying a saved scene just to test it.

## 0.4.0 gameplay scene API note

0.4.0 adds semantic Entity identity (`tag`, `layer`) and public query/clone helpers without moving gameplay logic into the renderer. These APIs are deliberately renderer-independent and use stable scene ids rather than editor indexes as their durable identity.

Trigger enter/exit tracking is also a scene/runtime primitive rather than a reference-game-specific callback system. `TriggerTracker` consumes public trigger overlap queries and emits stable-id events; future C# and Lua lifecycle layers can translate the same events into script callbacks.

The new 2.5D raycast is similarly explicit about its constrained world model: it is a constant-height X/Z gameplay query against sector wall segments and Cylinder Colliders, with portal edges transparent inside their vertical opening. A future mesh/physics raycast should coexist as a separate capability rather than silently changing this deterministic query.

Built-in component metadata now includes property keys/types, and `component_access` provides generic get/set by semantic keys. Native code remains free to access typed C++ structs directly. Importantly, generic tooling does **not** depend on native member offsets, leaving room for managed bindings and ABI changes.

CMake-generated engine version metadata removes another class of duplicated string constants across the runtime/editor. The same principle should be used for future generated API metadata: one source of truth, multiple surfaces.


## 0.4.5 prefab and asset-discovery note

`EntityPrefab` is deliberately renderer-independent and reuses the same Entity/Transform/built-in component data already consumed by runtime and editor code. Prefab files do not carry stable scene ids; instantiation allocates a fresh `SceneObjectId`, preserving the rule that durable scene identity belongs to the target Scene rather than an asset template.

Scene Text v8 optionally records a project-relative `prefab_source` on an Entity. This is authoring metadata, not a hidden runtime live-link. The editor explicitly performs Apply/Revert/Unpack operations, which makes mutation behavior predictable now and leaves room for a later property-override model without having to undo implicit synchronization semantics.

`AssetCatalog` is the first shared native asset-discovery layer. It recursively catalogs known Vespera-authored asset types from an assets root and presents normalized project-relative paths. It is intentionally smaller than the eventual resource database: imported texture/audio/model metadata, stable GUIDs, dependency tracking, reimport rules, and hot reload remain future work. The important architectural step is that native asset discovery now belongs to an engine service rather than individual editor panels.

## 0.4.9 renderer/editor integration note

0.4.9 introduces a staged renderer frame API so tools can share the actual game renderer without moving Scene/world logic into a graphics backend. `begin_frame()` owns swapchain/depth setup, `render_scene()` can consume a camera override and pixel sub-region, and `end_frame()` presents. Normal games still call the convenience `render()` path.

The first docked 3D editor view renders the Scene into a sub-region of the active D3D12 backbuffer. Starting in 0.4.13, that rendered frame is copied into a shader-readable editor preview texture and displayed by ImGui. This keeps the renderer/game integration small while avoiding reliance on transparent docking backgrounds. A general renderer-owned offscreen render-target abstraction remains planned for thumbnails, multiple simultaneous 3D views, post-processing, Vulkan, and other tooling.

Dear ImGui's Windows renderer backend now records into Vespera's open D3D12 command list after the Scene draw and before present. `D3D12NativeAccess` is an explicitly backend-specific tooling bridge; it exposes only the D3D12 objects needed for this integration. It is not part of the renderer-independent Scene/gameplay API, and a Vulkan tooling bridge can be implemented separately later.

The editor preview camera is tooling state, not scene state. Navigating the 3D View must never silently rewrite the authored game camera. This separation will also matter for later multiple views, play-in-editor, and editor automation.


## 0.5.1 managed authoring boundary note

0.5.1 corrects the initial proof-of-concept ownership shape before more scripting APIs accumulate: managed scripts are an ordered `Entity::managed_scripts` attachment list rather than a native built-in component kind. Scene v11 / prefab v4 persist multiple attachments and semantic exposed-field overrides. `Vespera.ScriptTool` reflects `[Expose]` metadata from the built game assembly for editor authoring, while runtime field application remains inside `Vespera.NET` and happens before `Start()`. Missing types/fields remain serialized so compiler failures do not silently destroy authoring data.

The native ABI now carries both version and structure size. Script instances remain managed-owned and are addressed by host-lifetime handles while native engine state remains identified by stable `SceneObjectId` values.

## 0.5.0 managed runtime boundary note

0.5.0 adds a Windows-first `ManagedScriptHost` without turning CLR objects into engine-owned scene data. The Scene stores a small `ManagedScriptComponent` containing the full managed type name and enabled state. At runtime the host initializes .NET through `hostfxr`, loads `Vespera.NET`, loads the project game assembly, and creates one managed `Component` instance for each enabled authored C# Script component.

The native/managed boundary is a versioned C ABI table rather than exported C++ classes. The initial callbacks cover logging, Entity existence, Transform get/set, and Entity enabled get/set. This prevents native struct layout, standard-library types, exceptions, or ownership rules from becoming managed ABI accidentally.

`Vespera.Managed.EntryPoint.Dispatch` is deliberately one unmanaged-callable dispatcher for the first host. The reference game assembly is loaded into the same `AssemblyLoadContext` as `Vespera.NET`, preserving `Vespera.Component` type identity. Hot reload should later introduce a deliberately collectible game-code context; it must not try to unload CoreCLR itself.

The reference `ManagedSpinner` is a dogfood test of the actual architecture rather than a scripting-only demo: C# changes the same native Transform read by directional sprite rendering. As more APIs are bound, they should wrap the existing semantic Entity/query/input/trigger APIs instead of creating managed-only gameplay systems.


## 0.5.2 managed workflow/lifetime boundary note

Managed build tooling now stages game-code output and only replaces the canonical last-good managed artifacts after both compilation and reflection metadata generation succeed. Editor-side Build C# consumes the same helper and reports structured diagnostics rather than creating a separate build path.

`Vespera.NET` remains process-lifetime bridge/API code, while user game scripts now load from streams into a collectible `AssemblyLoadContext` that reuses the default `Vespera.NET` type identity. This deliberately separates **CoreCLR lifetime**, **Vespera.NET bridge lifetime**, and **game-script assembly lifetime**. `ManagedScriptHost::reload()` is the native recreate primitive; automatic reload orchestration is intentionally deferred until the lifetime boundary is validated on Windows.

Managed script exceptions are contained at the dispatcher. A faulted script instance is quarantined after one detailed error (type, entity id, lifecycle phase, exception type/stack) instead of throwing through unmanaged code or logging the same failure every frame.

## 0.5.4 managed reload boundary note

CoreCLR and `Vespera.NET` remain process-lifetime once the native host is initialized. Only project game code lives in a collectible `AssemblyLoadContext`. Manual reload replaces that context in place, calls `OnDestroy()` on old script instances, recreates scripts from native Scene attachment records, reapplies semantic serialized fields, then calls `Start()`. The native Scene remains authoritative throughout; reload does not create a second managed scene model.

## 0.5.5 managed replacement transaction

The managed reload boundary is now explicitly two-phase. `Vespera.NET` can prepare a collectible candidate game assembly without changing the active game-code context. Native `ManagedScriptHost` validates every enabled scene script attachment against that candidate, then commits only after the full attachment preflight succeeds. A candidate that loads at the CLR level but removes/invalidates an authored component is discarded before the active instances receive `OnDestroy()`.

This keeps the ownership rule intact: native Scene data decides which script attachments are required, while managed reflection decides whether the candidate assembly can satisfy them. The protocol change stays behind the single managed dispatcher and does not expand or version-bump the public C ABI table.

`Vespera.ScriptTool` is also part of the managed authoring boundary rather than a passive file lister. It rejects unsupported or ambiguous `[Expose]` contracts before staged managed output becomes last-good output. This prevents editor/runtime behavior from depending on undefined reflection cases such as readonly exposed fields or colliding legacy serialized names.


## 0.6.0 gameplay bridge

The managed ABI advances to v2. `ManagedScriptHost` supplies one stable bridge context containing pointers to the authoritative native `Scene` and `InputSystem`; C# wrappers call semantic functions rather than observing C++ layout. The new bridge adapts action-map input, Entity name/tag/layer and lifecycle operations, stable component keys, prefab instantiation, and `raycast_scene_2d()`. Trigger overlap detection remains native and only stable Entity ids cross into managed `OnTriggerEnter` / `OnTriggerExit`. `SaveData` and `Time` live in process-lifetime `Vespera.NET`, while game script objects remain in the collectible game-code context.

## 0.6.1 runtime services and deferred scene transitions

`GameContext` now exposes three engine-owned runtime services/data surfaces: `Scene`, `InputSystem`, and `AudioSystem`. Audio is deliberately independent of the renderer and Scene object model. The SDL3 playback implementation is an engine runtime service so future C++, C#, Lua, editor play mode, and standalone builds can share it without embedding audio ownership inside D3D12 or sector-world code.

The managed ABI v3 adds callbacks for audio playback, scene-load requests, runtime managed-script attachment mutation, and generic semantic built-in properties. The bridge still passes POD values/UTF-8 strings/stable ids; no STL, `Entity*`, CLR objects, or native component addresses cross the language boundary.

Managed `Scene.Load()` is deferred. C# records a requested path in the bridge context and `ManagedScriptHost::take_scene_load_request()` transfers ownership of that request to the native game/runtime after managed Update returns. The reference game demonstrates the orchestration sequence: stop managed scene instances -> transactional native scene load -> recreate runtime player proxy/state -> initialize scripts against the new Scene -> Start. A later generalized runtime scene manager can move this orchestration out of the reference game without changing the C# request semantics.

Runtime managed-script synchronization uses `(SceneObjectId, script slot, class name)` as temporary live attachment identity. The authored Scene remains authoritative. Adding/removing script records from runtime entities causes managed instances to be created/destroyed at frame boundaries, preserving the same one-scene-model rule used throughout Vespera.


## 0.6.2 audio/physics gameplay depth

`AudioSystem` remains engine-owned runtime state outside `Scene` and the renderer. Voice handles identify SDL-backed playback instances without leaking SDL handles into Scene or managed code. The Application feeds the Scene camera position as the default listener each frame; gameplay may explicitly override the listener through the public audio API. The initial spatial implementation is deterministic distance attenuation, leaving panning/HRTF/asset import for later layers.

The stronger C# overlap and motion APIs call `scene_circle_overlapping_colliders()` and `resolve_circle_motion_against_scene_colliders()` directly. This preserves one authoritative 2.5D collision implementation for C++, C#, future Lua, tests, and MCP.

## 0.6.3 gameplay lifetime and wrapper boundary

Managed gameplay mutation must not invalidate native Scene storage while the CLR is iterating script instances. C# `Entity.Destroy()` therefore queues a stable `SceneObjectId`; the native host performs managed cleanup after `UpdateAll` returns and destroys the native Entity only afterward. This preserves the existing stable-id ownership rule rather than exposing vector/pointer lifetime to C#.

Collection queries use stable ids and a count/fill C ABI. Typed managed `CylinderCollider` and `PointLight` objects are convenience wrappers over the same semantic component/property layer used by editor/tooling code. Trigger stay events likewise come from the native `TriggerTracker`. These are deliberate examples of the one-source-of-truth rule that future Lua/MCP bindings should follow.

Managed audio handles are wrappers around engine-owned SDL3 voices. `Vespera.NET` may track wrapper ownership/follow targets for convenience, but the native `AudioSystem` remains authoritative for playback state.


## 0.7.0 project and asset identity boundary

`VesperaProject` is now the shared native definition of a game workspace. Editor/runtime code should resolve project-relative asset/startup/managed paths through this layer instead of accumulating more sample-specific path heuristics.

`AssetCatalog` is the source-asset identity/discovery layer. Stable ids live in adjacent `.vmeta` sidecars and are intentionally independent of renderer objects and Scene storage. The 0.7.0 catalog is still a source catalog, not a cooked artifact database; later import caches/dependency graphs should key off the same ids rather than replacing them.

Texture import remains renderer-independent: importers produce CPU-side `TextureData` and the active render backend decides how that payload becomes GPU resources. The new BMP importer therefore lives under `engine/assets`, not under D3D12.

## 0.7.1 project/editor boundary

Project-owned runtime presentation settings now live in `.vesperaproject` v2. The reference executable reads them before entering `Application::run()`. This is intentionally project configuration, not Scene state: changing a startup window size/title should not dirty or serialize a gameplay scene.

Asset selection is also kept separate from Scene storage. The editor stores a selected stable asset ID and resolves it through `AssetCatalog`; it does not smuggle source-asset records into `Scene` or renderer-native resources.

`.vmeta` v2 adds a modification stamp only as a cache optimization. Stable identity remains the persisted ID, and content identity remains the source hash. A timestamp mismatch causes a rehash; an explicit reimport can force hashing. Future cooked-artifact caches and dependency graphs should continue to key on asset IDs/content hashes rather than path/mtime alone.

The Project browser's automatic polling is source-catalog refresh, not a substitute for a future dependency-aware runtime hot-reload system. GPU/audio resources must still be rebuilt through their owning subsystem when true live asset replacement arrives.


### `.vmeta` v3 import intent

0.7.9 keeps stable ID/content-fingerprint semantics unchanged and adds optional renderer-independent texture import settings to `.vmeta` v3. These settings must remain source/import semantics rather than backend objects: do not serialize D3D12 descriptors, GPU handles, or renderer-native sampler state into project metadata. v1/v2 remain readable.


## 0.9.9b game-facing UI boundary

`UiSurface` is the first engine-owned semantic boundary shared by the legacy `.slui` runtime and RmlUi. It intentionally stops at gameplay-facing element operations. Layout, serialization, authoring and renderer preparation remain backend/document responsibilities because `.slui` RectTransform layout and RmlUi DOM/RCSS layout are not interchangeable abstractions.

`LegacyUiSurface` resolves the same authored names used by the existing managed `UI.Find(name)` path and translates text/value/visibility/disabled/focus/click behavior onto `UiDocument` + `UiRuntimeState`. `RmlUiSurface` implements the interface directly while retaining its private RmlUi implementation. This seam is intended to become the managed/Lua/MCP-facing UI service after it is validated independently; 0.9.9b does not change the managed ABI.

## 0.9.9c managed UI service boundary

The managed host now depends on `UiSurface`, not on `.slui` document/runtime types. Script-facing numeric UI IDs are opaque handles maintained by `UiHandleTable`; the handle maps to a backend-neutral authored element key and revalidates existence on every operation. This preserves the public C# shape while allowing either `LegacyUiSurface` or `RmlUiSurface` to sit behind it.

Managed ABI v11 contains 86 ordered fields. ABI v10 introduced four UI callbacks for string value get/set and generic property/class mutation; v11 appends active-camera get/set callbacks for scripted cameras and cinematics. All prior callback order is preserved. The source validator compares the ordered field names across native and C# mirrors. Layout, rendering and authoring remain intentionally outside this service boundary.

## 0.9.9b1 RmlUi sizing hotfix

Windows visual feedback showed that the remaining oversized-control feel was primarily authored RCSS sizing rather than a renderer or `UiSurface` problem. The Project Hub now uses a tighter content/panel/input width budget, and the guild showcase uses shrink-to-fit inline-block layout for primary/secondary action buttons while retaining full-width selection/list rows. RmlUi documents `min-width` with an initial value of `0px`; explicit `min-width: 0px` in the Hub is therefore descriptive, not the claimed fix.



## 0.9.9f shared player / explicit startup UI

`runtime/player/vespera_player` is the first engine-owned generic standalone host. It deliberately treats a user project as data rather than requiring that project to become a CMake target inside the Vespera source tree. Export packages the same native host for ordinary projects and renames it to the authored executable name. Projects can still opt into a specialized native `game_target`.

The player resolves the packaged project beside itself, loads project window/input settings, refreshes the stable asset catalog without writing metadata, resolves the startup scene by stable ID, hydrates material caches, resolves the Project v8 `startup_ui` stable reference (with legacy build-root fallback for older projects), starts the configured managed assembly through `ManagedScriptHost` when present, and starts the optional Project v10 `lua_entry` through `LuaScriptHost`. The C# host receives the active `UiSurface`, so a generic-player RML document can satisfy the same public managed UI calls without exposing RmlUi types.

`engine/runtime/player_project` keeps project discovery and startup-RML selection independent from SDL/D3D12 so those policies have cheap behavioral coverage. This is a runtime-selection seam, not a new gameplay framework. Starter gameplay/controller content remains project-owned and is the next template milestone.


## Lua runtime scripting

`LuaScriptHost` is a project-level lightweight runtime/mod host. Project v10 can reference one stable Lua entry script with optional `Start`, `Update(dt)`, and `Stop` functions. Editor Play, the shared player, and the reference runtime feed it the same Scene/Input/Audio/AssetCatalog/`UiSurface` services used by the C# bridge. Generic component access reuses the existing component/property metadata rather than growing a second binding model.

The Lua VM intentionally opens a restricted standard-library set and does not expose standard filesystem/process/debug/package libraries. Per-entity Lua components and a separate Lua event framework are outside the 0.10.0b boundary.
