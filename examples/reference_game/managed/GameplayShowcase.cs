using Vespera;

namespace ReferenceGame.Scripts;

// Reference component that exercises the public Vespera.NET gameplay APIs.
// It intentionally uses only public engine surfaces so the sample stays
// representative of what an external C# project can do.
public sealed class GameplayShowcase : Component
{
    private static readonly AssetReference MainScene = new(
        "6f0cf9d1daf63e7f10485c6f60ffd428", "assets/scenes/connected_sectors.slscene");
    private static readonly AssetReference AlternateScene = new(
        "1794a50560a8f23ac5a50a743448bfc8", "assets/scenes/alternate_chamber.slscene");
    private static readonly AssetReference WatcherPrefab = new(
        "3ea2605121c0ad8e0875f49b7b5c334c", "assets/prefabs/watcher.slprefab");
    private static readonly AssetReference ChimeAudio = new(
        "421c432b92520be0ef3890880ce929e4", "assets/audio/test_chime.wav");
    private readonly GameTimer _reportTimer = new GameTimer(8.0f, repeat: true);
    private Entity? _runtimeMarker;
    private Entity? _spawnedWatcher;
    private AudioSource? _spatialSource;
    private UiElement? _uiProgress;
    private UiElement? _continueButton;

    public override void Start()
    {
        var firstEnemy = Scene.FindWithTag("enemy");
        Log.Info($"Gameplay API: first enemy = {(firstEnemy is null ? "<none>" : firstEnemy.Id)} | scene={Scene.CurrentPath}");
        var recoveredPrefab = Assets.FromPath("assets/prefabs/watcher.slprefab");
        Log.Info($"Gameplay API: stable asset API prefab-id={(string.IsNullOrWhiteSpace(recoveredPrefab.Id) ? "<missing>" : recoveredPrefab.Id[..8])} resolved={WatcherPrefab.Exists}");

        var uiTitle = UI.Find("Title");
        _uiProgress = UI.Find("Runtime Progress");
        _continueButton = UI.Find("Continue Button");
        if (uiTitle is not null)
        {
            uiTitle.Text = "Vespera Runtime UI + C#";
            if (_uiProgress is not null)
            {
                // Exercise the string-valued UI bridge through the shared UiSurface.
                // The reference HUD still uses the .slui compatibility path.
                _uiProgress.ValueText = "0.35";
            }
            Log.Info($"Gameplay API: UiSurface bridge active progress={_uiProgress is not null} button={_continueButton is not null} value={_uiProgress?.ValueText ?? "<none>"}");
        }

        var enemies = Scene.FindAllWithTag("enemy");
        var gameplayLayer = Scene.FindAllOnLayer("Gameplay");
        var colliders = Scene.FindAllWithComponent("sectorline.cylinder_collider");
        Log.Info($"Gameplay API: collection queries -> enemies={enemies.Length} gameplay-layer={gameplayLayer.Length} colliders={colliders.Length} all={Scene.All.Length}");

        _spawnedWatcher = Scene.Find("C# Spawned Watcher");
        if (_spawnedWatcher is null)
        {
            _spawnedWatcher = Prefab.Instantiate(
                WatcherPrefab,
                new Vector3(1.55f, 0.60f, 4.75f),
                "C# Spawned Watcher");
            if (_spawnedWatcher is not null)
            {
                Log.Info($"Gameplay API: C# prefab spawn created entity {_spawnedWatcher.Id}");
                var sprite = _spawnedWatcher.SpriteRenderer;
                if (sprite is not null)
                {
                    sprite.Play("Watcher Walk", 1.8f);
                    sprite.Tint = new Color(0.70f, 1.0f, 0.78f, 1.0f);
                    Log.Info("Gameplay API: typed SpriteRenderer animation/tint control active");
                }
                if (_spawnedWatcher.AddScript<RuntimeSpawnReporter>())
                    Log.Info("Gameplay API: attached a C# script to a runtime-spawned Entity");
            }
        }

        _runtimeMarker = Scene.Find("C# Runtime Marker") ?? Scene.Create("C# Runtime Marker");
        _runtimeMarker.Position = new Vector3(0.0f, 0.35f, 4.25f);
        _runtimeMarker.Tag = "runtime_marker";
        _runtimeMarker.Layer = "Gameplay";

        var markerCollider = _runtimeMarker.EnsureCylinderCollider();
        markerCollider.Radius = 0.22f;
        markerCollider.Height = 0.70f;
        markerCollider.Center = new Vector3(0.0f, 0.35f, 0.0f);
        markerCollider.IsTrigger = true;
        var typedOverlaps = markerCollider.Overlaps(includeTriggers: true);

        var markerLight = _runtimeMarker.EnsurePointLight();
        markerLight.Color = new Color(0.40f, 0.85f, 1.0f, 1.0f);
        markerLight.Intensity = 0.35f;
        markerLight.Radius = 2.25f;
        Log.Info($"Gameplay API: typed Collider + PointLight active; collider overlaps={typedOverlaps.Length}");

        // Exercise transactional destruction on a dynamically scripted Entity.
        var destroyProbe = Scene.Find("C# Deferred Destroy Probe") ?? Scene.Create("C# Deferred Destroy Probe");
        destroyProbe.Position = new Vector3(-0.75f, 0.35f, 4.25f);
        if (!destroyProbe.HasScript<DestroyLifecycleProbe>() && destroyProbe.AddScript<DestroyLifecycleProbe>())
            Log.Info($"Gameplay API: attached deferred-destroy lifecycle probe to entity {destroyProbe.Id}");

        if (Physics.Raycast2D(Entity.Position, new Vector3(0.0f, 0.0f, 1.0f), out var hit, 12.0f, includeTriggers: true, ignore: Entity))
            Log.Info($"Gameplay API: C# raycast hit {hit.Type} at {hit.Distance:0.00}m");

        var overlaps = Physics.OverlapCircle2D(new Vector3(0.0f, 0.6f, 5.0f), 3.0f, includeTriggers: true);
        Log.Info($"Gameplay API: overlap-circle query found {overlaps.Length} collider(s)");

        var motionFrom = new Vector3(-1.6f, 0.6f, 8.3f);
        var motionCandidate = new Vector3(-1.6f, 0.6f, 7.8f);
        var motionResolved = Physics.ResolveCircleMotion2D(motionFrom, motionCandidate, 0.35f);
        Log.Info($"Gameplay API: resolve-circle motion -> ({motionResolved.X:0.00}, {motionResolved.Z:0.00})");

        SaveData.UseSlot("reference_game");
        SaveData.Load();
        var starts = SaveData.GetInt("managed_starts") + 1;
        SaveData.SetInt("managed_starts", starts);
        SaveData.SetVector3("last_start_position", Entity.Position);
        SaveData.Save();
        Log.Info($"Gameplay API: SaveData managed_starts={starts} keys={SaveData.Keys.Length}");
    }

    public override void Update(float deltaTime)
    {
        if (_uiProgress is { Exists: true })
            _uiProgress.Value = 0.5f + 0.5f * System.MathF.Sin((float)Time.ElapsedTime * 0.8f);
        if (_continueButton is { Exists: true } && _continueButton.Clicked)
            Log.Info("Gameplay API: native UI button click reached C#");
        if (Input.Pressed("probe"))
            Log.Info($"Gameplay API: C# observed the 'probe' input action at t={Time.ElapsedTime:0.00}s");

        if (Input.Pressed("audio_test"))
        {
            var played = Audio.PlayOneShot(ChimeAudio, 0.45f);
            Log.Info($"Gameplay API: audio one-shot requested -> {(played ? "playing" : "failed")} | voices={Audio.ActiveVoiceCount}");
        }

        if (Input.Pressed("audio_spatial"))
        {
            if (_spatialSource is { IsPlaying: true })
            {
                _spatialSource.Stop();
                _spatialSource = null;
                Log.Info($"Gameplay API: stopped looping spatial AudioSource | voices={Audio.ActiveVoiceCount}");
            }
            else if (_spawnedWatcher is { Exists: true })
            {
                _spatialSource = Audio.PlaySpatial(ChimeAudio, _spawnedWatcher.Position,
                    0.55f, loop: true, minDistance: 1.0f, maxDistance: 14.0f);
                if (_spatialSource is not null)
                    _spatialSource.Follow(_spawnedWatcher, 1.0f, 14.0f); // automatic follow from now on
                Log.Info($"Gameplay API: looping auto-follow AudioSource -> {(_spatialSource is null ? "failed" : "playing")} | voices={Audio.ActiveVoiceCount}");
            }
        }

        if (Input.Pressed("lifecycle_test") && _spawnedWatcher is { Exists: true })
        {
            _spawnedWatcher.Enabled = !_spawnedWatcher.Enabled;
            Log.Info($"Gameplay API: runtime watcher Enabled={_spawnedWatcher.Enabled}");
        }

        if (Input.Pressed("scene_next") && !Scene.CurrentPath.EndsWith("alternate_chamber.slscene", System.StringComparison.OrdinalIgnoreCase))
        {
            Log.Info("Gameplay API: requesting alternate scene at end of frame");
            Scene.Load(AlternateScene);
        }
        if (Input.Pressed("scene_previous") && !Scene.CurrentPath.EndsWith("connected_sectors.slscene", System.StringComparison.OrdinalIgnoreCase))
        {
            Log.Info("Gameplay API: requesting main scene at end of frame");
            Scene.Load(MainScene);
        }

        if (_reportTimer.Tick(deltaTime))
            Log.Info($"Gameplay API: managed timer tick at t={Time.ElapsedTime:0.0}s remaining={_reportTimer.Remaining:0.0}s voices={Audio.ActiveVoiceCount}");
    }

    public override void OnDestroy()
    {
        _spatialSource?.Stop();
        _spatialSource = null;
        if (_runtimeMarker is { Exists: true })
            _runtimeMarker.Destroy();
    }
}
