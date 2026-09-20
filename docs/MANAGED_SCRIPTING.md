# C# Scripting

C# is Vespera's primary gameplay scripting path. A game owns its managed project and assembly; scripts derive from `Vespera.Component` and attach to scene entities.

## Create a component

```csharp
using Vespera;

namespace VesperaGame;

public sealed class Rotator : Component
{
    [Expose("Degrees Per Second")]
    [Range(-720.0f, 720.0f)]
    [Tooltip("Rotation speed around the Y axis")]
    public float Speed = 90.0f;

    public override void Update(float deltaTime)
    {
        var r = Entity.Rotation;
        r.Y += Speed * deltaTime * (MathF.PI / 180.0f);
        Entity.Rotation = r;
    }
}
```

Build the project's C# scripts and refresh metadata. Attach the component in the Inspector.

## Lifecycle

A component can override:

```csharp
Start()
OnEnable()
Update(float deltaTime)
OnDisable()
OnTriggerEnter(Entity other)
OnTriggerStay(Entity other)
OnTriggerExit(Entity other)
OnDestroy()
```

Use `Start` for initialization, `Update` for per-frame logic, trigger callbacks for Cylinder Collider trigger events, and `OnDestroy` for cleanup before a script instance is discarded.

## Exposed Inspector fields

Use attributes on public fields:

```csharp
[Expose("Move Speed")]
[Range(0.25f, 12.0f)]
[Tooltip("Units per second")]
public float MoveSpeed = 3.0f;
```

If you rename an exposed field but need authored Inspector data to survive, use:

```csharp
[FormerlySerializedAs("OldFieldName")]
[Expose]
public float NewFieldName = 1.0f;
```

## The attached entity

Every component receives an `Entity` property:

```csharp
Entity.Position = new Vector3(0, 1, 0);
Entity.Tag = "player";
Entity.EnsurePointLight().Intensity = 2.0f;
```

The managed API works through stable entity IDs and semantic component/property calls rather than exposing native memory layouts.

## Build and last-good behavior

Use **Build C# Scripts** from the editor after changing managed code. If compilation fails, Vespera reports diagnostics and preserves the last known-good managed output rather than replacing it with a broken assembly.

Fix the compiler error and build again. If the editor needs refreshed reflected fields/classes, use **Refresh C# Metadata**.

## Project-owned assembly

The `.vesperaproject` file names the managed `.csproj` and output assembly. Keep game scripts in that project. Do not add gameplay code to Vespera's engine-managed assembly.
