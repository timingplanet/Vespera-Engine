# Gameplay scene API - 0.6.3

0.4.x turns the Entity/component model into a reusable gameplay-facing API rather than only serialized editor data.

## Entity identity

Every Entity has:

- stable `SceneObjectId`
- `name`
- `enabled`
- `tag`
- `layer`
- required Transform
- zero or more optional components

Tags are semantic labels. Layers are broad system groupings. They are strings for now; project-defined indexed/layer-mask optimization can be introduced later without changing their authoring intent.

New/migrated defaults are:

```text
tag   = Untagged
layer = Default
```

## Scene queries

The native Scene API provides direct lookup/query helpers for:

- id
- name
- first/all by tag
- all by layer
- all by built-in component type or stable component key

`clone_entity()` duplicates the complete current Entity/component data while allocating a fresh stable id.

SectorWorld also provides `find_sector_index(point)` and `find_sector_by_name(name)`.

## Trigger events

`scene_circle_overlapping_triggers()` returns stable ids for enabled trigger Cylinder Colliders overlapping an X/Z query circle.

`TriggerTracker` stores the previous overlap set and emits deterministic:

```text
TriggerEventType::Enter
TriggerEventType::Exit
```

The tracker stores ids rather than pointers so normal Entity vector movement does not invalidate its state.

This is intentionally a low-level runtime primitive. Future C# / Lua lifecycle wrappers can translate these events into callbacks without moving trigger logic into either scripting runtime.

## 2.5D raycast

`raycast_scene_2d()` casts on the X/Z gameplay plane at the supplied origin Y height.

It can hit:

- sector wall segments
- entity Cylinder Colliders
- trigger colliders when enabled in options

Portal edges are skipped if origin Y lies inside the portal's floor/ceiling overlap. The query returns the nearest hit.

`SceneRaycastOptions` supports:

- max distance
- sector wall inclusion
- entity collider inclusion
- trigger collider inclusion
- ignored entity id
- required entity tag
- required entity layer

This is meant for interaction probes, simple weapons, AI visibility checks, and editor/debug tooling in the sector-based world. A future full mesh/physics raycast can coexist under a different API rather than silently changing the meaning of this deterministic 2.5D query.

## Built-in component metadata

Built-in components expose stable component keys and property keys/types.

Current component keys:

```text
sectorline.transform
sectorline.sprite_renderer
sectorline.cylinder_collider
sectorline.point_light
```

The generic built-in property access API uses `BuiltinPropertyValue` and semantic keys instead of native member offsets. Native C++ code can still use the typed structs directly.

This boundary is deliberate: future managed/script/tool bindings should not depend on the memory layout of C++ structs.



## Managed gameplay API in 0.6.0

0.6.0 is the first broad gameplay-facing expansion of `Vespera.NET`. It adapts existing native systems instead of creating managed-only replacements.

Public C# surfaces now include:

- `Input.Value/Down/Pressed/Released(action)` over the native `InputMap`
- `Time.DeltaTime`, `Time.ElapsedTime`, and reusable `GameTimer`
- `Scene.Find`, `Scene.FindWithTag`, and `Scene.Create`
- `Entity.Clone`, `Entity.Destroy`, and `Entity.HasComponent`
- `Prefab.Instantiate(path, position, name)` using the native prefab loader/instantiator
- `Physics.Raycast2D(...)` over `raycast_scene_2d()`
- `Component.OnTriggerEnter(Entity)` / `OnTriggerExit(Entity)` fed by native trigger tracking
- `SaveData` for simple JSON-backed bool/int/float/string key/value persistence

The reference game exercises these surfaces from C# rather than treating them as untested wrappers. The managed ABI is now **v2**; scene text remains v11 and prefab text remains v4.

## Prefab and native asset discovery foundation

0.4.5 adds a renderer-independent single-Entity prefab API:

- `load_entity_prefab()`
- `save_entity_prefab()`
- `instantiate_entity_prefab()`

Prefab instantiation always allocates a fresh `SceneObjectId`. A caller can attach a project-relative source asset path to the instance; Scene Text v8 persists that path for editor Apply/Revert/Unpack workflows. Runtime code is never forced into automatic live-link behavior.

`AssetCatalog` provides a shared native filesystem discovery layer for `.slscene` and `.slprefab` assets. This is intentionally a **catalog foundation**, not the final importer/GUID/resource database. Later texture/audio/model importers can build on the same project-level discovery concept without teaching every editor panel how to scan folders independently.

## What this is not yet

0.4.5 does not implement:

- arbitrary plugin component registration
- C# or Lua lifecycle dispatch
- a physics layer collision matrix
- rigid bodies
- arbitrary 3D collider shapes
- generic mesh raycasts

Those should build on this API rather than replace it.

## Point Light foundation

`PointLightComponent` is a first renderer-consumed gameplay/editor component with stable key `sectorline.point_light`. It exposes semantic `color`, `intensity`, and `radius` properties through the same built-in metadata/property API intended for future C#, Lua, MCP, and generated Inspector surfaces. The current D3D12 backend uploads up to 32 enabled point lights per rendered view through a dedicated GPU constant buffer, using radial attenuation and no shadows. If a scene exceeds that initial budget, the closest 32 to the active camera are selected.

### 0.5.4 managed additions

`Vespera.Entity` now exposes a first-class `Transform` handle while preserving direct Position/Rotation/Scale aliases. Managed value types include `Vector2`, `Vector3`, and `Color`; enums are supported for explicitly exposed Inspector fields. `Component.OnDestroy()` is invoked before reload/shutdown.

### 0.5.5 managed foundation completion

The public gameplay surface is intentionally not broadened in 0.5.5. Instead, live replacement is hardened: a newly built game assembly is prepared in a candidate collectible context and every enabled scene attachment is validated against it before the active managed runtime is destroyed. Build-time metadata generation now rejects unsupported/ambiguous `[Expose]` declarations. The native managed ABI remains v1; broader Entity/Scene/Input/trigger/audio gameplay bindings begin in 0.6.x.

## 0.6.1 gameplay breadth additions

0.6.1 keeps the 0.6.0 APIs and adds four related gameplay capabilities without introducing a second managed scene/component model.

### Audio

`GameContext` now owns an engine `AudioSystem` service backed by SDL3. `Vespera.NET` exposes:

```csharp
Audio.MasterVolume = 0.8f;
Audio.PlayOneShot("assets/audio/click.wav", 0.5f);
```

This checkpoint intentionally supports WAV one-shots first. Asset importing, richer Audio Source/Listener authoring, and spatial mixing are later work.

### Safe scene-load requests

```csharp
Scene.Load("assets/scenes/level02.slscene");
Scene.Reload();
```

These are end-of-frame requests. Native game/runtime ownership performs the actual load only after the current C# `Update()` returns, then recreates managed scene attachments against the new Scene. This avoids self-destructing a managed context from inside its own call stack.

### Runtime managed-script mutation

```csharp
entity.AddScript<MyRuntimeBehaviour>();
entity.HasScript<MyRuntimeBehaviour>();
entity.RemoveScript<MyRuntimeBehaviour>();
```

The native host synchronizes enabled `ManagedScriptComponent` attachments at frame boundaries. This means prefab/runtime entities can gain managed behavior without requiring a DLL reload.

### Semantic built-in properties

C# can use the same component/property keys already used by native reflection/editor code:

```csharp
entity.SetFloat("sectorline.sprite_renderer", "animation_speed", 1.5f);
entity.SetBool("sectorline.sprite_renderer", "animation_paused", false);
entity.SetColor("sectorline.sprite_renderer", "color", new Color(1, 0.8f, 0.8f));
```

The bridge supports float, bool, string, Vector2, Vector3, and Color values in 0.6.1. This deliberately preserves the one-semantic-API direction needed by C#, Lua, editor tooling, MCP, docs, and future visual scripting.


## 0.6.2 gameplay depth additions

### Persistent managed audio voices

`Audio.Play(...)` returns an `AudioSource` handle wrapper. Sources can stop, loop, pause/resume, change volume, and update a spatial position. `Audio.PlaySpatial(...)` configures source position plus minimum/maximum attenuation distance. `AudioListener.UseCamera()` restores the default camera-following listener after a script explicitly overrides `AudioListener.Position`. The first spatial pass is distance attenuation only; stereo panning/HRTF are future mixer work.

### Collision overlap and motion queries

`Physics.OverlapCircle2D(...)` returns Entity handles for overlapping Cylinder Colliders. `Physics.ResolveCircleMotion2D(...)` exposes the same full-candidate/X-slide/Z-slide deterministic resolver used natively. These do not introduce a managed physics scene.

### SpriteRenderer typed wrapper

`Entity.SpriteRenderer` wraps semantic keys for animation clip, speed, pause, time offset, size, and tint. `Play`, `Pause`, `Resume`, `Restart`, and `Stop` are convenience behavior over those existing properties. `Time.ElapsedTime` now stays on the process-runtime clock across scene switches/reloads so restart offsets share a stable epoch with rendering.

### Lifecycle

Managed components now receive `OnEnable()` / `OnDisable()` transitions as their owning Entity becomes active/inactive. `OnDisable()` is also sent before active instances are discarded during reload/destruction.

## 0.6.3 gameplay-foundation completion additions

### Managed collection queries

`Scene.All`, `Scene.FindAllWithTag`, `Scene.FindAllOnLayer`, and `Scene.FindAllWithComponent` return stable managed Entity handles in native Scene order. The C ABI uses a count/fill pattern so managed code does not depend on native containers.

### Deferred Entity destruction

Managed `Entity.Destroy()` queues the native id for end-of-managed-frame commit. Vespera invokes `OnDisable()` / `OnDestroy()` for attached scripts while the native Entity is still resolvable, removes live script attachments, then destroys native storage. This prevents C# from invalidating the Entity vector while managed script iteration is active.

### Typed built-in wrappers

`Entity.CylinderCollider` and `Entity.PointLight` expose the current native built-ins through semantic keys. `EnsureCylinderCollider()` / `EnsurePointLight()` add those components through the same native component API. Collider convenience methods use the existing 2.5D overlap/resolved-motion functions rather than a managed physics copy.

### Trigger stay

`Component.OnTriggerStay(Entity)` is dispatched from `TriggerTracker::active_trigger_ids()` each frame. Enter, stay, and exit therefore share one native overlap source of truth.

### Runtime helper polish

Spatial `AudioSource` can automatically follow an Entity; managed voice wrappers are centrally tracked and cleaned up when the managed game assembly is released. `Audio.ActiveVoiceCount` / `Audio.StopAll` expose basic mixer state/control. `SaveData` supports Vector2/Vector3 helpers and key enumeration, and switching to another/missing slot clears stale in-memory values; `GameTimer` exposes remaining time, normalized progress, and restart.

