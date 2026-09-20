using Vespera;

namespace ReferenceGame.Scripts;

// Small reference component: rotating the watcher changes which directional sprite
// frame the native renderer selects, proving C# -> native Transform writes are live.
public enum SpinDirection
{
    Clockwise,
    CounterClockwise,
}

public sealed class ManagedSpinner : Component
{
    [Expose("Radians Per Second")]
    [Range(0.0f, 6.0f)]
    [Tooltip("How quickly the watcher rotates around its Y axis.")]
    [FormerlySerializedAs("SpinSpeed")]
    public float RadiansPerSecond = 0.65f;

    [Expose("Direction")]
    [Tooltip("Choose which direction the watcher rotates.")]
    public SpinDirection Direction = SpinDirection.Clockwise;

    public override void Start()
    {
        Log.Info($"ManagedSpinner.Start on entity {Entity.Id}");
    }

    public override void Update(float deltaTime)
    {
        if (Input.Pressed("probe"))
            Direction = Direction == SpinDirection.Clockwise ? SpinDirection.CounterClockwise : SpinDirection.Clockwise;

        var rotation = Entity.Transform.Rotation;
        var sign = Direction == SpinDirection.Clockwise ? 1.0f : -1.0f;
        rotation.Y += RadiansPerSecond * sign * deltaTime;
        Entity.Transform.Rotation = rotation;
    }

    public override void OnDestroy()
    {
        Log.Info($"ManagedSpinner.OnDestroy on entity {Entity.Id}");
    }
}
