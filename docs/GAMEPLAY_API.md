# Gameplay API

The public C# namespace is `Vespera`. This page is a task-oriented map of the most useful runtime types.

## Entity and Scene

```csharp
var player = Scene.FindWithTag("player");
var enemy = Scene.Create("Enemy");
var allEnemies = Scene.FindAllWithTag("enemy");

enemy.Tag = "enemy";
enemy.Layer = "gameplay";
enemy.Enabled = true;
```

`Entity` exposes `Position`, `Rotation`, `Scale`, `Name`, `Tag`, `Layer`, `Enabled`, component helpers, `Clone()`, and `Destroy()`.

## Transform

```csharp
Entity.Transform.Set(
    new Vector3(0, 1, 0),
    new Vector3(0, 0, 0),
    new Vector3(1, 1, 1));
```

For many objects, reuse a `TransformUpdate[]` and call `Transform.SetBatch` to reduce managed/native transitions.

## Camera

```csharp
Camera.SetPose(new Vector3(0, 2, -6), yaw: 0.0f, pitch: 0.0f);
Camera.VerticalFovDegrees = 75.0f;
```

The camera exposes a complete `CameraState` as well as position/yaw/pitch/FOV convenience properties.

## Prefabs

```csharp
var pickup = Prefab.Instantiate(
    "prefabs/pickup.slprefab",
    new Vector3(3, 0, 2),
    "Pickup");
```

## Physics and collision

Cylinder Collider provides movement resolution and overlap helpers:

```csharp
if (Entity.CylinderCollider is { } collider)
    collider.MoveBy(new Vector3(dx, 0, dz));
```

The static `Physics` API includes `Raycast2D`, `OverlapCircle2D`, and `ResolveCircleMotion2D`.

## Input and Time

```csharp
if (Input.Pressed("jump")) { /* ... */ }
var move = Input.Value("move_horizontal");
var dt = Time.DeltaTime;
```

`Time.ElapsedTime` is also available. `GameTimer` provides a small reusable duration/repeat timer.

## UI

```csharp
var score = UI.FindRequired("score");
score.Text = "Score 10";
score.Visible = true;
score.SetClass("warning", enabled: false);
score.SetProperty("left", "24px");

if (UI.Find("reset")?.Clicked ?? false)
    ResetGame();
```

`UiElement` also supports interactable/focus/read-only state, numeric and text values, colors, image/font assignment, and RmlUi property/class changes where supported.

## Audio

```csharp
Audio.MasterVolume = 0.8f;
Audio.PlayOneShot("audio/click.wav");
var hum = Audio.PlaySpatial("audio/hum.wav", Entity.Position, loop: true);
hum?.Follow(Entity);
```

## Assets

Use `Assets.FromPath(...)` to create a stable-reference value from a project-relative path, and `Assets.Resolve(...)` when an API needs the current path for that reference.

## Save data

```csharp
SaveData.UseSlot("slot1");
SaveData.SetInt("score", 42);
SaveData.SetVector3("spawn", Entity.Position);
SaveData.Save();

if (SaveData.Load())
    Log.Info($"Loaded score {SaveData.GetInt("score")}");
```

SaveData supports string, int, float, bool, Vector2, and Vector3 values. Slots are written as human-readable JSON under the configured save directory.

## Scene switching

```csharp
Scene.Load("scenes/level2.slscene");
// or
Scene.Reload();
```

## Logging

```csharp
Log.Info("Loaded level");
Log.Warning("Missing optional asset");
Log.Error("Could not continue");
```
