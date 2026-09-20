# Scenes & Entities

A scene is Vespera's authored runtime world. It contains sector/world data, entities, component state, script attachments, and scene-owned resources such as sprite clips.

## Entities

Every entity has a stable scene ID plus core identity and Transform state. From C# you can access:

```csharp
Entity.Name = "Player";
Entity.Tag = "player";
Entity.Layer = "gameplay";
Entity.Enabled = true;

var p = Entity.Position;
Entity.Position = new Vector3(p.X + 1, p.Y, p.Z);
```

`Entity.Transform` exposes Position, Rotation, and Scale. `Transform.Set(...)` updates all three in one native call, and `Transform.SetBatch(...)` is available for many transform updates.

## Finding and creating entities

```csharp
var player = Scene.Find("Player");
var tagged = Scene.FindWithTag("player");
var enemies = Scene.FindAllWithTag("enemy");
var uiLayer = Scene.FindAllOnLayer("ui");
var colliders = Scene.FindAllWithComponent("sectorline.cylinder_collider");

var entity = Scene.Create("Spawned Object");
```

An `Entity` is a lightweight handle around the stable native entity ID. Check `Exists` if you retain a reference across destructive gameplay.

## Built-in components

The public C# convenience wrappers currently include:

- `SpriteRenderer`
- `MeshRenderer`
- `CylinderCollider`
- `PointLight`

Use `HasComponent`, `AddComponent`, and `RemoveComponent` for the semantic built-in component surface. `EnsureMeshRenderer`, `EnsureCylinderCollider`, and `EnsurePointLight` add those components if needed and return their typed wrapper.

## Hierarchy

Author parent/child relationships in the Hierarchy. Parenting is scene state: it is saved with the scene, participates in validation, and is preserved across ordinary save/reopen workflows.

Use editor commands for reparenting instead of hand-editing `.slscene` files. The editor rejects invalid hierarchy operations such as ancestry cycles.

## Sectors

Vespera's world model supports sector-based 2.5D content. Sectors define floor/ceiling/wall geometry and portal-connected areas. The 3D starter uses this world path together with entity components.

## Scene switching

From C#:

```csharp
Scene.Load("scenes/level2.slscene");
```

`Scene.Load(...)` requests a scene change and the native owner commits it after the current managed update returns. `Scene.Reload()` intentionally reloads the current scene.

Use an `AssetReference` overload when you already have a stable asset reference.
