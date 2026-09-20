using Vespera;

namespace ReferenceGame.Scripts;

// Reference coverage for transactional Entity.Destroy(). The native host must call
// OnDestroy while the Entity still exists, then remove native storage after all
// managed Update calls have returned.
public sealed class DestroyLifecycleProbe : Component
{
    private readonly GameTimer _lifetime = new(1.25f);
    private bool _requested;

    public override void Start()
    {
        Log.Info($"DestroyLifecycleProbe.Start entity={Entity.Id}");
    }

    public override void Update(float deltaTime)
    {
        if (_requested || !_lifetime.Tick(deltaTime)) return;
        _requested = Entity.Destroy();
        Log.Info($"DestroyLifecycleProbe requested deferred destroy entity={Entity.Id} accepted={_requested}");
    }

    public override void OnDestroy()
    {
        Log.Info($"DestroyLifecycleProbe.OnDestroy entity={Entity.Id} ExistsBeforeNativeRemove={Entity.Exists}");
    }
}
