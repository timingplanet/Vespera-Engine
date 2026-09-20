using Vespera;

namespace ReferenceGame.Scripts;

// Proves a C# Component can be attached to a runtime-spawned Entity without a
// full assembly reload, and exercises the enable/disable lifecycle.
public sealed class RuntimeSpawnReporter : Component
{
    public override void Start()
    {
        Log.Info($"RuntimeSpawnReporter.Start on dynamically scripted entity {Entity.Id}");
    }

    public override void OnEnable()
    {
        Log.Info($"RuntimeSpawnReporter.OnEnable on entity {Entity.Id}");
    }

    public override void OnDisable()
    {
        Log.Info($"RuntimeSpawnReporter.OnDisable on entity {Entity.Id}");
    }

    public override void OnDestroy()
    {
        Log.Info($"RuntimeSpawnReporter.OnDestroy on entity {Entity.Id}");
    }
}
