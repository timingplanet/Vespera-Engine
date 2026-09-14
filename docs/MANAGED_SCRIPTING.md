# Vespera managed scripting

## Current managed scripting status — ABI v11

Managed ABI v10 appended four UI-service callbacks to the v9 table while preserving all previous field order: string UI value get/set plus generic UI property/class mutation. The 0.15.0 Performance Lab cinematic revision appended two callbacks for the active scene camera. 0.15.2 appends bulk transform writes and a runtime performance snapshot, so the current native and C# tables contain **88 fields in identical order at ABI v12**, guarded by `tools/validate_source.py`.

The more important architectural change is that managed UI callbacks no longer target `UiDocument` / `UiRuntimeState` directly. `ManagedScriptHost` receives an engine-owned `UiSurface`, and `UiHandleTable` maps public numeric `UiElement.Id` handles onto backend-neutral element keys. Existing `.slui` runtime behavior is retained through `LegacyUiSurface`, while RmlUi can now back the same C# element API. `UiElement.ValueText`, `SetProperty`, and `SetClass` are added for RML-friendly form/style control.

### Scripted camera

C# can read or author the active scene camera through `Vespera.Camera.State`, `Camera.Position`, `Camera.Yaw`, `Camera.Pitch`, `Camera.VerticalFovDegrees`, or `Camera.SetPose(...)`. This is intended for cinematics, camera rigs and scripted benchmark fly-throughs. Editor Play detects a managed camera write during the frame and does not immediately overwrite it with the reference first-person controller.

## 0.9.6 managed scripting status — ABI v9

Managed ABI v9 appends one structured runtime-event callback while preserving all prior service-table order. The native and C# tables now contain **80 fields in identical order**. Runtime event records carry a monotonically increasing sequence, runtime frame, managed assembly generation, Entity id, component type and callback name. The host records lifecycle/trigger boundaries such as `Start`, `OnEnable`, `OnDisable`, `OnDestroy` and trigger enter/stay/exit; it intentionally does not trace every `Update()` call.

The event buffer is bounded and intentionally survives managed shutdown/reinitialize long enough for QA to inspect teardown ordering. Editor Play archives the final event stream when Stop returns to Edit Mode. Standalone runtimes launched with `--automation` expose the same event records through the localhost QA endpoint.

Guarded MCP C# writes and **Build C#** remain allowed while Editor Play is active in 0.9.6. A successful managed build still commits through the existing staged/last-good pipeline; the Play runtime's existing auto-reload watcher then observes the completed game assembly and performs the normal collectible-context replacement. A changed process-lifetime `Vespera.NET.dll` still requires a process restart.

Same-scene requests are now explicit: ordinary `Scene.Load()` to the already-current scene is coalesced instead of creating an accidental reload loop. `Scene.Reload()` requests an intentional same-scene reload through an internal semantic sentinel, and the native runtime blocks pathological repeated reload bursts with a diagnostic.

## 0.9.3 richer runtime UI bridge — ABI v8

Managed ABI v8 appends seven semantic UI callbacks to the existing ABI v7 table while preserving all prior field order. Native `UiDocument` / `UiRuntimeState` remain authoritative; C# never receives native UI pointers or renderer objects.

`UiElement` now provides `Text`, `Enabled`/`Visible`, Button `Interactable`, focus state, Text Input `ReadOnly`, Progress `Value`, consume-on-read `Clicked`, color setters, and stable Image/Font assignment. `UI.FindRequired(name)` is available for cases where a missing authored node should be treated as an error.

The reference dogfood animates the `Runtime Progress` bar from C# and consumes the native Continue-button click queue. This exercises the same managed bridge in editor Play and standalone runtime. Type-specific native callbacks reject semantically incompatible operations.

ABI v8 has 79 native/C# service-table fields in identical order; the source validator checks parity. Changing `Vespera.NET` still requires restarting any process hosting the process-lifetime bridge assembly.

## 0.8.8 managed Material references

`Vespera.NET.MeshRenderer` exposes a stable `AssetReference Material` plus `SetMaterial(projectRelativePath)` and `ClearMaterial()`. These reuse the existing semantic string-property bridge, so managed gameplay ABI remains v6. Setting a Material through managed code rehydrates the live scene's transient Material render cache through the project AssetCatalog.


## 0.8.7 editor Play hosting

The same `ManagedScriptHost` used by standalone gameplay can now be owned by an isolated editor Play session. Pressing Play constructs runtime services around the play-scene copy, binds the project `InputSystem` and `AudioSystem`, initializes the project assembly from the editor-local `managed/` mirror, calls managed `Start`, then advances `Update` and trigger callbacks each play tick. Managed `Scene.Load` remains an end-of-update request; editor Play tears down the old managed attachment set, loads the requested scene into the play copy, recreates its player proxy, and initializes a fresh managed attachment set. Stop shuts this runtime down before restoring the untouched edit scene.

`tools/build-managed-editor.ps1` now treats both editor and standalone managed mirrors as live-process destinations. `Vespera.NET.dll` remains process-lifetime and is never replaced underneath an active host. Reloadable game-code files are copied atomically and the game DLL is committed last, allowing the existing debounce/two-phase managed reload path to work while editor Play is running. A changed bridge DLL requires restarting the editor rather than risking mixed ABI/type identity.

0.6.3 builds on Vespera's completed Windows-first C# scripting foundation without replacing the native C++20 core. It retains the validated automatic collectible reload and two-phase replacement pipeline, expands the native/managed gameplay bridge to ABI v5, and hardens Entity lifetime, collection queries, trigger stay dispatch, managed audio ownership, and typed built-in gameplay wrappers.

## Layering

```text
GameScripts.dll
      | C# gameplay classes
      v
Vespera.NET.dll
      | small versioned native ABI
      v
ManagedScriptHost (C++)
      |
      v
Scene / Entity / Transform / native runtime
```

The engine never exposes C++ object pointers or C++ struct layout as the public managed ABI. `Vespera.NET` calls a small table of C-compatible function pointers supplied by the native host.

## Project files

The reference project demonstrates the intended initial layout:

```text
managed/
  Vespera.NET/
    Vespera.NET.csproj
    Runtime.cs
  Vespera.ScriptTool/
    Vespera.ScriptTool.csproj
    Program.cs

examples/reference_game/managed/
  ReferenceGame.Scripts.csproj
  ManagedSpinner.cs
```

`build.ps1` detects an installed .NET 8+ SDK and builds the reference managed project after the native build. The project reference causes `Vespera.NET.dll` and `ReferenceGame.Scripts.dll` to be emitted into `build/managed/reference_game/`.

`run-game.ps1` copies that managed output next to the reference game under `managed/` before launch. While the game is running, editor **Build C#** refreshes the reloadable game assembly/PDB/dependency files in that runtime mirror. `Vespera.NET.dll` is intentionally not replaced in a live process because the bridge assembly stays in the default load context.

## C# script attachments

A scene Entity may contain multiple ordered managed script blocks:

```text
managed_script "ReferenceGame.Scripts.ManagedSpinner" 1
  managed_field "RadiansPerSecond" "float" "0.65"
end_managed_script
```

The type must exist in the loaded game assembly, derive from `Vespera.Component`, be non-abstract, and be constructible by `Activator.CreateInstance`. The per-script enabled flag controls whether that attachment is instantiated. Entity disabled state does not prevent construction/`Start()`; it suppresses `Update()` while disabled.

`managed_field` records are authored overrides for fields explicitly marked `[Expose]`. The runtime binds the Entity, applies these overrides through managed reflection, then calls `Start()`. One Entity can carry more than one C# script without turning managed types into native `BuiltinComponentType` values.

## Lifecycle

For the first foundation, lifecycle is intentionally small:

```csharp
public abstract class Component
{
    public Entity Entity { get; }
    public virtual void Start() { }
    public virtual void Update(float deltaTime) { }
    public virtual void OnDestroy() { }
}
```

`Start()` runs after all enabled C# components have been created and serialized `[Expose]` overrides have been applied. `Update()` runs once per game update while the owning Entity still exists and is enabled. `OnDestroy()` runs before a committed reload or shutdown discards a live instance.

Script exceptions are caught at the managed boundary and routed through Vespera logging instead of escaping across the unmanaged boundary. A `Start()`/`Update()` failure faults only that script instance so a bad component cannot flood the log every frame.

## Initial Entity API

0.5.0 exposes:

```csharp
Entity.Id
Entity.Exists
Entity.Position
Entity.Rotation
Entity.Scale
Entity.Enabled
```

Transform values map to the same native `TransformComponent` used by the renderer/editor/runtime. This is why the reference `ManagedSpinner` can rotate a directional sprite without a scripting-specific render path.

## Logging

```csharp
Log.Info("hello");
Log.Warning("careful");
Log.Error("bad thing");
```

Messages are UTF-8 and appear through the native log with a `[C#]` prefix.

## Runtime hosting

On Windows, the host locates the installed x64 `hostfxr.dll`, initializes .NET from `Vespera.Managed.runtimeconfig.json`, then obtains `load_assembly_and_get_function_pointer` and binds the single `Vespera.Managed.EntryPoint.Dispatch` method.

One dispatcher remains in the process-lifetime `Vespera.NET` context. Game code is now loaded into a dedicated **collectible `AssemblyLoadContext`** whose resolver deliberately reuses the already-loaded `Vespera.NET` assembly so `Vespera.Component` retains one type identity. Game DLL/PDB bytes are loaded from streams, which avoids holding a permanent file lock on the authored game assembly.

The native host does not unload `hostfxr`/CoreCLR libraries during process lifetime. `ManagedScriptHost::reload()` replaces only the collectible game-code context while keeping CoreCLR and Vespera.NET process-lifetime. In 0.5.5, replacement is explicitly two-phase: prepare a candidate assembly, validate every enabled scene script type against it, then commit. A candidate that loads but no longer contains a required attached component is discarded before the old instances receive `OnDestroy()`. Future reload orchestration should build on this boundary, not attempt to unload CoreCLR itself.

## ABI v1

The first native ABI contains callbacks for:

- log
- Entity existence
- get/set Transform
- get/set Entity Enabled

The API table carries an explicit ABI version and structure size so compatible fields can be appended deliberately. Future expansion should preserve compatibility or bump the ABI deliberately rather than silently depending on C++ layout.

## Intentionally outside the completed 0.5.x foundation

The following are later milestones rather than unfinished blockers for 0.5.x:

- edit-mode script execution
- Scene queries/tags/layers/raycast/Input/audio wrappers
- trigger lifecycle callbacks
- multiple managed assemblies / plugin dependency policy
- standalone-game managed-runtime packaging policy

The 0.5.x priority was reliability of the managed host/lifecycle itself. 0.5.5 closes that foundation; broader gameplay APIs move into 0.6.x, while edit-mode execution and standalone managed-runtime packaging belong to later editor/shipping milestones.


## 0.5.1 managed authoring checkpoint

0.5.1 moves user C# scripts out of `BuiltinComponentType` and stores an ordered list of script attachments on each Entity. Scene v11 / prefab v4 can persist multiple scripts and `managed_field` overrides. `[Expose]` fields currently support bool, int, float, string, and `Vespera.Vector3`; overrides are applied after Entity binding and before `Start()`. Entity-disabled state suppresses `Update()` rather than preventing script construction/`Start()`. The build also emits `Vespera.ScriptMetadata.txt`, which the editor uses to discover script types and exposed fields. Unsupported or temporarily missing fields/types are preserved rather than silently deleted.


## 0.5.2 managed workflow + reliability checkpoint

The editor can now invoke the managed build through **Build C#**. The helper builds into a staging directory, generates reflection metadata, and only swaps the staging output into the canonical managed build after all steps succeed. Compiler failures are written as structured diagnostic records and surfaced in the Vespera Console; the previous good DLL and metadata stay intact.

Reflection metadata v2 adds optional tooltip/range authoring hints. `Vespera.NET` provides `[Tooltip("...")]` and `[Range(min, max)]`; the editor uses range metadata for numeric sliders while serialized values remain the same semantic string records used by scene v11/prefab v4.

Runtime exceptions are contained per script instance. A failure in `Start()` or `Update()` records script type, entity id, phase, exception type, and stack trace, then marks that script instance faulted so a per-frame exception cannot flood logs.

## Manual reload foundation retained from 0.5.3

The reference game now exposes the first deliberate reload proof: keep `run-game.ps1` open, edit project C# code, use **Build C#** in the editor, then press **Space** in the running game. `ManagedScriptHost::reload()` asks the existing `Vespera.NET` dispatcher to replace only the collectible game-code context, recreates enabled script attachments in authored order, reapplies serialized exposed fields, and calls `Start()` again.

Before an old game-code context is released, each live component receives `OnDestroy()`. Exceptions are contained at the dispatch boundary. The host then requests collectible-context unload and performs a bounded GC/finalizer verification pass; a surviving context produces a warning because static events, tasks, threads, delegates, or GCHandles are common reload leaks.

The explicit manual reload remains as a fallback and regression tool. In 0.5.4, successful editor builds additionally trigger automatic debounced reload through the native host. Serialized Inspector fields are reconstructed; ordinary transient managed state still restarts.

### Managed field types

Supported `[Expose]` values carried forward into 0.5.4 are: bool, int, float, string, enum, `Vector2`, `Vector3`, and `Color`. Reflection metadata v4 carries enum choices, range/tooltip hints, and legacy serialized-field aliases. Ranged int/float controls expose both a slider and a visible exact-value entry box.

### Script order

An Entity's managed script list is ordered. The Inspector can Move Up, Move Down, and Duplicate attachments; creation, field application, `Start()`, `Update()`, and cleanup follow that deterministic order.


## 0.5.4 automatic reload and safe replacement

The reference runtime enables `ManagedScriptHostConfig::auto_reload`. The host polls the game assembly signature at a low frequency and requires a changed signature to remain stable through a debounce window before reloading. `tools/build-managed-editor.ps1` copies dependencies/PDB/metadata first and commits `ReferenceGame.Scripts.dll` last with a temporary-file rename, so the watched DLL acts as the completed-build marker rather than appearing while companion files are still being copied.

The manual **Space** reload remains available as a fallback. Automatic reload intentionally reconstructs script instances, reapplies serialized `[Expose]` fields, runs `Start()` again, and does not promise preservation of arbitrary transient managed state.

Replacement loading is transactional at the assembly boundary: Vespera first loads the candidate assembly into a fresh collectible context. Only after that succeeds are existing instances given `OnDestroy()` and the old context released. A truncated or unloadable candidate therefore does not tear down the currently running scripts merely because the watcher noticed it.

## Serialized field rename compatibility

Exposed fields may carry one or more legacy semantic names:

```csharp
[Expose]
[FormerlySerializedAs("SpinSpeed")]
public float RadiansPerSecond = 0.65f;
```

Old scene/prefab records using `SpinSpeed` continue to apply to `RadiansPerSecond`. Reflection metadata v4 carries these aliases to the editor; the Inspector recognizes the old key instead of classifying it as unresolved and offers **Migrate Name** to rewrite that stored override to the current field name through normal undo history. The scene/prefab formats remain v11/v4 because this is semantic interpretation of existing `managed_field` records, not a new record layout.


## 0.5.5 foundation-completion checkpoint

0.5.5 closes the remaining reliability gap in live replacement. `LoadGameAssembly` now prepares a candidate collectible context without touching the active one. Native `ManagedScriptHost` preflights every enabled authored script attachment through `ValidateCandidateScript`; only a fully compatible candidate receives `CommitCandidateAssembly`. A compile that successfully produces a DLL but removes/renames an attached component is therefore rejected while the previous script instances continue running. `DiscardCandidateAssembly` unloads rejected candidates without affecting the active context. These dispatcher commands are internal host protocol changes; the public native ABI table remains v1.

`Vespera.ScriptTool` also validates the authoring contract before last-good build output is replaced. Concrete C# components must be constructible with a parameterless constructor; `[Expose]` fields must use a supported writable type; `[Range]` is valid only on int/float; and current/legacy serialized keys must be unique within one component. These failures are emitted as structured `VESPERA` diagnostics and flow through the existing staged **Build C#** path, preserving the prior working DLL/metadata.

With those checks in place, the first 0.5.x scripting foundation is considered complete pending the normal Windows smoke test. Scene v11, prefab v4, metadata v4, and native ABI v1 remain stable. New engine-facing C# capabilities such as Input, scene queries, spawning, triggers, audio, timers, saves, and prefab gameplay APIs move to 0.6.x.


## 0.6.0 gameplay bridge expansion

The native ABI table advances to v2 and now carries a stable bridge context containing both the authoritative `Scene` and `InputSystem`. New callbacks adapt the existing native action map, Entity queries/mutation, prefab instantiation, and deterministic 2.5D raycast APIs. Managed trigger lifecycle dispatch remains native-driven: the engine computes overlap transitions and sends stable Entity ids to `OnTriggerEnter` / `OnTriggerExit`. `Vespera.NET` also owns `Time`, `GameTimer`, and the initial JSON-backed `SaveData` helper. Scene/prefab serialization formats do not change for this checkpoint.

## 0.6.1 runtime-service and dynamic attachment expansion

The managed service table is ABI v3. New callbacks expose engine-owned audio playback, deferred scene-load requests, managed-script attachment mutation, and semantic built-in component properties. None of these callbacks expose native C++ object addresses or STL/optional layout.

Runtime-created script attachments are synchronized at frame boundaries. The native side tracks `(SceneObjectId, script slot, class name)` attachment identity and asks the managed dispatcher to create/start or destroy instances as the Scene's managed-script lists change. Assembly reload still reconstructs all live attachments transactionally.

`Scene.Load()` is intentionally a request, not an immediate load. The current native game owner must consume `ManagedScriptHost::take_scene_load_request()` after managed update and perform whatever scene/runtime orchestration the host application requires. This keeps CLR stack lifetime and native Scene replacement safely separated.


## 0.6.2 ABI v4 runtime services

ABI v4 appends persistent-audio and collision-query callbacks to the versioned POD/function-pointer table. Managed audio receives only voice ids, UTF-8 paths, scalar settings, and POD vectors; managed collision receives stable Entity ids and POD vectors. No SDL stream pointer, `Scene*`, STL container, component address, or CLR object crosses the ABI.

The managed dispatcher also tracks whether each live script instance is active. `Start()` is followed by `OnEnable()` when the Entity is enabled; enabled-state transitions issue `OnEnable()`/`OnDisable()` before `Update()`, and active instances receive `OnDisable()` before `OnDestroy()` during destruction/reload.

## 0.6.3 ABI v5 gameplay hardening

ABI v5 appends a native Entity collection-query callback plus audio stop-all/active-voice-count services while keeping the existing POD/function-pointer boundary. Scene/prefab/managed-reflection formats do not change.

`Entity.Destroy()` is now a deferred native request. The host waits until `UpdateAll` has returned, calls managed script destruction while the Entity still exists, then removes native storage. Destroy requests made during assembly teardown are committed before replacement script instances are recreated.

Managed `Scene` collection queries return stable Entity ids through a two-call count/fill pattern. `CylinderCollider` and `PointLight` typed wrappers remain adapters over semantic built-in property access rather than new managed component storage.

The trigger dispatcher adds `OnTriggerStay` using the native active-trigger snapshot. Managed spatial AudioSources can register an Entity follow target and are centrally ticked/cleaned up by `Vespera.NET`; releasing a game assembly stops managed-owned sources before unloading its collectible context.



## Managed asset references (ABI v6)

0.7.7 passes the runtime project `AssetCatalog` into the managed host and exposes ID-first asset resolution through `Vespera.NET`:

```csharp
var prefab = new AssetReference("stable-id", "assets/prefabs/example.slprefab");
if (prefab.Exists)
    Prefab.Instantiate(prefab, Vector3.Zero);
```

`Assets.FromPath()` recovers a stable ID from the current catalog. `Assets.Resolve()` resolves the stable ID first and returns the current native asset path. Scene, Prefab, and Audio APIs provide `AssetReference` overloads. Managed code does not receive C++ pointers or catalog memory layouts.


## 0.9.9f shared-player managed hosting

The reusable `vespera_player` can host any project that declares `managed_project` and `managed_assembly`. Source-project launches through `run-project.ps1` and generic exports build the project-owned C# project with the existing staged/last-good helper using `-NoMirrors`, so arbitrary projects no longer copy their output into the reference editor/runtime managed folders. Packaged managed files live under the game package's `managed/` directory and use the current ABI v12 / 88-field `UiSurface` bridge.

All three Project Hub templates now include `managed/VesperaGame.Scripts.csproj` plus project-owned source and scene attachments. The template `.csproj` intentionally does not hard-code a relative path back into the engine checkout: `tools/build-managed-editor.ps1` supplies `VesperaSdkProject=<matching checkout>/managed/Vespera.NET/Vespera.NET.csproj` at build time. `run.ps1 -Project ...` builds/stages the selected project's assembly before opening the editor, while `run-project.ps1` and `export.ps1` use the same staged/last-good helper. Template-specific movement/UI behavior stays in project C#, never inside `vespera_player`.

## Relationship to Lua (0.10.0b)

C# remains Vespera's primary full-game scripting path and keeps the component/lifecycle model documented above. Lua is a separate lightweight project-level runtime/mod host, but it deliberately reuses the same scene/input/audio/assets/UI semantics and generic component/property metadata. Projects may configure both C# and a Project v9 `lua_entry`. See `LUA_SCRIPTING.md`.
