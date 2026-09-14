using System.Globalization;
using System.Text;
using System.Text.Json;
using Vespera;

namespace VesperaPerformanceLab;

public sealed class PerformanceLabController : Component
{
    private enum Preset { Baseline, Gameplay, Heavy, Torture }

    private sealed class Actor
    {
        public required Entity Entity;
        public required Vector3 BasePosition;
        public required Vector3 BaseRotation;
        public required Vector3 Scale;
        public required float Phase;
        public required float Speed;
        public required float Amplitude;
        public required int Motion;
    }

    private sealed record Result(
        Preset Preset, int Frames, float AverageFps, float OnePercentLow, float WorstMs,
        float AverageUpdateMs, float AverageRenderMs, float AverageDrawCalls,
        float AverageVisibleMeshes, float AverageCulledMeshes);
    private readonly record struct TourPoint(Vector3 Position, Vector3 LookAt, float Fov);

    private readonly Dictionary<Preset, List<Entity>> _groups = new();
    private readonly Dictionary<Preset, List<Actor>> _actors = new();
    private readonly Dictionary<string, Entity> _meshPrototypes = new();
    private readonly Dictionary<Preset, Entity> _lightPrototypes = new();
    private readonly Dictionary<Preset, int> _lightCounts = new();
    private readonly List<float> _liveFrames = new(420);
    private readonly List<float> _phaseFrames = new(2400);
    private readonly List<Result> _results = new();
    private TransformUpdate[] _transformUpdates = new TransformUpdate[10000];

    private PerformanceSnapshot _latestPerformance;
    private bool _hasPerformance;
    private double _phaseUpdateMsTotal;
    private double _phaseRenderMsTotal;
    private double _phaseDrawCallsTotal;
    private double _phaseVisibleMeshesTotal;
    private double _phaseCulledMeshesTotal;
    private int _phaseTelemetrySamples;

    private UiElement _presetName = null!;
    private UiElement _liveFps = null!;
    private UiElement _frameMs = null!;
    private UiElement _avgFps = null!;
    private UiElement _oneLow = null!;
    private UiElement _worstMs = null!;
    private UiElement _objectCount = null!;
    private UiElement _moverCount = null!;
    private UiElement _lightCount = null!;
    private UiElement _updateMs = null!;
    private UiElement _renderMs = null!;
    private UiElement _drawCalls = null!;
    private UiElement _visibleMeshes = null!;
    private UiElement _culledMeshes = null!;
    private UiElement _benchmarkState = null!;
    private UiElement _progressFill = null!;
    private UiElement _resultSummary = null!;
    private UiElement _runBenchmark = null!;
    private UiElement _resetStats = null!;
    private readonly Dictionary<Preset, UiElement> _buttons = new();

    private Preset _preset = Preset.Baseline;
    private float _uiTick;
    private float _manualTourClock;
    private float _benchmarkPhaseClock;
    private bool _benchmarkRunning;
    private int _benchmarkPhase;
    private float _warmupRemaining;
    private float _sampleRemaining;

    // Long enough to see each tier's fly-through and to amortize one-off hitches.
    private const float WarmupSeconds = 1.25f;
    private const float SampleSeconds = 7.0f;
    private const float PhaseSeconds = WarmupSeconds + SampleSeconds;
    private const float ManualTourSeconds = 12.0f;

    private static readonly Color BaselineColor = new(0.18f, 0.58f, 1.00f, 1.0f);
    private static readonly Color GameplayColor = new(0.10f, 0.92f, 0.67f, 1.0f);
    private static readonly Color HeavyColor = new(1.00f, 0.56f, 0.12f, 1.0f);
    private static readonly Color TortureColor = new(1.00f, 0.18f, 0.43f, 1.0f);
    private static readonly Preset[] Presets = { Preset.Baseline, Preset.Gameplay, Preset.Heavy, Preset.Torture };

    private static readonly TourPoint[] BaselineRoute =
    {
        new(new Vector3(0, 5.2f, -15), new Vector3(0, 7.5f, 14), 72),
        new(new Vector3(-11, 7.5f, 5), new Vector3(7, 9.0f, 28), 74),
        new(new Vector3(9, 11.5f, 25), new Vector3(-7, 8.0f, 40), 76),
        new(new Vector3(0, 6.0f, 41), new Vector3(0, 10.0f, 10), 73),
    };

    private static readonly TourPoint[] GameplayRoute =
    {
        new(new Vector3(0, 5.5f, 43), new Vector3(0, 9.0f, 66), 75),
        new(new Vector3(13, 10.5f, 57), new Vector3(-10, 8.0f, 77), 78),
        new(new Vector3(-14, 13.5f, 75), new Vector3(9, 7.0f, 91), 80),
        new(new Vector3(0, 5.5f, 90), new Vector3(0, 11.0f, 55), 76),
    };

    private static readonly TourPoint[] HeavyRoute =
    {
        new(new Vector3(0, 6.0f, 91), new Vector3(0, 10.0f, 116), 78),
        new(new Vector3(-15, 12.5f, 108), new Vector3(11, 8.0f, 130), 81),
        new(new Vector3(14, 15.5f, 128), new Vector3(-12, 9.5f, 144), 83),
        new(new Vector3(0, 6.0f, 145), new Vector3(0, 12.0f, 108), 79),
    };

    private static readonly TourPoint[] TortureRoute =
    {
        new(new Vector3(0, 6.0f, 146), new Vector3(0, 10.0f, 170), 80),
        new(new Vector3(16, 11.0f, 165), new Vector3(-14, 10.0f, 187), 84),
        new(new Vector3(-17, 16.5f, 186), new Vector3(14, 8.0f, 205), 86),
        new(new Vector3(0, 7.0f, 213), new Vector3(0, 12.0f, 171), 82),
    };

    public override void Start()
    {
        foreach (var preset in Presets)
        {
            _groups[preset] = new List<Entity>();
            _actors[preset] = new List<Actor>();
            _lightCounts[preset] = 0;
        }

        BindUi();
        BuildShowcase();
        SetPreset(Preset.Baseline, true);
        ResetLiveStats();
        UpdateShowcaseCamera(0.0f);

        Log.Info($"Performance Lab cinematic workload ready: {TotalObjects()} renderables, {TotalMovers()} animated transforms, {TotalLights()} point lights generated.");
        Log.Info("Torture is intentionally aggressive: roughly 13K mesh instances and 9K+ animated transform writes per frame; Performance Lab requests uncapped presentation (VSync off).");
    }

    public override void Update(float deltaTime)
    {
        var dt = Math.Clamp(deltaTime, 0.0f, 0.10f);
        _hasPerformance = Performance.TryGetSnapshot(out _latestPerformance);
        var ms = _hasPerformance && _latestPerformance.FrameMs > 0.0001
            ? (float)_latestPerformance.FrameMs
            : Math.Max(0.001f, dt * 1000.0f);
        _liveFrames.Add(ms);
        if (_liveFrames.Count > 420) _liveFrames.RemoveAt(0);
        if (_benchmarkRunning && _warmupRemaining <= 0.0f)
        {
            _phaseFrames.Add(ms);
            if (_hasPerformance)
            {
                _phaseUpdateMsTotal += _latestPerformance.UpdateMs;
                _phaseRenderMsTotal += _latestPerformance.RenderMs;
                _phaseDrawCallsTotal += _latestPerformance.TotalDrawCalls;
                _phaseVisibleMeshesTotal += _latestPerformance.MeshVisible;
                _phaseCulledMeshesTotal += _latestPerformance.MeshCulled;
                _phaseTelemetrySamples++;
            }
        }

        if (!_benchmarkRunning)
        {
            if (_buttons[Preset.Baseline].Clicked) SetPreset(Preset.Baseline);
            if (_buttons[Preset.Gameplay].Clicked) SetPreset(Preset.Gameplay);
            if (_buttons[Preset.Heavy].Clicked) SetPreset(Preset.Heavy);
            if (_buttons[Preset.Torture].Clicked) SetPreset(Preset.Torture);
            if (_runBenchmark.Clicked) BeginBenchmark();
        }
        if (_resetStats.Clicked) ResetLiveStats();

        AnimateActiveWorkload(dt);
        if (_benchmarkRunning) TickBenchmark(dt);
        else _manualTourClock += dt;
        UpdateShowcaseCamera(dt);

        _uiTick += dt;
        if (_uiTick >= 0.20f)
        {
            _uiTick = 0.0f;
            RefreshLiveUi();
        }
    }

    private void BindUi()
    {
        _presetName = UI.FindRequired("preset-name");
        _liveFps = UI.FindRequired("live-fps");
        _frameMs = UI.FindRequired("frame-ms");
        _avgFps = UI.FindRequired("avg-fps");
        _oneLow = UI.FindRequired("one-low");
        _worstMs = UI.FindRequired("worst-ms");
        _objectCount = UI.FindRequired("object-count");
        _moverCount = UI.FindRequired("mover-count");
        _lightCount = UI.FindRequired("light-count");
        _updateMs = UI.FindRequired("update-ms");
        _renderMs = UI.FindRequired("render-ms");
        _drawCalls = UI.FindRequired("draw-calls");
        _visibleMeshes = UI.FindRequired("visible-meshes");
        _culledMeshes = UI.FindRequired("culled-meshes");
        _benchmarkState = UI.FindRequired("benchmark-state");
        _progressFill = UI.FindRequired("progress-fill");
        _resultSummary = UI.FindRequired("result-summary");
        _runBenchmark = UI.FindRequired("run-benchmark");
        _resetStats = UI.FindRequired("reset-stats");
        _buttons[Preset.Baseline] = UI.FindRequired("preset-baseline");
        _buttons[Preset.Gameplay] = UI.FindRequired("preset-gameplay");
        _buttons[Preset.Heavy] = UI.FindRequired("preset-heavy");
        _buttons[Preset.Torture] = UI.FindRequired("preset-torture");
    }

    private void BuildShowcase()
    {
        BuildArchitecture();

        // Each tier lives mainly in its own part of the hall so the camera can visibly
        // fly through progressively denser sections. Every object remains individually
        // authored and animated; the renderer is free to batch compatible primitives.
        BuildSwarm(Preset.Baseline, 700, 500, -4.0f, 44.0f, 1101);
        BuildSwarm(Preset.Gameplay, 1600, 1100, 42.0f, 92.0f, 2203);
        BuildSwarm(Preset.Heavy, 3400, 2500, 90.0f, 146.0f, 3307);
        BuildSwarm(Preset.Torture, 7000, 5300, 144.0f, 216.0f, 4411);

        BuildLights(Preset.Baseline, -2.0f, 44.0f);
        BuildLights(Preset.Gameplay, 44.0f, 92.0f);
        BuildLights(Preset.Heavy, 92.0f, 146.0f);
        BuildLights(Preset.Torture, 146.0f, 216.0f);
    }

    private void BuildArchitecture()
    {
        var frameColor = new Color(0.12f, 0.18f, 0.28f, 1.0f);
        var railColor = new Color(0.16f, 0.34f, 0.52f, 1.0f);
        var coreColor = new Color(0.24f, 0.72f, 1.00f, 1.0f);

        for (var i = 0; i < 29; ++i)
        {
            var z = -10.0f + i * 8.0f;
            MakeMesh(Preset.Baseline, "cube", frameColor, new Vector3(-23.0f, 9.5f, z), default, new Vector3(0.65f, 9.5f, 0.65f));
            MakeMesh(Preset.Baseline, "cube", frameColor, new Vector3(23.0f, 9.5f, z), default, new Vector3(0.65f, 9.5f, 0.65f));
            MakeMesh(Preset.Baseline, "cube", frameColor, new Vector3(0.0f, 19.0f, z), default, new Vector3(23.5f, 0.45f, 0.65f));
            MakeMesh(Preset.Baseline, "cube", railColor, new Vector3(-18.5f, 1.0f, z), default, new Vector3(3.5f, 0.18f, 0.75f));
            MakeMesh(Preset.Baseline, "cube", railColor, new Vector3(18.5f, 1.0f, z), default, new Vector3(3.5f, 0.18f, 0.75f));

            if ((i & 1) == 0)
            {
                var rotor = MakeMesh(Preset.Baseline, "cube", coreColor,
                    new Vector3(0.0f, 15.5f, z + 2.0f), default, new Vector3(5.5f, 0.16f, 0.16f));
                _actors[Preset.Baseline].Add(new Actor
                {
                    Entity = rotor,
                    BasePosition = new Vector3(0.0f, 15.5f, z + 2.0f),
                    BaseRotation = default,
                    Scale = new Vector3(5.5f, 0.16f, 0.16f),
                    Phase = i * 0.31f,
                    Speed = 0.45f + (i % 5) * 0.07f,
                    Amplitude = 0.0f,
                    Motion = 4
                });
            }
        }

        // A few huge distant towers make camera motion/readable parallax obvious.
        for (var i = 0; i < 12; ++i)
        {
            var z = 4.0f + i * 18.0f;
            var x = (i & 1) == 0 ? -16.0f : 16.0f;
            MakeMesh(Preset.Baseline, "cylinder", frameColor,
                new Vector3(x, 6.0f, z), default, new Vector3(2.2f, 6.0f + (i % 3), 2.2f));
        }
    }

    private void BuildSwarm(Preset group, int count, int movingCount, float zMin, float zMax, int seed)
    {
        var random = new Random(seed);
        var baseColor = PresetColor(group);
        for (var i = 0; i < count; ++i)
        {
            var primitive = (i % 7) switch
            {
                0 => "sphere",
                1 => "cylinder",
                _ => "cube"
            };

            var laneBias = random.NextDouble();
            var x = laneBias < 0.38
                ? -21.0f + (float)random.NextDouble() * 8.0f
                : laneBias > 0.62
                    ? 13.0f + (float)random.NextDouble() * 8.0f
                    : -13.0f + (float)random.NextDouble() * 26.0f;
            var y = 1.4f + (float)random.NextDouble() * 16.3f;
            var z = zMin + (float)random.NextDouble() * (zMax - zMin);
            var s = 0.20f + (float)random.NextDouble() * 0.72f;
            var stretch = 0.75f + (float)random.NextDouble() * 1.45f;
            var scale = primitive == "cylinder"
                ? new Vector3(s, s * stretch, s)
                : new Vector3(s * stretch, s, s * (0.75f + (float)random.NextDouble() * 0.65f));

            var entity = MakeMesh(group, primitive, baseColor,
                new Vector3(x, y, z),
                new Vector3((float)random.NextDouble() * 1.3f, (float)random.NextDouble() * 2.5f, (float)random.NextDouble() * 0.8f),
                scale);

            if (i >= movingCount) continue;
            var tier = (int)group;
            _actors[group].Add(new Actor
            {
                Entity = entity,
                BasePosition = new Vector3(x, y, z),
                BaseRotation = new Vector3((float)random.NextDouble(), (float)random.NextDouble() * 2.0f, (float)random.NextDouble()),
                Scale = scale,
                Phase = (float)random.NextDouble() * MathF.PI * 2.0f,
                Speed = 0.55f + (float)random.NextDouble() * (1.15f + tier * 0.55f),
                Amplitude = 1.0f + tier * 1.35f + (float)random.NextDouble() * (1.4f + tier * 0.8f),
                Motion = i & 3
            });
        }
    }

    private void BuildLights(Preset group, float zMin, float zMax)
    {
        var color = PresetColor(group);
        for (var i = 0; i < 8; ++i)
        {
            var t = (i + 0.5f) / 8.0f;
            var z = zMin + (zMax - zMin) * t;
            var x = (i & 1) == 0 ? -12.0f : 12.0f;
            var y = 5.0f + (i % 4) * 3.0f;
            var entity = MakeLight(group, color, new Vector3(x, y, z));
            _actors[group].Add(new Actor
            {
                Entity = entity,
                BasePosition = new Vector3(x, y, z),
                BaseRotation = default,
                Scale = new Vector3(0.34f, 0.34f, 0.34f),
                Phase = i * 0.83f + (int)group * 1.17f,
                Speed = 0.42f + i * 0.035f + (int)group * 0.08f,
                Amplitude = 5.0f + (int)group * 1.5f,
                Motion = 5
            });
        }
    }

    private Entity MakeMesh(Preset group, string primitive, Color tint, Vector3 position, Vector3 rotation, Vector3 scale)
    {
        var key = $"{group}:{primitive}";
        Entity entity;
        if (!_meshPrototypes.TryGetValue(key, out var prototype))
        {
            entity = Scene.Create($"{group} {primitive} prototype");
            entity.Layer = $"Bench{group}";
            entity.Tag = "performance-renderable";
            var renderer = entity.EnsureMeshRenderer();
            renderer.Primitive = primitive;
            renderer.Tint = tint;
            _meshPrototypes[key] = entity;
        }
        else
        {
            entity = prototype.Clone() ?? throw new InvalidOperationException($"Could not clone {key} benchmark prototype.");
        }

        entity.Transform.Set(position, rotation, scale);
        _groups[group].Add(entity);
        return entity;
    }

    private Entity MakeLight(Preset group, Color color, Vector3 position)
    {
        Entity entity;
        if (!_lightPrototypes.TryGetValue(group, out var prototype))
        {
            entity = Scene.Create($"{group} moving point light");
            entity.Layer = $"Bench{group}";
            entity.Tag = "performance-light";
            var marker = entity.EnsureMeshRenderer();
            marker.Primitive = "sphere";
            marker.Tint = color;
            var light = entity.EnsurePointLight();
            light.Color = color;
            light.Intensity = 2.2f + (int)group * 0.55f;
            light.Radius = 14.0f + (int)group * 1.8f;
            _lightPrototypes[group] = entity;
        }
        else
        {
            entity = prototype.Clone() ?? throw new InvalidOperationException($"Could not clone {group} light prototype.");
        }

        entity.Transform.Set(position, default, new Vector3(0.34f, 0.34f, 0.34f));
        _groups[group].Add(entity);
        _lightCounts[group]++;
        return entity;
    }

    private void AnimateActiveWorkload(float deltaTime)
    {
        var t = (float)Time.ElapsedTime;
        var updateCount = 0;
        foreach (var group in Presets)
        {
            if ((int)group > (int)_preset) break;
            foreach (var actor in _actors[group])
            {
                var angle = t * actor.Speed + actor.Phase;
                var p = actor.BasePosition;
                var r = actor.BaseRotation;
                switch (actor.Motion)
                {
                    case 0:
                        p.Y += MathF.Sin(angle * 1.7f) * actor.Amplitude;
                        p.X += MathF.Sin(angle * 0.37f) * actor.Amplitude * 0.35f;
                        r.Y += angle;
                        r.X += MathF.Sin(angle * 0.6f) * 0.55f;
                        break;
                    case 1:
                        p.X += MathF.Cos(angle) * actor.Amplitude;
                        p.Z += MathF.Sin(angle) * actor.Amplitude;
                        p.Y += MathF.Sin(angle * 1.9f) * actor.Amplitude * 0.32f;
                        r.Y -= angle * 1.4f;
                        r.Z += angle * 0.7f;
                        break;
                    case 2:
                        p.X += MathF.Sin(angle * 1.25f) * actor.Amplitude;
                        p.Y += MathF.Cos(angle * 0.72f) * actor.Amplitude * 0.75f;
                        r.X += angle;
                        r.Y += angle * 0.35f;
                        break;
                    case 3:
                        p.Z += MathF.Sin(angle * 1.1f) * actor.Amplitude;
                        p.Y += MathF.Sin(angle * 2.15f) * actor.Amplitude * 0.50f;
                        p.X += MathF.Cos(angle * 0.53f) * actor.Amplitude * 0.55f;
                        r.X -= angle * 0.65f;
                        r.Z += angle;
                        break;
                    case 4: // overhead architecture rotor
                        r.Z = angle;
                        r.Y = MathF.Sin(angle * 0.4f) * 0.25f;
                        break;
                    case 5: // moving light marker + actual point light
                        p.X += MathF.Cos(angle) * actor.Amplitude;
                        p.Y += MathF.Sin(angle * 1.35f) * actor.Amplitude * 0.60f;
                        p.Z += MathF.Sin(angle * 0.55f) * actor.Amplitude * 0.75f;
                        r.Y = angle;
                        break;
                }
                if (updateCount >= _transformUpdates.Length)
                    Array.Resize(ref _transformUpdates, _transformUpdates.Length * 2);
                _transformUpdates[updateCount++].Set(actor.Entity, p, r, actor.Scale);
            }
        }
        if (updateCount > 0) Transform.SetBatch(_transformUpdates, updateCount);
    }

    private void SetPreset(Preset preset, bool initial = false)
    {
        _preset = preset;
        foreach (var group in Presets)
        {
            var enabled = (int)group <= (int)preset;
            foreach (var entity in _groups[group]) entity.Enabled = enabled;
            if (_buttons.TryGetValue(group, out var button)) button.SetClass("selected", group == preset);
        }

        _manualTourClock = 0.0f;
        _presetName.Text = preset.ToString().ToUpperInvariant();
        _benchmarkState.Text = _benchmarkRunning
            ? $"BENCHMARK · {preset.ToString().ToUpperInvariant()}"
            : $"LIVE · {preset.ToString().ToUpperInvariant()} FLYTHROUGH";
        RefreshLoadCounts();
        if (!initial) ResetLiveStats();
    }

    private void RefreshLoadCounts()
    {
        var objects = 0;
        var movers = 0;
        var lights = 0;
        foreach (var group in Presets)
        {
            if ((int)group > (int)_preset) break;
            objects += _groups[group].Count;
            movers += _actors[group].Count;
            lights += _lightCounts[group];
        }
        _objectCount.Text = objects.ToString("N0");
        _moverCount.Text = movers.ToString("N0");
        _lightCount.Text = lights.ToString();
    }

    private void UpdateShowcaseCamera(float deltaTime)
    {
        float local;
        if (_benchmarkRunning)
        {
            local = Math.Clamp(_benchmarkPhaseClock / PhaseSeconds, 0.0f, 0.9999f);
        }
        else
        {
            local = (_manualTourClock % ManualTourSeconds) / ManualTourSeconds;
        }

        var route = RouteFor(_preset);
        var scaled = local * route.Length;
        var segment = Math.Clamp((int)MathF.Floor(scaled), 0, route.Length - 1);
        var next = (segment + 1) % route.Length;
        var blend = SmoothStep(scaled - MathF.Floor(scaled));
        var position = Lerp(route[segment].Position, route[next].Position, blend);
        var lookAt = Lerp(route[segment].LookAt, route[next].LookAt, blend);
        var fov = Lerp(route[segment].Fov, route[next].Fov, blend);

        var dx = lookAt.X - position.X;
        var dy = lookAt.Y - position.Y;
        var dz = lookAt.Z - position.Z;
        var horizontal = MathF.Sqrt(dx * dx + dz * dz);
        var yaw = MathF.Atan2(dx, dz);
        var pitch = MathF.Atan2(dy, MathF.Max(horizontal, 0.0001f));
        Camera.State = new CameraState(position, yaw, pitch, fov, 0.04f, 650.0f);
    }

    private static TourPoint[] RouteFor(Preset preset) => preset switch
    {
        Preset.Baseline => BaselineRoute,
        Preset.Gameplay => GameplayRoute,
        Preset.Heavy => HeavyRoute,
        _ => TortureRoute
    };

    private void ResetLiveStats()
    {
        _liveFrames.Clear();
        _liveFps.Text = "---";
        _frameMs.Text = "-- ms";
        _avgFps.Text = "---";
        _oneLow.Text = "---";
        _worstMs.Text = "-- ms";
        _updateMs.Text = "-- ms";
        _renderMs.Text = "-- ms";
        _drawCalls.Text = "---";
        _visibleMeshes.Text = "---";
        _culledMeshes.Text = "---";
    }

    private void RefreshLiveUi()
    {
        if (_liveFrames.Count == 0) return;
        var last = _liveFrames[^1];
        var averageMs = _liveFrames.Average();
        var worst = _liveFrames.Max();
        var p99 = PercentileFrameMs(_liveFrames, 0.99f);
        _liveFps.Text = Fps(last).ToString("0");
        _frameMs.Text = $"{last:0.00} ms";
        _avgFps.Text = Fps((float)averageMs).ToString("0");
        _oneLow.Text = Fps(p99).ToString("0");
        _worstMs.Text = $"{worst:0.00} ms";
        if (_hasPerformance)
        {
            _updateMs.Text = $"{_latestPerformance.UpdateMs:0.00} ms";
            _renderMs.Text = $"{_latestPerformance.RenderMs:0.00} ms";
            _drawCalls.Text = _latestPerformance.TotalDrawCalls.ToString("N0");
            _visibleMeshes.Text = _latestPerformance.MeshVisible.ToString("N0");
            _culledMeshes.Text = _latestPerformance.MeshCulled.ToString("N0");
        }
    }

    private void BeginBenchmark()
    {
        _benchmarkRunning = true;
        _benchmarkPhase = 0;
        _results.Clear();
        _runBenchmark.Text = "Benchmark Running";
        _runBenchmark.Interactable = false;
        StartBenchmarkPhase();
        Log.Info("Performance Lab benchmark started: cinematic Baseline -> Gameplay -> Heavy -> Torture fly-through.");
    }

    private void StartBenchmarkPhase()
    {
        var preset = (Preset)_benchmarkPhase;
        SetPreset(preset);
        _warmupRemaining = WarmupSeconds;
        _sampleRemaining = SampleSeconds;
        _benchmarkPhaseClock = 0.0f;
        _phaseFrames.Clear();
        _phaseUpdateMsTotal = 0.0;
        _phaseRenderMsTotal = 0.0;
        _phaseDrawCallsTotal = 0.0;
        _phaseVisibleMeshesTotal = 0.0;
        _phaseCulledMeshesTotal = 0.0;
        _phaseTelemetrySamples = 0;
        _benchmarkState.Text = $"BENCHMARK · {preset.ToString().ToUpperInvariant()} · WARMUP";
        UpdateProgress(0.0f);
    }

    private void TickBenchmark(float deltaTime)
    {
        var dt = Math.Max(0.0f, deltaTime);
        _benchmarkPhaseClock += dt;
        if (_warmupRemaining > 0.0f)
        {
            _warmupRemaining -= dt;
            var warmupProgress = 1.0f - Math.Clamp(_warmupRemaining / WarmupSeconds, 0.0f, 1.0f);
            UpdateProgress(warmupProgress * 0.15f);
            if (_warmupRemaining <= 0.0f)
            {
                _phaseFrames.Clear();
                _benchmarkState.Text = $"BENCHMARK · {_preset.ToString().ToUpperInvariant()} · SAMPLING";
            }
            return;
        }

        _sampleRemaining -= dt;
        var sampleProgress = 1.0f - Math.Clamp(_sampleRemaining / SampleSeconds, 0.0f, 1.0f);
        UpdateProgress(0.15f + sampleProgress * 0.85f);
        if (_sampleRemaining > 0.0f) return;

        CompleteBenchmarkPhase();
        _benchmarkPhase++;
        if (_benchmarkPhase >= 4) FinishBenchmark();
        else StartBenchmarkPhase();
    }

    private void CompleteBenchmarkPhase()
    {
        if (_phaseFrames.Count == 0)
        {
            _results.Add(new Result(_preset, 0, 0, 0, 0, 0, 0, 0, 0, 0));
            return;
        }
        var averageMs = (float)_phaseFrames.Average();
        var p99 = PercentileFrameMs(_phaseFrames, 0.99f);
        var worst = _phaseFrames.Max();
        var samples = Math.Max(1, _phaseTelemetrySamples);
        _results.Add(new Result(
            _preset, _phaseFrames.Count, Fps(averageMs), Fps(p99), worst,
            (float)(_phaseUpdateMsTotal / samples),
            (float)(_phaseRenderMsTotal / samples),
            (float)(_phaseDrawCallsTotal / samples),
            (float)(_phaseVisibleMeshesTotal / samples),
            (float)(_phaseCulledMeshesTotal / samples)));
    }

    private void FinishBenchmark()
    {
        _benchmarkRunning = false;
        _runBenchmark.Text = "Run Full Benchmark";
        _runBenchmark.Interactable = true;
        _benchmarkState.Text = "BENCHMARK COMPLETE";
        UpdateProgress(1.0f);
        var summary = string.Join("  |  ", _results.Select(r => $"{r.Preset}: {r.AverageFps:0} avg / {r.OnePercentLow:0} 1% low / {r.WorstMs:0.0}ms worst"));
        _resultSummary.Text = summary;
        Log.Info("=== VESPERA PERFORMANCE LAB RESULT ===");
        foreach (var result in _results)
            Log.Info($"{result.Preset,-9} frames={result.Frames} avg={result.AverageFps:0.0} FPS 1%low={result.OnePercentLow:0.0} FPS worst={result.WorstMs:0.00} ms update={result.AverageUpdateMs:0.00} ms render={result.AverageRenderMs:0.00} ms draws={result.AverageDrawCalls:0} visible={result.AverageVisibleMeshes:0} culled={result.AverageCulledMeshes:0}");
        ExportBenchmarkResults();
        Log.Info("=== END PERFORMANCE LAB RESULT ===");
    }

    private void ExportBenchmarkResults()
    {
        try
        {
            var directory = Path.Combine(Environment.CurrentDirectory, "benchmark-results");
            Directory.CreateDirectory(directory);
            var stamp = DateTime.UtcNow.ToString("yyyyMMdd-HHmmss", CultureInfo.InvariantCulture);
            var jsonPath = Path.Combine(directory, $"vespera-performance-{stamp}.json");
            var csvPath = Path.Combine(directory, $"vespera-performance-{stamp}.csv");
            var payload = new
            {
                engine = "Vespera Engine 1.0.0",
                project = "Vespera Performance Lab",
                utc = DateTime.UtcNow.ToString("O", CultureInfo.InvariantCulture),
                vsync = false,
                workload = new { objects = TotalObjects(), movers = TotalMovers(), lights = TotalLights() },
                results = _results
            };
            File.WriteAllText(jsonPath, JsonSerializer.Serialize(payload, new JsonSerializerOptions { WriteIndented = true }));

            var csv = new StringBuilder();
            csv.AppendLine("preset,frames,avg_fps,one_percent_low_fps,worst_ms,avg_update_ms,avg_render_ms,avg_draw_calls,avg_visible_meshes,avg_culled_meshes");
            foreach (var result in _results)
            {
                csv.Append(result.Preset).Append(',').Append(result.Frames).Append(',')
                    .Append(result.AverageFps.ToString("0.00", CultureInfo.InvariantCulture)).Append(',')
                    .Append(result.OnePercentLow.ToString("0.00", CultureInfo.InvariantCulture)).Append(',')
                    .Append(result.WorstMs.ToString("0.000", CultureInfo.InvariantCulture)).Append(',')
                    .Append(result.AverageUpdateMs.ToString("0.000", CultureInfo.InvariantCulture)).Append(',')
                    .Append(result.AverageRenderMs.ToString("0.000", CultureInfo.InvariantCulture)).Append(',')
                    .Append(result.AverageDrawCalls.ToString("0.0", CultureInfo.InvariantCulture)).Append(',')
                    .Append(result.AverageVisibleMeshes.ToString("0.0", CultureInfo.InvariantCulture)).Append(',')
                    .Append(result.AverageCulledMeshes.ToString("0.0", CultureInfo.InvariantCulture)).AppendLine();
            }
            File.WriteAllText(csvPath, csv.ToString());
            Log.Info($"Performance Lab results exported: {jsonPath}");
            Log.Info($"Performance Lab CSV exported: {csvPath}");
        }
        catch (Exception ex)
        {
            Log.Warning($"Performance Lab could not export benchmark results: {ex.Message}");
        }
    }

    private int TotalObjects() => _groups.Values.Sum(x => x.Count);
    private int TotalMovers() => _actors.Values.Sum(x => x.Count);
    private int TotalLights() => _lightCounts.Values.Sum();

    private static Color PresetColor(Preset preset) => preset switch
    {
        Preset.Baseline => BaselineColor,
        Preset.Gameplay => GameplayColor,
        Preset.Heavy => HeavyColor,
        _ => TortureColor
    };

    private void UpdateProgress(float value) => _progressFill.SetProperty("width", $"{Math.Clamp(value, 0.0f, 1.0f) * 100.0f:0.0}%");
    private static float Fps(float frameMs) => frameMs <= 0.0001f ? 0.0f : 1000.0f / frameMs;
    private static float SmoothStep(float t) => t * t * (3.0f - 2.0f * t);
    private static float Lerp(float a, float b, float t) => a + (b - a) * t;
    private static Vector3 Lerp(Vector3 a, Vector3 b, float t) => new(Lerp(a.X, b.X, t), Lerp(a.Y, b.Y, t), Lerp(a.Z, b.Z, t));

    private static float PercentileFrameMs(List<float> frames, float percentile)
    {
        if (frames.Count == 0) return 0.0f;
        var sorted = frames.OrderBy(x => x).ToArray();
        var index = Math.Clamp((int)MathF.Ceiling((sorted.Length - 1) * percentile), 0, sorted.Length - 1);
        return sorted[index];
    }
}
