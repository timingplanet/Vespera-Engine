# Current editor workflow — 1.0.0

- The native window title follows the loaded project name (`<Project> - Vespera Editor <version>`) instead of falling back to `Untitled`.
- Use **Build -> Build Game...** or `Ctrl+Shift+B` for Debug / Development / Release standalone builds. **Build & Run** launches the packaged game without requiring a terminal.
- Build Game saves current project state, performs project/build-closure preflight, runs asynchronously, exposes recent build output, and can open the package folder.
- PowerShell build/export scripts remain useful for source-checkout automation/QA but are not the intended normal game-developer workflow. Engine release candidates are validated with `tools/run-rc-gate.ps1`, which reuses one Release build for the public shipping gate plus adversarial editor/runtime QA.

# Project Hub / New Project — 0.9.8

Normal startup now uses `./vespera.ps1` (PowerShell: `.\vespera.ps1`) to launch the RmlUi-powered Project Hub. The hub lists recent projects, opens an existing `.vesperaproject`, and creates **2D / UI Foundation (Experimental)**, **3D / 2.5D Game**, or **Empty Project** starters from `templates/`. After selection/creation it launches the existing Vespera Editor with that project path.

The reference game remains an engine QA project and is intentionally not cloned into new projects. The 2D/UI starter is explicitly experimental, but it is now meaningfully distinct: it starts as a playable screen-space RmlUi canvas driven by project C# and project input bindings. Dedicated orthographic world/editor tooling is still not advertised for 1.0. Fresh templates use the shared `vespera_player` by default rather than pretending each needs a custom native target.

## 0.8.10 closeout additions

- Console: **Copy Visible**, **Copy All**, and right-click **Copy Entry / Copy Message Only**.
- Tools > Extensions shows Editor Extension API v1 registrations.
- Automation/MCP expands to 28 developer-preview tools and can discover registered capabilities.
- Runtime UI layout/input groundwork is engine-native; see `RUNTIME_UI.md`.

## 0.8.9 Automation / MCP developer preview

The editor can now optionally host a localhost-only automation endpoint. Start it with `run.ps1 -Automation` or Tools > Automation / MCP. Requests are dispatched on the editor/main thread and selected mutations use the same undo/history path as human editor actions. The companion `tools/vespera_mcp_server.py` translates MCP stdio calls into that internal VAP v1 boundary. See `docs/AUTOMATION_MCP.md`.

# Vespera Editor

## 0.8.8 Material authoring

Materials are first-class Project assets (`.slmat`). Use **Assets -> Create Material Asset**, select the asset to edit its built-in shader/base texture/color/emission/cutoff, then drag it onto a Mesh Renderer Material field or directly onto a primitive in the Scene viewport. The Mesh Renderer keeps its older direct Texture + Instance Tint values as fallback/per-instance data.

Project grid/list views expose Material cards and filtering. Selecting a Material base texture uses the same stable AssetCatalog ID-first/fallback reference semantics as other authored descriptors.


## 0.8.7 interactive Game view runtime

The 0.8.6 Play/Stop isolation boundary is now connected to real gameplay services. **Play** clones the edit scene into an editor-owned play scene, creates a runtime player proxy, starts the project C# assembly when available, initializes SDL3 audio, and runs a shared project-authored InputMap. Open **Game** and click the viewport to capture input; WASD + mouse drive the authored camera/player through sector movement and scene colliders, Shift sprints, and the reference project's named actions remain visible to C#. **Escape** releases Game input without stopping Play.

Managed `Start`/`Update`, trigger Enter/Stay/Exit, deferred entity work, C# scene-load requests and audio services run against the isolated play scene. A requested scene is loaded only after the managed update returns, then managed attachments are recreated against the new play scene. **Pause** freezes play advancement and pauses active editor-owned voices. **Step** advances exactly one 1/60-second runtime tick while remaining paused. **Stop** tears down managed/audio/runtime state and restores the pre-Play edit scene plus selection and undo/redo history.

Project v5 adds serialized `input_bind` records. Project Settings exposes these bindings, and the reference project now authors its keyboard/gamepad action map there instead of rebuilding a separate map in C++. Standalone runtime and editor Play therefore resolve the same semantic action names. 0.8.7 editor Play currently captures keyboard + relative mouse directly; the serialized gamepad bindings are already used by standalone runtime and are ready for editor controller feed in a later 0.8 checkpoint.

The **Build C#** helper now treats the editor's managed directory as a live runtime mirror. The process-lifetime `Vespera.NET.dll` bridge is never replaced under a running editor; collectible game-code/dependency files are copied atomically and the game DLL is committed last so automatic reload cannot observe a half-updated set. If the bridge itself changes, the build succeeds but the editor explicitly requires restart.

## 0.8.6 Scene/Game Play Mode and asset drag/drop

The default center workspace includes Scene, Game and Sector tabs. 0.8.6 established the non-destructive Play/Stop/Pause/Step boundary and isolated play-scene copy that 0.8.7 now runs interactively.

Project cards/list rows are drag sources. Drop a Prefab into Scene to instantiate it in front of the editor camera. Drop a Texture onto a Sprite Renderer or Mesh Renderer texture field when that texture is already registered in the open scene resources. Prefab instances also expose Apply/Revert/Unpack from Hierarchy context menus and Select Source in Inspector.

The command audit record carries before/after editor state IDs and optional entity/asset targets. It remains an internal transaction surface rather than a public MCP endpoint, but new editor work should converge on this typed boundary instead of bypassing it.

## 0.8.5 hierarchy, world transforms and asset previews

Hierarchy drag/drop creates real parent/child relationships. Child Transform values are local to the parent; Scene gizmos operate in world space and preserve world position when reparenting. Drop on `Entities` to unparent, or Ctrl-drop on another entity to reorder without parenting. The Project grid also shows decoded texture previews, and the editor window uses the approved Vespera mark.

## 0.8.4 group transforms and Hierarchy ordering

Hierarchy supports single selection, Ctrl-click toggle selection, Shift-click ranges, group Duplicate/Delete, and drag/drop ordering. Scene multi-selection shares one transform gizmo with Center/Pivot placement, Local/Global orientation, snap values, Alt snap bypass, Escape cancellation and undo/redo transaction behavior.

## 0.9.0 shipping workflow additions

- Console output is now rendered as a read-only selectable text surface. Drag over any substring and press **Ctrl+C**; bulk **Copy Visible / Copy All** remains available.
- **File → Export Development Package** packages the current project when the Debug runtime is already built.
- **File → Export Release Package** packages an already-built Release runtime. Use `export.ps1 -Configuration Release` when Vespera needs to build Release first.
- Export uses the same deterministic `BuildAssetManifest` closure used by runtime/build validation; it does not copy the whole Assets directory.

## 0.9.1 build/package + UI asset notes

Project Settings now exposes Project v6 **Build & Package** metadata: company, product version, package name and framework-dependent/portable managed deployment. UI Documents (`.slui`) appear as a normal Project asset type and the Asset Inspector reports node counts/layout status. Visual Canvas authoring is not yet implemented.

The Console remains a read-only selectable text surface with arbitrary drag selection + Ctrl+C; the redundant instructional hover tooltip from 0.9.0 was removed.

## 0.9.2 Game-view runtime UI

While Play Mode is running, the Game viewport now consumes the same engine-native `.slui` renderer as standalone runtime. The reference project's HUD is loaded before the embedded managed host starts, so C# UI changes are visible in the Game view without touching the edit scene. Visual Canvas authoring is still future 0.9.x work; current `.slui` editing remains asset/Inspector/MCP-oriented.

## 0.9.3 UI Authoring

Double-click a `.slui` asset in Project or use **Open UI Authoring** in its Inspector/context menu. The dockable authoring window is intentionally a practical first pass rather than a separate editor shell: UI hierarchy on the left, visual Canvas preview in the center, semantic properties on the right.

The preview supports 1280x720 / 1920x1080, direct selection, drag move, lower-right resize handles, Top Left / Center / Stretch anchor presets, anchors/offsets/margin/padding, container layout, clipping and v2 widget properties. Children managed by a List/Grid/layout container are selected visually but are not free-dragged because the parent owns their resolved placement.

Texture assets can be dragged onto Image fields and Font assets onto text-capable UI fields. Save uses the guarded `.slui` writer and refreshes AssetCatalog stable dependencies.

## 0.9.4 Build Settings

Project Settings now owns the public-facing standalone build defaults instead of exposing Vespera's internal target/output names:

- Company / Product Version / Package Name
- Managed Deployment
- Executable Name
- Build Output Directory
- stable Game Icon asset
- Development Diagnostics / Symbols

A Texture selected in the Asset Inspector can be assigned with **Use as Project Game Icon**. The icon becomes a stable build root and participates in safe-delete/fallback-repair rules.

File menu export actions now include Debug, Development, Export & Launch Development, and Release. Development expects a `RelWithDebInfo` runtime; when that configuration has not yet been built, use `export.ps1`, which performs the requested native build before packaging.

## 0.9.6 Modern UI authoring

UI Authoring now edits `.slui` v3 presentation data as well as layout/widget state. **Surface Style** exposes opacity, rounded corners, border width/color, shadow offset/softness/color, image fit and 9-slice insets. Text-capable nodes expose font family, size, weight, italic, word wrap, line spacing, horizontal/vertical alignment and text shadow.

Textures may skin normal surfaces such as Panels and Buttons in addition to Image nodes; Font assets still resolve through stable AssetReferences. The authoring preview approximates radius/border/shadow/text styling while the engine-native runtime renderer remains authoritative for font rasterization, image fit and 9-slice output.

For automation-heavy UI work, the MCP bridge adds `vespera_apply_ui_batch` so substantial trees can be created/edited through one tool call while each mutation still travels through the validated VAP/editor operations.

## Project Lua entry (0.10.0b)

Project Text v9 adds one optional project-level Lua entry script. Project Settings exposes the readable path and stable ID, can adopt the currently selected `.lua` asset, and can clear the entry. The entry is used by both Editor Play and the shared player; C# and Lua may run together. See `LUA_SCRIPTING.md` for the runtime API and current boundary.
