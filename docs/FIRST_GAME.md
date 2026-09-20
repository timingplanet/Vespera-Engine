# Your First Game

This walkthrough uses the **3D / 2.5D Game** starter because it already contains a scene, input actions, a C# assembly, and a small controller you can inspect.

## 1. Create the project

Open the Project Hub with `./vespera.ps1`, choose **3D / 2.5D Game**, select a folder, and open the project.

The starter gives you a working scene plus `managed/StarterController.cs`. Press Play once before changing anything. WASD should move the starter actor and Shift should sprint.

## 2. Inspect the starter entity

Select the moving entity in the Hierarchy. Its Transform is the entity's authored position, rotation, and scale. The starter also uses a Cylinder Collider so movement can resolve against the world instead of blindly writing a position.

The controller reads project input actions rather than raw keyboard state:

```csharp
var x = Input.Value("move_right");
var z = Input.Value("move_forward");
var sprinting = Input.Down("sprint");
```

This is the preferred pattern. Your script depends on action names; Project Settings decides which keys or controller inputs drive them.

## 3. Add your own script

Create a C# class in the project's managed folder:

```csharp
using Vespera;

namespace VesperaGame;

public sealed class BobUpAndDown : Component
{
    [Expose("Height")]
    [Range(0.0f, 5.0f)]
    public float Height = 0.5f;

    [Expose("Speed")]
    public float Speed = 2.0f;

    private float _startY;

    public override void Start()
    {
        _startY = Entity.Position.Y;
    }

    public override void Update(float deltaTime)
    {
        var p = Entity.Position;
        p.Y = _startY + MathF.Sin((float)Time.ElapsedTime * Speed) * Height;
        Entity.Position = p;
    }
}
```

Build C# scripts from the editor, refresh metadata if needed, then add the script component to an entity. Fields marked with `[Expose]` appear in the Inspector.

## 4. Add an input action

Open **Project Settings → Input Actions**. Add an action such as `interact` and bind a key or gamepad control.

Use it from a script:

```csharp
if (Input.Pressed("interact"))
    Log.Info("Interact pressed");
```

`Pressed` is true on the transition into the pressed state. `Down` remains true while held. `Released` reports the release transition. `Value` returns the action's numeric value and is useful for axes and paired positive/negative bindings.

## 5. Add a reusable prefab

Select an entity and use **Create Prefab from Selected**. The new `.slprefab` becomes an asset in the Project browser. Instances keep a stable link to the prefab source until you unpack them.

At runtime you can spawn one with:

```csharp
var enemy = Prefab.Instantiate("prefabs/enemy.slprefab", new Vector3(4, 0, 2), "Enemy");
```

## 6. Add UI

If your project has a startup RML document, give an element an `id`, then look it up from C#:

```csharp
private UiElement? _score;

public override void Start()
{
    _score = UI.Find("score");
}

public void SetScore(int value)
{
    if (_score is not null)
        _score.Text = $"Score {value}";
}
```

For RmlUi elements you can also set CSS properties and classes through `SetProperty` and `SetClass`.

## 7. Save and build

Save with `Ctrl+S`, then use **Build → Build Game…**. Start with a Development build while iterating. Use Release when you want the normal shipping configuration.

At this point you have exercised the core public workflow: project → scene → entity → C# → input → prefab/UI → standalone package.
