using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Globalization;
using System.Text;
using System.Text.Json;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;

namespace Vespera
{
    public struct Vector2
    {
        public float X;
        public float Y;
        public Vector2(float x, float y) => (X, Y) = (x, y);
    }

    public struct Vector3
    {
        public float X;
        public float Y;
        public float Z;
        public Vector3(float x, float y, float z) => (X, Y, Z) = (x, y, z);
    }

    public struct Color
    {
        public float R;
        public float G;
        public float B;
        public float A;
        public Color(float r, float g, float b, float a = 1.0f) => (R, G, B, A) = (r, g, b, a);
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct TransformUpdate
    {
        public ulong EntityId;
        public float Px, Py, Pz;
        public float Rx, Ry, Rz;
        public float Sx, Sy, Sz;

        public TransformUpdate(Entity entity, Vector3 position, Vector3 rotation, Vector3 scale)
        {
            EntityId = entity?.Id ?? 0;
            (Px, Py, Pz) = (position.X, position.Y, position.Z);
            (Rx, Ry, Rz) = (rotation.X, rotation.Y, rotation.Z);
            (Sx, Sy, Sz) = (scale.X, scale.Y, scale.Z);
        }

        public void Set(Entity entity, Vector3 position, Vector3 rotation, Vector3 scale)
        {
            EntityId = entity?.Id ?? 0;
            (Px, Py, Pz) = (position.X, position.Y, position.Z);
            (Rx, Ry, Rz) = (rotation.X, rotation.Y, rotation.Z);
            (Sx, Sy, Sz) = (scale.X, scale.Y, scale.Z);
        }
    }

    // Transform is a managed view over the authoritative native Transform. It stores only
    // the stable Entity id; every read/write crosses the semantic native API boundary.
    public sealed class Transform
    {
        private readonly ulong _entityId;
        internal Transform(ulong entityId) => _entityId = entityId;

        public Vector3 Position
        {
            get => Managed.Native.GetTransform(_entityId).Position;
            set
            {
                var t = Managed.Native.GetTransform(_entityId);
                t.Position = value;
                Managed.Native.SetTransform(_entityId, t);
            }
        }

        public Vector3 Rotation
        {
            get => Managed.Native.GetTransform(_entityId).Rotation;
            set
            {
                var t = Managed.Native.GetTransform(_entityId);
                t.Rotation = value;
                Managed.Native.SetTransform(_entityId, t);
            }
        }

        public Vector3 Scale
        {
            get => Managed.Native.GetTransform(_entityId).Scale;
            set
            {
                var t = Managed.Native.GetTransform(_entityId);
                t.Scale = value;
                Managed.Native.SetTransform(_entityId, t);
            }
        }

        // Set a complete transform in one native call. Useful for animation-heavy
        // gameplay where separate Position/Rotation/Scale setters would create
        // unnecessary bridge traffic.
        public void Set(Vector3 position, Vector3 rotation, Vector3 scale)
        {
            Managed.Native.SetTransform(_entityId, new Managed.NativeTransform
            {
                Position = position,
                Rotation = rotation,
                Scale = scale
            });
        }

        // Applies a preallocated transform command array through one managed/native
        // transition. Callers may reuse the same array every frame and pass the
        // active prefix count to avoid per-frame allocations.
        public static int SetBatch(TransformUpdate[] updates, int count = -1)
            => Managed.Native.SetTransforms(updates, count);
    }

    public readonly struct PerformanceSnapshot
    {
        public ulong FrameIndex { get; init; }
        public double FrameMs { get; init; }
        public double SmoothedFrameMs { get; init; }
        public double MaxFrameMs { get; init; }
        public double UpdateMs { get; init; }
        public double RenderMs { get; init; }
        public ulong ScenePasses { get; init; }
        public ulong WorldDrawCalls { get; init; }
        public ulong MeshConsidered { get; init; }
        public ulong MeshVisible { get; init; }
        public ulong MeshCulled { get; init; }
        public ulong MeshDrawCalls { get; init; }
        public ulong SpriteConsidered { get; init; }
        public ulong SpriteVisible { get; init; }
        public ulong SpriteCulled { get; init; }
        public ulong SpriteDrawCalls { get; init; }
        public ulong LightsConsidered { get; init; }
        public ulong LightsVisible { get; init; }
        public ulong LightsUploaded { get; init; }
        public ulong UiDrawCalls { get; init; }
        public ulong TotalDrawCalls { get; init; }
    }

    public static class Performance
    {
        public static bool TryGetSnapshot(out PerformanceSnapshot snapshot)
            => Managed.Native.TryGetPerformance(out snapshot);

        public static PerformanceSnapshot Snapshot
            => TryGetSnapshot(out var snapshot) ? snapshot : default;
    }

    // Typed convenience wrapper over the same semantic Sprite Renderer properties
    // used by the editor/component API. It adds animation-state ergonomics without
    // creating a managed-only renderer model.
    public sealed class SpriteRenderer
    {
        private const string ComponentKey = "sectorline.sprite_renderer";
        private readonly Entity _entity;
        internal SpriteRenderer(Entity entity) => _entity = entity;

        public Vector2 Size { get => _entity.GetVector2(ComponentKey, "size", new Vector2(1, 1)); set => _entity.SetVector2(ComponentKey, "size", value); }
        public string AnimationClip { get => _entity.GetString(ComponentKey, "animation_clip"); set => _entity.SetString(ComponentKey, "animation_clip", value); }
        public float Speed { get => _entity.GetFloat(ComponentKey, "animation_speed", 1.0f); set => _entity.SetFloat(ComponentKey, "animation_speed", Math.Max(0.0f, value)); }
        public float TimeOffset { get => _entity.GetFloat(ComponentKey, "animation_time_offset"); set => _entity.SetFloat(ComponentKey, "animation_time_offset", value); }
        public bool Paused { get => _entity.GetBool(ComponentKey, "animation_paused"); set => _entity.SetBool(ComponentKey, "animation_paused", value); }
        public Color Tint { get => _entity.GetColor(ComponentKey, "color", new Color(1, 1, 1, 1)); set => _entity.SetColor(ComponentKey, "color", value); }

        public void Play(string clip, float speed = 1.0f, bool restart = true)
        {
            AnimationClip = clip;
            Speed = speed;
            if (restart) TimeOffset = -(float)Time.ElapsedTime;
            Paused = false;
        }
        public void Pause() => Paused = true;
        public void Resume() => Paused = false;
        public void Restart() { TimeOffset = -(float)Time.ElapsedTime; Paused = false; }
        public void Stop() { Paused = true; TimeOffset = -(float)Time.ElapsedTime; }
    }

    // Typed wrappers for the remaining core built-ins. They deliberately use
    // the semantic property API so C# never depends on native component layout.
    public sealed class MeshRenderer
    {
        private const string ComponentKey = "sectorline.mesh_renderer";
        private readonly Entity _entity;
        internal MeshRenderer(Entity entity) => _entity = entity;
        public string Primitive { get => _entity.GetString(ComponentKey, "primitive", "cube"); set => _entity.SetString(ComponentKey, "primitive", value); }
        public Color Tint { get => _entity.GetColor(ComponentKey, "color", new Color(0.72f, 0.74f, 0.78f, 1.0f)); set => _entity.SetColor(ComponentKey, "color", value); }
        public AssetReference Material
        {
            get => new(_entity.GetString(ComponentKey, "material_asset_id"), _entity.GetString(ComponentKey, "material_path"));
            set
            {
                _entity.SetString(ComponentKey, "material_asset_id", value.Id);
                _entity.SetString(ComponentKey, "material_path", value.FallbackPath);
            }
        }
        public void SetMaterial(string projectRelativePath) => Material = Assets.FromPath(projectRelativePath);
        public void ClearMaterial() => Material = new AssetReference(string.Empty, string.Empty);
    }

    public sealed class CylinderCollider
    {
        private const string ComponentKey = "sectorline.cylinder_collider";
        private readonly Entity _entity;
        internal CylinderCollider(Entity entity) => _entity = entity;

        public float Radius { get => _entity.GetFloat(ComponentKey, "radius", 0.5f); set => _entity.SetFloat(ComponentKey, "radius", Math.Max(0.001f, value)); }
        public float Height { get => _entity.GetFloat(ComponentKey, "height", 1.8f); set => _entity.SetFloat(ComponentKey, "height", Math.Max(0.001f, value)); }
        public Vector3 Center { get => _entity.GetVector3(ComponentKey, "center"); set => _entity.SetVector3(ComponentKey, "center", value); }
        public bool IsTrigger { get => _entity.GetBool(ComponentKey, "is_trigger"); set => _entity.SetBool(ComponentKey, "is_trigger", value); }
        public Vector3 WorldCenter => new(_entity.Position.X + Center.X, _entity.Position.Y + Center.Y, _entity.Position.Z + Center.Z);

        public Entity[] Overlaps(bool includeTriggers = false) => Physics.OverlapCircle2D(WorldCenter, Radius, includeTriggers, _entity);
        public Vector3 ResolveMoveTo(Vector3 candidate) => Physics.ResolveCircleMotion2D(_entity.Position, candidate, Radius, _entity);
        public Vector3 MoveTo(Vector3 candidate)
        {
            var resolved = ResolveMoveTo(candidate);
            _entity.Position = resolved;
            return resolved;
        }
        public Vector3 MoveBy(Vector3 delta) => MoveTo(new Vector3(
            _entity.Position.X + delta.X,
            _entity.Position.Y + delta.Y,
            _entity.Position.Z + delta.Z));
    }

    public sealed class PointLight
    {
        private const string ComponentKey = "sectorline.point_light";
        private readonly Entity _entity;
        internal PointLight(Entity entity) => _entity = entity;

        public Color Color { get => _entity.GetColor(ComponentKey, "color", new Color(1.0f, 0.82f, 0.62f, 1.0f)); set => _entity.SetColor(ComponentKey, "color", value); }
        public float Intensity { get => _entity.GetFloat(ComponentKey, "intensity", 1.0f); set => _entity.SetFloat(ComponentKey, "intensity", Math.Max(0.0f, value)); }
        public float Radius { get => _entity.GetFloat(ComponentKey, "radius", 5.0f); set => _entity.SetFloat(ComponentKey, "radius", Math.Max(0.001f, value)); }
    }

    // Managed Entity is a lightweight reference handle around the stable native id.
    public sealed class Entity
    {
        public ulong Id { get; }
        public Transform Transform { get; }
        internal Entity(ulong id)
        {
            Id = id;
            Transform = new Transform(id);
        }

        public bool Exists => Managed.Native.EntityExists(Id);
        // Convenience aliases remain for source compatibility and terse scripts.
        public Vector3 Position { get => Transform.Position; set => Transform.Position = value; }
        public Vector3 Rotation { get => Transform.Rotation; set => Transform.Rotation = value; }
        public Vector3 Scale { get => Transform.Scale; set => Transform.Scale = value; }

        public bool Enabled
        {
            get => Managed.Native.GetEnabled(Id);
            set => Managed.Native.SetEnabled(Id, value);
        }

        public string Name { get => Managed.Native.GetEntityName(Id); set => Managed.Native.SetEntityName(Id, value); }
        public string Tag { get => Managed.Native.GetEntityTag(Id); set => Managed.Native.SetEntityTag(Id, value); }
        public string Layer { get => Managed.Native.GetEntityLayer(Id); set => Managed.Native.SetEntityLayer(Id, value); }

        public SpriteRenderer? SpriteRenderer => HasComponent("sectorline.sprite_renderer") ? new SpriteRenderer(this) : null;
        public MeshRenderer? MeshRenderer => HasComponent("sectorline.mesh_renderer") ? new MeshRenderer(this) : null;
        public CylinderCollider? CylinderCollider => HasComponent("sectorline.cylinder_collider") ? new CylinderCollider(this) : null;
        public PointLight? PointLight => HasComponent("sectorline.point_light") ? new PointLight(this) : null;

        public MeshRenderer EnsureMeshRenderer()
        {
            if (!HasComponent("sectorline.mesh_renderer")) AddComponent("sectorline.mesh_renderer");
            return new MeshRenderer(this);
        }
        public CylinderCollider EnsureCylinderCollider()
        {
            if (!HasComponent("sectorline.cylinder_collider")) AddComponent("sectorline.cylinder_collider");
            return new CylinderCollider(this);
        }
        public PointLight EnsurePointLight()
        {
            if (!HasComponent("sectorline.point_light")) AddComponent("sectorline.point_light");
            return new PointLight(this);
        }

        public bool HasComponent(string componentKey) => Managed.Native.HasComponent(Id, componentKey);
        public bool AddComponent(string componentKey) => Managed.Native.AddComponent(Id, componentKey);
        public bool RemoveComponent(string componentKey) => Managed.Native.RemoveComponent(Id, componentKey);

        public bool HasScript<T>() where T : Component => Managed.Native.HasManagedScript(Id, typeof(T).FullName ?? typeof(T).Name);
        public bool AddScript<T>() where T : Component => Managed.Native.AddManagedScript(Id, typeof(T).FullName ?? typeof(T).Name);
        public bool RemoveScript<T>() where T : Component => Managed.Native.RemoveManagedScript(Id, typeof(T).FullName ?? typeof(T).Name);

        // Semantic built-in property access is the same reflected surface used by
        // editor tooling. It is intentionally keyed instead of exposing native layout.
        public float GetFloat(string componentKey, string propertyKey, float fallback = 0.0f) =>
            Managed.Native.GetPropertyFloat(Id, componentKey, propertyKey, fallback);
        public bool SetFloat(string componentKey, string propertyKey, float value) =>
            Managed.Native.SetPropertyFloat(Id, componentKey, propertyKey, value);
        public bool GetBool(string componentKey, string propertyKey, bool fallback = false) =>
            Managed.Native.GetPropertyBool(Id, componentKey, propertyKey, fallback);
        public bool SetBool(string componentKey, string propertyKey, bool value) =>
            Managed.Native.SetPropertyBool(Id, componentKey, propertyKey, value);
        public string GetString(string componentKey, string propertyKey, string fallback = "") =>
            Managed.Native.GetPropertyText(Id, componentKey, propertyKey, fallback);
        public bool SetString(string componentKey, string propertyKey, string value) =>
            Managed.Native.SetPropertyText(Id, componentKey, propertyKey, value);
        public Vector2 GetVector2(string componentKey, string propertyKey, Vector2 fallback = default) =>
            Managed.Native.GetPropertyVector2(Id, componentKey, propertyKey, fallback);
        public bool SetVector2(string componentKey, string propertyKey, Vector2 value) =>
            Managed.Native.SetPropertyVector2(Id, componentKey, propertyKey, value);
        public Vector3 GetVector3(string componentKey, string propertyKey, Vector3 fallback = default) =>
            Managed.Native.GetPropertyVector3(Id, componentKey, propertyKey, fallback);
        public bool SetVector3(string componentKey, string propertyKey, Vector3 value) =>
            Managed.Native.SetPropertyVector3(Id, componentKey, propertyKey, value);
        public Color GetColor(string componentKey, string propertyKey, Color fallback = default) =>
            Managed.Native.GetPropertyColor(Id, componentKey, propertyKey, fallback);
        public bool SetColor(string componentKey, string propertyKey, Color value) =>
            Managed.Native.SetPropertyColor(Id, componentKey, propertyKey, value);

        public Entity? Clone(string? name = null)
        {
            var id = Managed.Native.CloneEntity(Id, name ?? string.Empty);
            return id == 0 ? null : new Entity(id);
        }

        public bool Destroy() => Managed.Native.DestroyEntity(Id);
    }

    public static class Scene
    {
        public static Entity? Find(string name)
        {
            var id = Managed.Native.FindEntityByName(name);
            return id == 0 ? null : new Entity(id);
        }

        public static Entity? FindWithTag(string tag)
        {
            var id = Managed.Native.FindEntityWithTag(tag);
            return id == 0 ? null : new Entity(id);
        }

        public static Entity[] All => Managed.Native.QueryEntities(0, string.Empty);
        public static Entity[] FindAllWithTag(string tag) => Managed.Native.QueryEntities(1, tag);
        public static Entity[] FindAllOnLayer(string layer) => Managed.Native.QueryEntities(2, layer);
        public static Entity[] FindAllWithComponent(string componentKey) => Managed.Native.QueryEntities(3, componentKey);

        public static Entity Create(string name = "Entity")
        {
            var id = Managed.Native.CreateEntity(name);
            if (id == 0) throw new InvalidOperationException("Native scene could not create an Entity.");
            return new Entity(id);
        }

        public static string CurrentPath => Managed.Native.CurrentScenePath();

        // Scene loads are requested and committed by the native game owner after the
        // current managed Update() returns, so a script never destroys its own CLR
        // context while it is still executing.
        public static bool Load(string path) => Managed.Native.RequestSceneLoad(path);
        // Same-scene Scene.Load(Current) is coalesced by the native host to prevent
        // accidental per-frame reload loops. Use Reload() when a same-scene reload
        // is intentional.
        public static bool Reload() => Managed.Native.RequestSceneLoad("@vespera/reload-current");
        public static bool Load(AssetReference scene)
        {
            var path = Assets.Resolve(scene);
            return !string.IsNullOrWhiteSpace(path) && Load(path);
        }
    }

    public struct CameraState
    {
        public Vector3 Position;
        public float Yaw;
        public float Pitch;
        public float VerticalFovDegrees;
        public float NearPlane;
        public float FarPlane;

        public CameraState(Vector3 position, float yaw, float pitch, float verticalFovDegrees = 75.0f, float nearPlane = 0.05f, float farPlane = 500.0f)
        {
            Position = position;
            Yaw = yaw;
            Pitch = pitch;
            VerticalFovDegrees = verticalFovDegrees;
            NearPlane = nearPlane;
            FarPlane = farPlane;
        }
    }

    // Public game-camera access for cinematics, scripted cameras and camera rigs.
    // Values use the same world-space/radian conventions as Scene Text.
    public static class Camera
    {
        public static CameraState State
        {
            get => Managed.Native.GetCamera();
            set => Managed.Native.SetCamera(value);
        }

        public static Vector3 Position
        {
            get => State.Position;
            set { var state = State; state.Position = value; State = state; }
        }

        public static float Yaw
        {
            get => State.Yaw;
            set { var state = State; state.Yaw = value; State = state; }
        }

        public static float Pitch
        {
            get => State.Pitch;
            set { var state = State; state.Pitch = value; State = state; }
        }

        public static float VerticalFovDegrees
        {
            get => State.VerticalFovDegrees;
            set { var state = State; state.VerticalFovDegrees = value; State = state; }
        }

        public static void SetPose(Vector3 position, float yaw, float pitch)
        {
            var state = State;
            state.Position = position;
            state.Yaw = yaw;
            state.Pitch = pitch;
            State = state;
        }
    }

    public readonly struct AssetReference
    {
        public string Id { get; }
        public string FallbackPath { get; }

        public AssetReference(string id, string fallbackPath = "")
        {
            Id = id ?? string.Empty;
            FallbackPath = fallbackPath ?? string.Empty;
        }

        public bool Exists => Assets.Exists(this);
        public string ResolvedPath => Assets.Resolve(this);
        public override string ToString() => string.IsNullOrWhiteSpace(FallbackPath) ? Id : FallbackPath;
    }

    public static class Assets
    {
        public static string Resolve(AssetReference asset) =>
            Managed.Native.ResolveAssetPath(asset.Id, asset.FallbackPath);

        public static bool Exists(AssetReference asset) => !string.IsNullOrWhiteSpace(Resolve(asset));

        public static AssetReference FromPath(string path)
        {
            var id = Managed.Native.FindAssetId(path ?? string.Empty);
            return new AssetReference(id, path ?? string.Empty);
        }
    }

    public sealed class AudioSource : IDisposable
    {
        public ulong Handle { get; private set; }
        public float Volume { get; private set; }
        public bool Loop { get; private set; }
        public bool Paused { get; private set; }
        public bool Spatial { get; private set; }
        public Vector3 Position { get; private set; }
        public float MinDistance { get; private set; } = 1.0f;
        public float MaxDistance { get; private set; } = 20.0f;
        public Entity? FollowTarget { get; private set; }
        public bool IsPlaying => Handle != 0 && Managed.Native.AudioVoicePlaying(Handle);

        internal AudioSource(ulong handle, float volume, bool loop)
        {
            Handle = handle; Volume = volume; Loop = loop;
            Audio.Register(this);
        }

        public bool SetVolume(float volume)
        {
            Volume = Math.Clamp(volume, 0.0f, 4.0f);
            return Handle != 0 && Managed.Native.AudioSetVoiceVolume(Handle, Volume);
        }
        public bool SetLoop(bool loop)
        {
            Loop = loop;
            return Handle != 0 && Managed.Native.AudioSetVoiceLoop(Handle, loop);
        }
        public bool SetPaused(bool paused)
        {
            Paused = paused;
            return Handle != 0 && Managed.Native.AudioSetVoicePaused(Handle, paused);
        }
        public bool Pause() => SetPaused(true);
        public bool Resume() => SetPaused(false);
        private bool ApplyPosition(Vector3 position, bool spatial, float minDistance, float maxDistance)
        {
            Position = position; Spatial = spatial; MinDistance = Math.Max(0.0f, minDistance);
            MaxDistance = Math.Max(MinDistance + 0.001f, maxDistance);
            return Handle != 0 && Managed.Native.AudioSetVoicePosition(Handle, Position, Spatial, MinDistance, MaxDistance);
        }
        public bool SetPosition(Vector3 position, bool spatial = true, float minDistance = 1.0f, float maxDistance = 20.0f)
        {
            FollowTarget = null;
            return ApplyPosition(position, spatial, minDistance, maxDistance);
        }
        public bool Follow(Entity entity, float minDistance = 1.0f, float maxDistance = 20.0f)
        {
            if (!entity.Exists) return false;
            FollowTarget = entity;
            return ApplyPosition(entity.Position, true, minDistance, maxDistance);
        }
        public void Unfollow() => FollowTarget = null;
        internal void TickFollow()
        {
            if (FollowTarget is null) return;
            if (!FollowTarget.Exists) { Stop(); return; }
            ApplyPosition(FollowTarget.Position, true, MinDistance, MaxDistance);
        }
        internal void MarkStopped() { Handle = 0; FollowTarget = null; }
        public bool Stop()
        {
            if (Handle == 0) { Audio.Unregister(this); return false; }
            var stopped = Managed.Native.AudioStopVoice(Handle);
            Handle = 0;
            FollowTarget = null;
            Audio.Unregister(this);
            return stopped;
        }
        public void Dispose() => Stop();
    }

    public static class AudioListener
    {
        public static Vector3 Position
        {
            get => Managed.Native.AudioListenerPosition();
            set => Managed.Native.AudioSetListenerPosition(value);
        }
        public static void UseCamera() => Managed.Native.AudioUseCameraListener();
    }

    public static class Audio
    {
        private static readonly HashSet<AudioSource> Sources = new();
        internal static void Register(AudioSource source) => Sources.Add(source);
        internal static void Unregister(AudioSource source) => Sources.Remove(source);
        internal static void UpdateManagedSources()
        {
            foreach (var source in Sources.ToArray())
            {
                if (!source.IsPlaying) { source.MarkStopped(); Sources.Remove(source); continue; }
                source.TickFollow();
            }
        }
        internal static void StopAllManagedSources()
        {
            foreach (var source in Sources.ToArray()) source.Stop();
            Sources.Clear();
        }

        public static float MasterVolume
        {
            get => Managed.Native.AudioMasterVolume();
            set => Managed.Native.SetAudioMasterVolume(value);
        }

        public static int ActiveVoiceCount => Managed.Native.AudioActiveVoiceCount();
        public static bool PlayOneShot(string wavPath, float volume = 1.0f) => Managed.Native.AudioPlayOneShot(wavPath, volume);
        public static bool PlayOneShot(AssetReference audio, float volume = 1.0f)
        {
            var path = Assets.Resolve(audio);
            return !string.IsNullOrWhiteSpace(path) && PlayOneShot(path, volume);
        }
        public static void StopAll()
        {
            Managed.Native.AudioStopAll();
            foreach (var source in Sources.ToArray()) source.MarkStopped();
            Sources.Clear();
        }

        public static AudioSource? Play(string wavPath, float volume = 1.0f, bool loop = false)
        {
            var handle = Managed.Native.AudioPlayVoice(wavPath, volume, loop);
            return handle == 0 ? null : new AudioSource(handle, volume, loop);
        }

        public static AudioSource? Play(AssetReference audio, float volume = 1.0f, bool loop = false)
        {
            var path = Assets.Resolve(audio);
            return string.IsNullOrWhiteSpace(path) ? null : Play(path, volume, loop);
        }

        public static AudioSource? PlaySpatial(string wavPath, Vector3 position, float volume = 1.0f,
            bool loop = false, float minDistance = 1.0f, float maxDistance = 20.0f)
        {
            var source = Play(wavPath, volume, loop);
            if (source is null) return null;
            if (!source.SetPosition(position, true, minDistance, maxDistance)) { source.Stop(); return null; }
            return source;
        }

        public static AudioSource? PlaySpatial(AssetReference audio, Vector3 position, float volume = 1.0f,
            bool loop = false, float minDistance = 1.0f, float maxDistance = 20.0f)
        {
            var path = Assets.Resolve(audio);
            return string.IsNullOrWhiteSpace(path) ? null : PlaySpatial(path, position, volume, loop, minDistance, maxDistance);
        }
    }

    public sealed class UiElement
    {
        public ulong Id { get; }
        internal UiElement(ulong id) => Id = id;
        public bool Exists => Id != 0 && Managed.Native.UiNodeExists(Id);

        public string Text
        {
            get => Managed.Native.UiGetNodeText(Id);
            set
            {
                if (!Managed.Native.UiSetNodeText(Id, value ?? string.Empty))
                    throw new InvalidOperationException($"UI node {Id} is not available.");
            }
        }

        public bool Enabled
        {
            get => Managed.Native.UiGetNodeEnabled(Id, out var enabled) && enabled;
            set
            {
                if (!Managed.Native.UiSetNodeEnabled(Id, value))
                    throw new InvalidOperationException($"UI node {Id} is not available.");
            }
        }

        // Friendly UI terminology over the same native enabled flag.
        public bool Visible { get => Enabled; set => Enabled = value; }

        public bool Interactable
        {
            get => Managed.Native.UiGetNodeBool(Id, 1, out var value) && value;
            set
            {
                if (!Managed.Native.UiSetNodeBool(Id, 1, value))
                    throw new InvalidOperationException($"UI node {Id} does not support Interactable.");
            }
        }

        public bool Focused
        {
            get => Managed.Native.UiGetNodeBool(Id, 2, out var value) && value;
            set
            {
                if (!Managed.Native.UiSetNodeBool(Id, 2, value))
                    throw new InvalidOperationException($"UI node {Id} cannot change focus.");
            }
        }

        public bool ReadOnly
        {
            get => Managed.Native.UiGetNodeBool(Id, 3, out var value) && value;
            set
            {
                if (!Managed.Native.UiSetNodeBool(Id, 3, value))
                    throw new InvalidOperationException($"UI node {Id} does not support ReadOnly.");
            }
        }

        public float Value
        {
            get => Managed.Native.UiGetNodeFloat(Id, 1, out var value) ? value : 0.0f;
            set
            {
                if (!Managed.Native.UiSetNodeFloat(Id, 1, value))
                    throw new InvalidOperationException($"UI node {Id} does not support Value.");
            }
        }

        // String-valued form controls (RML input/select/textarea and legacy
        // text inputs) use this semantic value channel. Text remains the
        // element's visible/inner text rather than being overloaded as form state.
        public string ValueText
        {
            get => Managed.Native.UiGetValue(Id);
            set
            {
                if (!Managed.Native.UiSetValue(Id, value ?? string.Empty))
                    throw new InvalidOperationException($"UI node {Id} does not support a string value.");
            }
        }

        // RmlUi styling remains behind Vespera's public UI abstraction. Legacy
        // .slui returns false for CSS-only operations instead of faking them.
        public bool SetProperty(string property, string value) => Managed.Native.UiSetProperty(Id, property, value);
        public bool SetClass(string className, bool enabled = true) => Managed.Native.UiSetClass(Id, className, enabled);

        // Clicks are queued by the native UI runtime and consumed exactly once.
        // This makes polling deterministic and is also a clean base for a later
        // event/delegate convenience layer without exposing native pointers.
        public bool Clicked => Managed.Native.UiConsumeClick(Id);

        public bool SetColor(Color color) => Managed.Native.UiSetNodeColor(Id, 1, color);
        public bool SetTextColor(Color color) => Managed.Native.UiSetNodeColor(Id, 2, color);
        public bool SetProgressFillColor(Color color) => Managed.Native.UiSetNodeColor(Id, 3, color);
        public bool SetProgressBackgroundColor(Color color) => Managed.Native.UiSetNodeColor(Id, 4, color);
        public bool SetImage(AssetReference image) => Managed.Native.UiSetNodeAsset(Id, 1, image);
        public bool SetFont(AssetReference font) => Managed.Native.UiSetNodeAsset(Id, 2, font);

        public override string ToString() => $"UI({Id})";
    }

    public static class UI
    {
        public static UiElement? Find(string name)
        {
            var id = Managed.Native.UiFindNode(name);
            return id == 0 ? null : new UiElement(id);
        }
        public static UiElement FindRequired(string name) => Find(name)
            ?? throw new InvalidOperationException($"UI node '{name}' was not found in the active UI document.");
        public static UiElement? FromId(ulong id) => Managed.Native.UiNodeExists(id) ? new UiElement(id) : null;
        public static bool Exists(ulong id) => Managed.Native.UiNodeExists(id);
    }

    public static class Input
    {
        public static float Value(string action) => Managed.Native.ActionValue(action);
        public static bool Down(string action) => Managed.Native.ActionDown(action);
        public static bool Pressed(string action) => Managed.Native.ActionPressed(action);
        public static bool Released(string action) => Managed.Native.ActionReleased(action);
    }

    public static class Time
    {
        public static float DeltaTime { get; private set; }
        public static double ElapsedTime { get; private set; }
        internal static void Advance(float deltaTime)
        {
            DeltaTime = Math.Max(0.0f, deltaTime);
            ElapsedTime += DeltaTime;
        }
        internal static void Reset()
        {
            DeltaTime = 0.0f;
            ElapsedTime = 0.0;
        }
    }

    public sealed class GameTimer
    {
        public float Duration { get; set; }
        public float Elapsed { get; private set; }
        public bool Repeat { get; set; }
        public bool Running { get; private set; } = true;
        public bool Finished => !Repeat && !Running && Elapsed >= Duration;
        public float Remaining => Math.Max(0.0f, Duration - Elapsed);
        public float Progress => Duration <= 0.0f ? 1.0f : Math.Clamp(Elapsed / Duration, 0.0f, 1.0f);

        public GameTimer(float durationSeconds, bool repeat = false)
        {
            Duration = Math.Max(0.0f, durationSeconds);
            Repeat = repeat;
        }

        public bool Tick(float deltaTime)
        {
            if (!Running) return false;
            Elapsed += Math.Max(0.0f, deltaTime);
            if (Elapsed < Duration) return false;
            if (Repeat && Duration > 0.0f) Elapsed %= Duration;
            else { Elapsed = Duration; Running = false; }
            return true;
        }

        public void Reset(bool start = true) { Elapsed = 0.0f; Running = start; }
        public void Restart() => Reset(true);
        public void Start() => Running = true;
        public void Stop() => Running = false;
    }

    public static class Prefab
    {
        public static Entity? Instantiate(string path, Vector3 position, string name = "")
        {
            var id = Managed.Native.InstantiatePrefab(path, name, position);
            return id == 0 ? null : new Entity(id);
        }

        public static Entity? Instantiate(AssetReference prefab, Vector3 position, string name = "")
        {
            var path = Assets.Resolve(prefab);
            return string.IsNullOrWhiteSpace(path) ? null : Instantiate(path, position, name);
        }
    }

    public enum RaycastHitType2D
    {
        SectorWall,
        EntityCollider,
    }

    public readonly struct RaycastHit2D
    {
        public RaycastHitType2D Type { get; init; }
        public float Distance { get; init; }
        public Vector3 Position { get; init; }
        public Vector3 Normal { get; init; }
        public Entity? Entity { get; init; }
        public ulong SectorIndex { get; init; }
        public ulong SideIndex { get; init; }
    }

    public static class Physics
    {
        public static bool Raycast2D(Vector3 origin, Vector3 direction, out RaycastHit2D hit,
            float maxDistance = 100.0f, bool includeTriggers = false, Entity? ignore = null)
            => Managed.Native.Raycast2D(origin, direction, maxDistance, includeTriggers, ignore?.Id ?? 0, out hit);

        public static Entity[] OverlapCircle2D(Vector3 position, float radius,
            bool includeTriggers = false, Entity? ignore = null)
            => Managed.Native.OverlapCircle2D(position, radius, includeTriggers, ignore?.Id ?? 0);

        public static Vector3 ResolveCircleMotion2D(Vector3 from, Vector3 candidate, float radius, Entity? ignore = null)
            => Managed.Native.ResolveCircleMotion2D(from, candidate, radius, ignore?.Id ?? 0);
    }

    // Small engine-level key/value save surface for 0.6.x gameplay dogfooding.
    // Values are semantic strings in JSON so the format stays human-readable and
    // independent of live CLR object graphs.
    public static class SaveData
    {
        private static readonly Dictionary<string, string> Values = new(StringComparer.Ordinal);
        public static string Slot { get; private set; } = "default";
        public static string SaveDirectory { get; set; } = Path.Combine("runtime", "saves");

        private static string SlotPath => Path.Combine(SaveDirectory, $"{Slot}.json");

        public static void UseSlot(string slot)
        {
            var requested = string.IsNullOrWhiteSpace(slot) ? "default" : Path.GetFileName(slot.Trim());
            requested = string.IsNullOrWhiteSpace(requested) ? "default" : requested;
            if (!string.Equals(Slot, requested, StringComparison.Ordinal))
            {
                Slot = requested;
                Values.Clear();
            }
        }

        public static void SetString(string key, string value) => Values[key] = value ?? string.Empty;
        public static string GetString(string key, string fallback = "") => Values.TryGetValue(key, out var v) ? v : fallback;
        public static void SetInt(string key, int value) => SetString(key, value.ToString(CultureInfo.InvariantCulture));
        public static int GetInt(string key, int fallback = 0) => int.TryParse(GetString(key), NumberStyles.Integer, CultureInfo.InvariantCulture, out var v) ? v : fallback;
        public static void SetFloat(string key, float value) => SetString(key, value.ToString("R", CultureInfo.InvariantCulture));
        public static float GetFloat(string key, float fallback = 0.0f) => float.TryParse(GetString(key), NumberStyles.Float, CultureInfo.InvariantCulture, out var v) ? v : fallback;
        public static void SetBool(string key, bool value) => SetString(key, value ? "true" : "false");
        public static bool GetBool(string key, bool fallback = false) => bool.TryParse(GetString(key), out var v) ? v : fallback;
        public static void SetVector2(string key, Vector2 value) => SetString(key, $"{value.X.ToString("R", CultureInfo.InvariantCulture)} {value.Y.ToString("R", CultureInfo.InvariantCulture)}");
        public static Vector2 GetVector2(string key, Vector2 fallback = default)
        {
            var parts = GetString(key).Split(new[] { ' ', ',', ';' }, StringSplitOptions.RemoveEmptyEntries);
            return parts.Length == 2 && float.TryParse(parts[0], NumberStyles.Float, CultureInfo.InvariantCulture, out var x)
                && float.TryParse(parts[1], NumberStyles.Float, CultureInfo.InvariantCulture, out var y) ? new Vector2(x, y) : fallback;
        }
        public static void SetVector3(string key, Vector3 value) => SetString(key, $"{value.X.ToString("R", CultureInfo.InvariantCulture)} {value.Y.ToString("R", CultureInfo.InvariantCulture)} {value.Z.ToString("R", CultureInfo.InvariantCulture)}");
        public static Vector3 GetVector3(string key, Vector3 fallback = default)
        {
            var parts = GetString(key).Split(new[] { ' ', ',', ';' }, StringSplitOptions.RemoveEmptyEntries);
            return parts.Length == 3 && float.TryParse(parts[0], NumberStyles.Float, CultureInfo.InvariantCulture, out var x)
                && float.TryParse(parts[1], NumberStyles.Float, CultureInfo.InvariantCulture, out var y)
                && float.TryParse(parts[2], NumberStyles.Float, CultureInfo.InvariantCulture, out var z) ? new Vector3(x, y, z) : fallback;
        }
        public static string[] Keys => Values.Keys.OrderBy(key => key, StringComparer.Ordinal).ToArray();
        public static bool Contains(string key) => Values.ContainsKey(key);
        public static void Remove(string key) => Values.Remove(key);
        public static void Clear() => Values.Clear();

        public static void Save()
        {
            Directory.CreateDirectory(SaveDirectory);
            var tempPath = SlotPath + ".tmp";
            File.WriteAllText(tempPath, JsonSerializer.Serialize(Values, new JsonSerializerOptions { WriteIndented = true }));
            File.Move(tempPath, SlotPath, overwrite: true);
        }

        public static bool Load()
        {
            if (!File.Exists(SlotPath))
            {
                Values.Clear();
                return false;
            }
            try
            {
                var loaded = JsonSerializer.Deserialize<Dictionary<string, string>>(File.ReadAllText(SlotPath));
                Values.Clear();
                if (loaded is not null) foreach (var pair in loaded) Values[pair.Key] = pair.Value;
                return true;
            }
            catch (Exception ex)
            {
                Log.Warning($"SaveData could not load slot '{Slot}': {ex.Message}");
                return false;
            }
        }
    }


    [AttributeUsage(AttributeTargets.Field, AllowMultiple = false, Inherited = true)]
    public sealed class ExposeAttribute : Attribute
    {
        public string? DisplayName { get; }
        public ExposeAttribute() { }
        public ExposeAttribute(string displayName) => DisplayName = displayName;
    }

    [AttributeUsage(AttributeTargets.Field, AllowMultiple = false, Inherited = true)]
    public sealed class TooltipAttribute : Attribute
    {
        public string Text { get; }
        public TooltipAttribute(string text) => Text = text ?? string.Empty;
    }

    [AttributeUsage(AttributeTargets.Field, AllowMultiple = false, Inherited = true)]
    public sealed class RangeAttribute : Attribute
    {
        public float Min { get; }
        public float Max { get; }
        public RangeAttribute(float min, float max)
        {
            if (min <= max) { Min = min; Max = max; }
            else { Min = max; Max = min; }
        }
    }

    // Keeps serialized Inspector data valid when a script author renames an exposed
    // field. The old semantic key can remain readable without making CLR metadata
    // tokens or field offsets part of the scene format.
    [AttributeUsage(AttributeTargets.Field, AllowMultiple = true, Inherited = true)]
    public sealed class FormerlySerializedAsAttribute : Attribute
    {
        public string Name { get; }
        public FormerlySerializedAsAttribute(string name) => Name = name ?? string.Empty;
    }

    public abstract class Component
    {
        public Entity Entity { get; private set; } = null!;
        internal void Attach(ulong entityId) => Entity = new Entity(entityId);
        public virtual void Start() { }
        public virtual void OnEnable() { }
        public virtual void Update(float deltaTime) { }
        public virtual void OnDisable() { }
        public virtual void OnTriggerEnter(Entity other) { }
        public virtual void OnTriggerStay(Entity other) { }
        public virtual void OnTriggerExit(Entity other) { }
        // Called before script instances are discarded during reload or shutdown.
        public virtual void OnDestroy() { }
    }

    public static class Log
    {
        public static void Info(string message) => Managed.Native.Log(0, message);
        public static void Warning(string message) => Managed.Native.Log(1, message);
        public static void Error(string message) => Managed.Native.Log(2, message);
    }

    namespace Managed
    {
        [StructLayout(LayoutKind.Sequential)]
        public struct NativeTransform
        {
            public float Px, Py, Pz;
            public float Rx, Ry, Rz;
            public float Sx, Sy, Sz;

            public Vector3 Position
            {
                readonly get => new(Px, Py, Pz);
                set => (Px, Py, Pz) = (value.X, value.Y, value.Z);
            }

            public Vector3 Rotation
            {
                readonly get => new(Rx, Ry, Rz);
                set => (Rx, Ry, Rz) = (value.X, value.Y, value.Z);
            }

            public Vector3 Scale
            {
                readonly get => new(Sx, Sy, Sz);
                set => (Sx, Sy, Sz) = (value.X, value.Y, value.Z);
            }
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct NativeVec2
        {
            public float X, Y;
            public NativeVec2(Vector2 value) => (X, Y) = (value.X, value.Y);
            public readonly Vector2 ToManaged() => new(X, Y);
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct NativeVec3
        {
            public float X, Y, Z;
            public NativeVec3(Vector3 value) => (X, Y, Z) = (value.X, value.Y, value.Z);
            public readonly Vector3 ToManaged() => new(X, Y, Z);
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct NativeCamera
        {
            public NativeVec3 Position;
            public float Yaw;
            public float Pitch;
            public float VerticalFovDegrees;
            public float NearPlane;
            public float FarPlane;

            public NativeCamera(CameraState state)
            {
                Position = new NativeVec3(state.Position);
                Yaw = state.Yaw;
                Pitch = state.Pitch;
                VerticalFovDegrees = state.VerticalFovDegrees;
                NearPlane = state.NearPlane;
                FarPlane = state.FarPlane;
            }

            public readonly CameraState ToManaged() => new(
                Position.ToManaged(), Yaw, Pitch, VerticalFovDegrees, NearPlane, FarPlane);
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct NativeColor
        {
            public float R, G, B, A;
            public NativeColor(Color value) => (R, G, B, A) = (value.R, value.G, value.B, value.A);
            public readonly Color ToManaged() => new(R, G, B, A);
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct NativeScriptSlot
        {
            public uint Slot;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct NativePerformanceSnapshot
        {
            public ulong FrameIndex;
            public double FrameMs;
            public double SmoothedFrameMs;
            public double MaxFrameMs;
            public double UpdateMs;
            public double RenderMs;
            public ulong ScenePasses;
            public ulong WorldDrawCalls;
            public ulong MeshConsidered;
            public ulong MeshVisible;
            public ulong MeshCulled;
            public ulong MeshDrawCalls;
            public ulong SpriteConsidered;
            public ulong SpriteVisible;
            public ulong SpriteCulled;
            public ulong SpriteDrawCalls;
            public ulong LightsConsidered;
            public ulong LightsVisible;
            public ulong LightsUploaded;
            public ulong UiDrawCalls;
            public ulong TotalDrawCalls;

            public readonly PerformanceSnapshot ToManaged() => new()
            {
                FrameIndex = FrameIndex, FrameMs = FrameMs, SmoothedFrameMs = SmoothedFrameMs,
                MaxFrameMs = MaxFrameMs, UpdateMs = UpdateMs, RenderMs = RenderMs, ScenePasses = ScenePasses,
                WorldDrawCalls = WorldDrawCalls, MeshConsidered = MeshConsidered, MeshVisible = MeshVisible,
                MeshCulled = MeshCulled, MeshDrawCalls = MeshDrawCalls, SpriteConsidered = SpriteConsidered,
                SpriteVisible = SpriteVisible, SpriteCulled = SpriteCulled, SpriteDrawCalls = SpriteDrawCalls,
                LightsConsidered = LightsConsidered, LightsVisible = LightsVisible, LightsUploaded = LightsUploaded,
                UiDrawCalls = UiDrawCalls, TotalDrawCalls = TotalDrawCalls
            };
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct NativeRaycastHit
        {
            public int Type;
            public float Distance;
            public NativeVec3 Position;
            public NativeVec3 Normal;
            public ulong EntityId;
            public ulong SectorIndex;
            public ulong SideIndex;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal unsafe struct NativeApi
        {
            public uint AbiVersion;
            public uint StructSize;
            public nint UserData;
            public delegate* unmanaged[Cdecl]<nint, int, byte*, void> Log;
            public delegate* unmanaged[Cdecl]<nint, ulong, int> EntityExists;
            public delegate* unmanaged[Cdecl]<nint, ulong, NativeTransform*, int> GetTransform;
            public delegate* unmanaged[Cdecl]<nint, ulong, NativeTransform*, int> SetTransform;
            public delegate* unmanaged[Cdecl]<nint, ulong, int*, int> GetEnabled;
            public delegate* unmanaged[Cdecl]<nint, ulong, int, int> SetEnabled;
            public delegate* unmanaged[Cdecl]<nint, byte*, float> ActionValue;
            public delegate* unmanaged[Cdecl]<nint, byte*, int> ActionDown;
            public delegate* unmanaged[Cdecl]<nint, byte*, int> ActionPressed;
            public delegate* unmanaged[Cdecl]<nint, byte*, int> ActionReleased;
            public delegate* unmanaged[Cdecl]<nint, byte*, ulong> FindEntityByName;
            public delegate* unmanaged[Cdecl]<nint, byte*, ulong> FindEntityWithTag;
            public delegate* unmanaged[Cdecl]<nint, int, byte*, ulong*, nuint, nuint> QueryEntities;
            public delegate* unmanaged[Cdecl]<nint, byte*, ulong> CreateEntity;
            public delegate* unmanaged[Cdecl]<nint, ulong, int> DestroyEntity;
            public delegate* unmanaged[Cdecl]<nint, ulong, byte*, ulong> CloneEntity;
            public delegate* unmanaged[Cdecl]<nint, ulong, byte*, int> HasComponent;
            public delegate* unmanaged[Cdecl]<nint, ulong, byte*, int> AddComponent;
            public delegate* unmanaged[Cdecl]<nint, ulong, byte*, int> RemoveComponent;
            public delegate* unmanaged[Cdecl]<nint, ulong, byte*> GetEntityName;
            public delegate* unmanaged[Cdecl]<nint, ulong, byte*, int> SetEntityName;
            public delegate* unmanaged[Cdecl]<nint, ulong, byte*> GetEntityTag;
            public delegate* unmanaged[Cdecl]<nint, ulong, byte*, int> SetEntityTag;
            public delegate* unmanaged[Cdecl]<nint, ulong, byte*> GetEntityLayer;
            public delegate* unmanaged[Cdecl]<nint, ulong, byte*, int> SetEntityLayer;
            public delegate* unmanaged[Cdecl]<nint, byte*, byte*, NativeVec3*, ulong> InstantiatePrefab;
            public delegate* unmanaged[Cdecl]<nint, NativeVec3*, NativeVec3*, float, int, ulong, NativeRaycastHit*, int> Raycast2D;
            public delegate* unmanaged[Cdecl]<nint, NativeVec3*, float, int, ulong, ulong*, nuint, nuint> OverlapCircle2D;
            public delegate* unmanaged[Cdecl]<nint, NativeVec3*, NativeVec3*, float, ulong, NativeVec3*, int> ResolveCircleMotion2D;
            public delegate* unmanaged[Cdecl]<nint, byte*, float, int> AudioPlayOneShot;
            public delegate* unmanaged[Cdecl]<nint, byte*, float, int, ulong> AudioPlayVoice;
            public delegate* unmanaged[Cdecl]<nint, ulong, int> AudioStopVoice;
            public delegate* unmanaged[Cdecl]<nint, ulong, float, int> AudioSetVoiceVolume;
            public delegate* unmanaged[Cdecl]<nint, ulong, int, int> AudioSetVoiceLoop;
            public delegate* unmanaged[Cdecl]<nint, ulong, int, int> AudioSetVoicePaused;
            public delegate* unmanaged[Cdecl]<nint, ulong, NativeVec3*, int, float, float, int> AudioSetVoicePosition;
            public delegate* unmanaged[Cdecl]<nint, ulong, int> AudioVoicePlaying;
            public delegate* unmanaged[Cdecl]<nint, NativeVec3*, int> AudioSetListenerPosition;
            public delegate* unmanaged[Cdecl]<nint, NativeVec3*, int> AudioGetListenerPosition;
            public delegate* unmanaged[Cdecl]<nint, int> AudioUseCameraListener;
            public delegate* unmanaged[Cdecl]<nint, float, int> AudioSetMasterVolume;
            public delegate* unmanaged[Cdecl]<nint, float> AudioGetMasterVolume;
            public delegate* unmanaged[Cdecl]<nint, int> AudioStopAll;
            public delegate* unmanaged[Cdecl]<nint, nuint> AudioActiveVoiceCount;
            public delegate* unmanaged[Cdecl]<nint, byte*, int> RequestSceneLoad;
            public delegate* unmanaged[Cdecl]<nint, byte*> GetCurrentScenePath;
            public delegate* unmanaged[Cdecl]<nint, byte*, byte*, byte*> ResolveAssetPath;
            public delegate* unmanaged[Cdecl]<nint, byte*, byte*> FindAssetId;
            public delegate* unmanaged[Cdecl]<nint, ulong, byte*, int> HasManagedScript;
            public delegate* unmanaged[Cdecl]<nint, ulong, byte*, int> AddManagedScript;
            public delegate* unmanaged[Cdecl]<nint, ulong, byte*, int> RemoveManagedScript;
            public delegate* unmanaged[Cdecl]<nint, ulong, byte*, byte*, float*, int> GetPropertyFloat;
            public delegate* unmanaged[Cdecl]<nint, ulong, byte*, byte*, float, int> SetPropertyFloat;
            public delegate* unmanaged[Cdecl]<nint, ulong, byte*, byte*, int*, int> GetPropertyBool;
            public delegate* unmanaged[Cdecl]<nint, ulong, byte*, byte*, int, int> SetPropertyBool;
            public delegate* unmanaged[Cdecl]<nint, ulong, byte*, byte*, byte*> GetPropertyText;
            public delegate* unmanaged[Cdecl]<nint, ulong, byte*, byte*, byte*, int> SetPropertyText;
            public delegate* unmanaged[Cdecl]<nint, ulong, byte*, byte*, NativeVec2*, int> GetPropertyVec2;
            public delegate* unmanaged[Cdecl]<nint, ulong, byte*, byte*, NativeVec2*, int> SetPropertyVec2;
            public delegate* unmanaged[Cdecl]<nint, ulong, byte*, byte*, NativeVec3*, int> GetPropertyVec3;
            public delegate* unmanaged[Cdecl]<nint, ulong, byte*, byte*, NativeVec3*, int> SetPropertyVec3;
            public delegate* unmanaged[Cdecl]<nint, ulong, byte*, byte*, NativeColor*, int> GetPropertyColor;
            public delegate* unmanaged[Cdecl]<nint, ulong, byte*, byte*, NativeColor*, int> SetPropertyColor;
            public delegate* unmanaged[Cdecl]<nint, byte*, ulong> UiFindNode;
            public delegate* unmanaged[Cdecl]<nint, ulong, int> UiNodeExists;
            public delegate* unmanaged[Cdecl]<nint, ulong, byte*> UiGetNodeText;
            public delegate* unmanaged[Cdecl]<nint, ulong, byte*, int> UiSetNodeText;
            public delegate* unmanaged[Cdecl]<nint, ulong, int*, int> UiGetNodeEnabled;
            public delegate* unmanaged[Cdecl]<nint, ulong, int, int> UiSetNodeEnabled;
            public delegate* unmanaged[Cdecl]<nint, ulong, int, int*, int> UiGetNodeBool;
            public delegate* unmanaged[Cdecl]<nint, ulong, int, int, int> UiSetNodeBool;
            public delegate* unmanaged[Cdecl]<nint, ulong, int, float*, int> UiGetNodeFloat;
            public delegate* unmanaged[Cdecl]<nint, ulong, int, float, int> UiSetNodeFloat;
            public delegate* unmanaged[Cdecl]<nint, ulong, int, NativeColor*, int> UiSetNodeColor;
            public delegate* unmanaged[Cdecl]<nint, ulong, int, byte*, byte*, int> UiSetNodeAsset;
            public delegate* unmanaged[Cdecl]<nint, ulong, int> UiConsumeClick;
            public delegate* unmanaged[Cdecl]<nint, ulong, byte*> UiGetValue;
            public delegate* unmanaged[Cdecl]<nint, ulong, byte*, int> UiSetValue;
            public delegate* unmanaged[Cdecl]<nint, ulong, byte*, byte*, int> UiSetProperty;
            public delegate* unmanaged[Cdecl]<nint, ulong, byte*, int, int> UiSetClass;
            public delegate* unmanaged[Cdecl]<nint, ulong, byte*, byte*, void> TraceRuntimeEvent;
            public delegate* unmanaged[Cdecl]<nint, NativeCamera*, int> GetCamera;
            public delegate* unmanaged[Cdecl]<nint, NativeCamera*, int> SetCamera;
            public delegate* unmanaged[Cdecl]<nint, TransformUpdate*, nuint, nuint> SetTransforms;
            public delegate* unmanaged[Cdecl]<nint, NativePerformanceSnapshot*, int> GetPerformance;
        }

        internal static unsafe class Native
        {
            private static NativeApi _api;
            private static bool _ready;

            internal static bool Initialize(NativeApi* api)
            {
                if (api == null || api->AbiVersion != 12 || api->StructSize < (uint)sizeof(NativeApi)) return false;
                _api = *api;
                _ready = true;
                return true;
            }

            internal static void TraceRuntimeEvent(ulong entityId, string component, string callback)
            {
                if (!_ready || _api.TraceRuntimeEvent == null) return;
                var componentBytes = Encoding.UTF8.GetBytes((component ?? string.Empty) + "\0");
                var callbackBytes = Encoding.UTF8.GetBytes((callback ?? string.Empty) + "\0");
                fixed (byte* componentPtr = componentBytes)
                fixed (byte* callbackPtr = callbackBytes)
                    _api.TraceRuntimeEvent(_api.UserData, entityId, componentPtr, callbackPtr);
            }

            internal static void Reset()
            {
                _api = default;
                _ready = false;
            }

            internal static void Log(int level, string message)
            {
                if (!_ready || _api.Log == null) return;
                var bytes = Encoding.UTF8.GetBytes((message ?? string.Empty) + "\0");
                fixed (byte* ptr = bytes) _api.Log(_api.UserData, level, ptr);
            }

            internal static bool EntityExists(ulong id) =>
                _ready && _api.EntityExists != null && _api.EntityExists(_api.UserData, id) != 0;

            internal static NativeTransform GetTransform(ulong id)
            {
                NativeTransform value = default;
                if (!_ready || _api.GetTransform == null || _api.GetTransform(_api.UserData, id, &value) == 0)
                    throw new InvalidOperationException($"Entity {id} is not available.");
                return value;
            }

            internal static void SetTransform(ulong id, NativeTransform value)
            {
                if (!_ready || _api.SetTransform == null || _api.SetTransform(_api.UserData, id, &value) == 0)
                    throw new InvalidOperationException($"Entity {id} is not available.");
            }

            internal static int SetTransforms(TransformUpdate[] updates, int count)
            {
                if (updates is null) throw new ArgumentNullException(nameof(updates));
                var requested = count < 0 ? updates.Length : Math.Clamp(count, 0, updates.Length);
                if (requested == 0) return 0;
                if (!_ready || _api.SetTransforms == null) return 0;
                fixed (TransformUpdate* ptr = updates)
                    return checked((int)_api.SetTransforms(_api.UserData, ptr, (nuint)requested));
            }

            internal static bool TryGetPerformance(out PerformanceSnapshot snapshot)
            {
                snapshot = default;
                if (!_ready || _api.GetPerformance == null) return false;
                NativePerformanceSnapshot native = default;
                if (_api.GetPerformance(_api.UserData, &native) == 0) return false;
                snapshot = native.ToManaged();
                return true;
            }

            internal static CameraState GetCamera()
            {
                NativeCamera value = default;
                if (!_ready || _api.GetCamera == null || _api.GetCamera(_api.UserData, &value) == 0)
                    throw new InvalidOperationException("The active scene camera is not available.");
                return value.ToManaged();
            }

            internal static void SetCamera(CameraState state)
            {
                var value = new NativeCamera(state);
                if (!_ready || _api.SetCamera == null || _api.SetCamera(_api.UserData, &value) == 0)
                    throw new InvalidOperationException("The active scene camera is not available.");
            }

            internal static bool GetEnabled(ulong id)
            {
                int value = 0;
                if (!_ready || _api.GetEnabled == null || _api.GetEnabled(_api.UserData, id, &value) == 0)
                    throw new InvalidOperationException($"Entity {id} is not available.");
                return value != 0;
            }

            internal static void SetEnabled(ulong id, bool enabled)
            {
                if (!_ready || _api.SetEnabled == null || _api.SetEnabled(_api.UserData, id, enabled ? 1 : 0) == 0)
                    throw new InvalidOperationException($"Entity {id} is not available.");
            }

            private static byte[] Utf8(string text) => Encoding.UTF8.GetBytes((text ?? string.Empty) + "\0");

            internal static float ActionValue(string action)
            {
                if (!_ready || _api.ActionValue == null) return 0.0f;
                var bytes = Utf8(action); fixed (byte* ptr = bytes) return _api.ActionValue(_api.UserData, ptr);
            }
            internal static bool ActionDown(string action)
            {
                if (!_ready || _api.ActionDown == null) return false;
                var bytes = Utf8(action); fixed (byte* ptr = bytes) return _api.ActionDown(_api.UserData, ptr) != 0;
            }
            internal static bool ActionPressed(string action)
            {
                if (!_ready || _api.ActionPressed == null) return false;
                var bytes = Utf8(action); fixed (byte* ptr = bytes) return _api.ActionPressed(_api.UserData, ptr) != 0;
            }
            internal static bool ActionReleased(string action)
            {
                if (!_ready || _api.ActionReleased == null) return false;
                var bytes = Utf8(action); fixed (byte* ptr = bytes) return _api.ActionReleased(_api.UserData, ptr) != 0;
            }
            internal static ulong FindEntityByName(string name)
            {
                if (!_ready || _api.FindEntityByName == null) return 0;
                var bytes = Utf8(name); fixed (byte* ptr = bytes) return _api.FindEntityByName(_api.UserData, ptr);
            }
            internal static ulong FindEntityWithTag(string tag)
            {
                if (!_ready || _api.FindEntityWithTag == null) return 0;
                var bytes = Utf8(tag); fixed (byte* ptr = bytes) return _api.FindEntityWithTag(_api.UserData, ptr);
            }
            internal static Entity[] QueryEntities(int queryType, string value)
            {
                if (!_ready || _api.QueryEntities == null) return Array.Empty<Entity>();
                var bytes = Utf8(value);
                fixed (byte* text = bytes)
                {
                    var count = _api.QueryEntities(_api.UserData, queryType, text, null, 0);
                    if (count == 0) return Array.Empty<Entity>();
                    var ids = new ulong[checked((int)count)];
                    nuint written;
                    fixed (ulong* output = ids)
                        written = _api.QueryEntities(_api.UserData, queryType, text, output, count);
                    if (written < count) Array.Resize(ref ids, checked((int)written));
                    var entities = new Entity[ids.Length];
                    for (var i = 0; i < ids.Length; ++i) entities[i] = new Entity(ids[i]);
                    return entities;
                }
            }
            internal static ulong CreateEntity(string name)
            {
                if (!_ready || _api.CreateEntity == null) return 0;
                var bytes = Utf8(name); fixed (byte* ptr = bytes) return _api.CreateEntity(_api.UserData, ptr);
            }
            internal static bool DestroyEntity(ulong id) =>
                _ready && _api.DestroyEntity != null && _api.DestroyEntity(_api.UserData, id) != 0;
            internal static ulong CloneEntity(ulong id, string name)
            {
                if (!_ready || _api.CloneEntity == null) return 0;
                var bytes = Utf8(name); fixed (byte* ptr = bytes) return _api.CloneEntity(_api.UserData, id, ptr);
            }
            internal static bool HasComponent(ulong id, string key)
            {
                if (!_ready || _api.HasComponent == null) return false;
                var bytes = Utf8(key); fixed (byte* ptr = bytes) return _api.HasComponent(_api.UserData, id, ptr) != 0;
            }
            internal static bool AddComponent(ulong id, string key)
            {
                if (!_ready || _api.AddComponent == null) return false;
                var bytes = Utf8(key); fixed (byte* ptr = bytes) return _api.AddComponent(_api.UserData, id, ptr) != 0;
            }
            internal static bool RemoveComponent(ulong id, string key)
            {
                if (!_ready || _api.RemoveComponent == null) return false;
                var bytes = Utf8(key); fixed (byte* ptr = bytes) return _api.RemoveComponent(_api.UserData, id, ptr) != 0;
            }

            private static string ReadBorrowedUtf8(byte* value) => value == null ? string.Empty : Marshal.PtrToStringUTF8((nint)value) ?? string.Empty;
            internal static string GetEntityName(ulong id) => _ready && _api.GetEntityName != null ? ReadBorrowedUtf8(_api.GetEntityName(_api.UserData, id)) : string.Empty;
            internal static string GetEntityTag(ulong id) => _ready && _api.GetEntityTag != null ? ReadBorrowedUtf8(_api.GetEntityTag(_api.UserData, id)) : string.Empty;
            internal static string GetEntityLayer(ulong id) => _ready && _api.GetEntityLayer != null ? ReadBorrowedUtf8(_api.GetEntityLayer(_api.UserData, id)) : string.Empty;
            internal static void SetEntityName(ulong id, string value) { var b = Utf8(value); fixed (byte* p = b) if (!_ready || _api.SetEntityName == null || _api.SetEntityName(_api.UserData, id, p) == 0) throw new InvalidOperationException($"Entity {id} is not available."); }
            internal static void SetEntityTag(ulong id, string value) { var b = Utf8(value); fixed (byte* p = b) if (!_ready || _api.SetEntityTag == null || _api.SetEntityTag(_api.UserData, id, p) == 0) throw new InvalidOperationException($"Entity {id} is not available."); }
            internal static void SetEntityLayer(ulong id, string value) { var b = Utf8(value); fixed (byte* p = b) if (!_ready || _api.SetEntityLayer == null || _api.SetEntityLayer(_api.UserData, id, p) == 0) throw new InvalidOperationException($"Entity {id} is not available."); }
            internal static ulong InstantiatePrefab(string path, string name, Vector3 position)
            {
                if (!_ready || _api.InstantiatePrefab == null) return 0;
                var pathBytes = Utf8(path); var nameBytes = Utf8(name); var nativePosition = new NativeVec3(position);
                fixed (byte* pathPtr = pathBytes) fixed (byte* namePtr = nameBytes)
                    return _api.InstantiatePrefab(_api.UserData, pathPtr, namePtr, &nativePosition);
            }
            internal static bool Raycast2D(Vector3 origin, Vector3 direction, float maxDistance,
                bool includeTriggers, ulong ignoreEntity, out RaycastHit2D hit)
            {
                hit = default;
                if (!_ready || _api.Raycast2D == null) return false;
                var nativeOrigin = new NativeVec3(origin); var nativeDirection = new NativeVec3(direction); NativeRaycastHit nativeHit = default;
                if (_api.Raycast2D(_api.UserData, &nativeOrigin, &nativeDirection, maxDistance,
                    includeTriggers ? 1 : 0, ignoreEntity, &nativeHit) == 0) return false;
                hit = new RaycastHit2D
                {
                    Type = nativeHit.Type == 1 ? RaycastHitType2D.EntityCollider : RaycastHitType2D.SectorWall,
                    Distance = nativeHit.Distance,
                    Position = nativeHit.Position.ToManaged(),
                    Normal = nativeHit.Normal.ToManaged(),
                    Entity = nativeHit.EntityId == 0 ? null : new Entity(nativeHit.EntityId),
                    SectorIndex = nativeHit.SectorIndex,
                    SideIndex = nativeHit.SideIndex,
                };
                return true;
            }

            internal static Entity[] OverlapCircle2D(Vector3 position, float radius, bool includeTriggers, ulong ignoreEntity)
            {
                if (!_ready || _api.OverlapCircle2D == null) return Array.Empty<Entity>();
                var nativePosition = new NativeVec3(position);
                var count = _api.OverlapCircle2D(_api.UserData, &nativePosition, Math.Max(0.0f, radius),
                    includeTriggers ? 1 : 0, ignoreEntity, null, 0);
                if (count == 0 || count > (nuint)int.MaxValue) return Array.Empty<Entity>();
                var ids = new ulong[(int)count];
                nuint returned;
                fixed (ulong* idPtr = ids)
                {
                    returned = _api.OverlapCircle2D(_api.UserData, &nativePosition, Math.Max(0.0f, radius),
                        includeTriggers ? 1 : 0, ignoreEntity, idPtr, (nuint)ids.Length);
                }
                if (returned < (nuint)ids.Length) Array.Resize(ref ids, (int)returned);
                return ids.Where(id => id != 0).Select(id => new Entity(id)).ToArray();
            }

            internal static Vector3 ResolveCircleMotion2D(Vector3 from, Vector3 candidate, float radius, ulong ignoreEntity)
            {
                if (!_ready || _api.ResolveCircleMotion2D == null) return candidate;
                var nativeFrom = new NativeVec3(from); var nativeCandidate = new NativeVec3(candidate); NativeVec3 resolved = default;
                return _api.ResolveCircleMotion2D(_api.UserData, &nativeFrom, &nativeCandidate, Math.Max(0.0f, radius),
                    ignoreEntity, &resolved) != 0 ? resolved.ToManaged() : candidate;
            }

            internal static bool AudioPlayOneShot(string path, float volume)
            {
                if (!_ready || _api.AudioPlayOneShot == null) return false;
                var bytes = Utf8(path); fixed (byte* ptr = bytes)
                    return _api.AudioPlayOneShot(_api.UserData, ptr, volume) != 0;
            }
            internal static ulong AudioPlayVoice(string path, float volume, bool loop)
            {
                if (!_ready || _api.AudioPlayVoice == null) return 0;
                var bytes = Utf8(path); fixed (byte* ptr = bytes)
                    return _api.AudioPlayVoice(_api.UserData, ptr, volume, loop ? 1 : 0);
            }
            internal static bool AudioStopVoice(ulong handle) =>
                _ready && _api.AudioStopVoice != null && _api.AudioStopVoice(_api.UserData, handle) != 0;
            internal static bool AudioSetVoiceVolume(ulong handle, float volume) =>
                _ready && _api.AudioSetVoiceVolume != null && _api.AudioSetVoiceVolume(_api.UserData, handle, volume) != 0;
            internal static bool AudioSetVoiceLoop(ulong handle, bool loop) =>
                _ready && _api.AudioSetVoiceLoop != null && _api.AudioSetVoiceLoop(_api.UserData, handle, loop ? 1 : 0) != 0;
            internal static bool AudioSetVoicePaused(ulong handle, bool paused) =>
                _ready && _api.AudioSetVoicePaused != null && _api.AudioSetVoicePaused(_api.UserData, handle, paused ? 1 : 0) != 0;
            internal static bool AudioSetVoicePosition(ulong handle, Vector3 position, bool spatial, float minDistance, float maxDistance)
            {
                if (!_ready || _api.AudioSetVoicePosition == null) return false;
                var native = new NativeVec3(position);
                return _api.AudioSetVoicePosition(_api.UserData, handle, &native, spatial ? 1 : 0, minDistance, maxDistance) != 0;
            }
            internal static bool AudioVoicePlaying(ulong handle) =>
                _ready && _api.AudioVoicePlaying != null && _api.AudioVoicePlaying(_api.UserData, handle) != 0;
            internal static void AudioSetListenerPosition(Vector3 position)
            {
                if (!_ready || _api.AudioSetListenerPosition == null) return;
                var native = new NativeVec3(position); _api.AudioSetListenerPosition(_api.UserData, &native);
            }
            internal static Vector3 AudioListenerPosition()
            {
                NativeVec3 native = default;
                return _ready && _api.AudioGetListenerPosition != null && _api.AudioGetListenerPosition(_api.UserData, &native) != 0
                    ? native.ToManaged() : default;
            }
            internal static void AudioUseCameraListener()
            {
                if (_ready && _api.AudioUseCameraListener != null) _api.AudioUseCameraListener(_api.UserData);
            }

            internal static void SetAudioMasterVolume(float volume)
            {
                if (_ready && _api.AudioSetMasterVolume != null)
                    _api.AudioSetMasterVolume(_api.UserData, Math.Clamp(volume, 0.0f, 1.0f));
            }
            internal static float AudioMasterVolume() =>
                _ready && _api.AudioGetMasterVolume != null ? _api.AudioGetMasterVolume(_api.UserData) : 0.0f;
            internal static bool AudioStopAll() => _ready && _api.AudioStopAll != null && _api.AudioStopAll(_api.UserData) != 0;
            internal static int AudioActiveVoiceCount() =>
                _ready && _api.AudioActiveVoiceCount != null ? checked((int)_api.AudioActiveVoiceCount(_api.UserData)) : 0;
            internal static bool RequestSceneLoad(string path)
            {
                if (!_ready || _api.RequestSceneLoad == null) return false;
                var bytes = Utf8(path); fixed (byte* ptr = bytes)
                    return _api.RequestSceneLoad(_api.UserData, ptr) != 0;
            }
            internal static string CurrentScenePath() =>
                _ready && _api.GetCurrentScenePath != null ? ReadBorrowedUtf8(_api.GetCurrentScenePath(_api.UserData)) : string.Empty;

            internal static string ResolveAssetPath(string assetId, string fallbackPath)
            {
                if (!_ready || _api.ResolveAssetPath == null) return string.Empty;
                var idBytes = Utf8(assetId); var pathBytes = Utf8(fallbackPath);
                fixed (byte* idPtr = idBytes) fixed (byte* pathPtr = pathBytes)
                    return ReadBorrowedUtf8(_api.ResolveAssetPath(_api.UserData, idPtr, pathPtr));
            }

            internal static string FindAssetId(string path)
            {
                if (!_ready || _api.FindAssetId == null) return string.Empty;
                var bytes = Utf8(path);
                fixed (byte* ptr = bytes) return ReadBorrowedUtf8(_api.FindAssetId(_api.UserData, ptr));
            }

            internal static ulong UiFindNode(string name)
            {
                if (!_ready || _api.UiFindNode == null) return 0;
                var bytes = Utf8(name);
                fixed (byte* ptr = bytes) return _api.UiFindNode(_api.UserData, ptr);
            }
            internal static bool UiNodeExists(ulong id) =>
                _ready && _api.UiNodeExists != null && _api.UiNodeExists(_api.UserData, id) != 0;
            internal static string UiGetNodeText(ulong id) =>
                _ready && _api.UiGetNodeText != null ? ReadBorrowedUtf8(_api.UiGetNodeText(_api.UserData, id)) : string.Empty;
            internal static bool UiSetNodeText(ulong id, string value)
            {
                if (!_ready || _api.UiSetNodeText == null) return false;
                var bytes = Utf8(value);
                fixed (byte* ptr = bytes) return _api.UiSetNodeText(_api.UserData, id, ptr) != 0;
            }
            internal static bool UiGetNodeEnabled(ulong id, out bool enabled)
            {
                enabled = false;
                if (!_ready || _api.UiGetNodeEnabled == null) return false;
                int value = 0;
                var ok = _api.UiGetNodeEnabled(_api.UserData, id, &value) != 0;
                enabled = value != 0;
                return ok;
            }
            internal static bool UiSetNodeEnabled(ulong id, bool enabled) =>
                _ready && _api.UiSetNodeEnabled != null && _api.UiSetNodeEnabled(_api.UserData, id, enabled ? 1 : 0) != 0;


            internal static bool UiGetNodeBool(ulong id, int property, out bool value)
            {
                value = false;
                if (!_ready || _api.UiGetNodeBool == null) return false;
                int native = 0;
                var ok = _api.UiGetNodeBool(_api.UserData, id, property, &native) != 0;
                value = native != 0;
                return ok;
            }
            internal static bool UiSetNodeBool(ulong id, int property, bool value) =>
                _ready && _api.UiSetNodeBool != null && _api.UiSetNodeBool(_api.UserData, id, property, value ? 1 : 0) != 0;
            internal static bool UiGetNodeFloat(ulong id, int property, out float value)
            {
                value = 0.0f;
                if (!_ready || _api.UiGetNodeFloat == null) return false;
                float native = 0.0f;
                var ok = _api.UiGetNodeFloat(_api.UserData, id, property, &native) != 0;
                value = native;
                return ok;
            }
            internal static bool UiSetNodeFloat(ulong id, int property, float value) =>
                _ready && _api.UiSetNodeFloat != null && _api.UiSetNodeFloat(_api.UserData, id, property, value) != 0;
            internal static bool UiSetNodeColor(ulong id, int property, Color value)
            {
                if (!_ready || _api.UiSetNodeColor == null) return false;
                NativeColor native = new(value);
                return _api.UiSetNodeColor(_api.UserData, id, property, &native) != 0;
            }
            internal static bool UiSetNodeAsset(ulong id, int property, AssetReference value)
            {
                if (!_ready || _api.UiSetNodeAsset == null) return false;
                var idBytes = Utf8(value.Id); var pathBytes = Utf8(value.FallbackPath);
                fixed (byte* idPtr = idBytes) fixed (byte* pathPtr = pathBytes)
                    return _api.UiSetNodeAsset(_api.UserData, id, property, idPtr, pathPtr) != 0;
            }
            internal static bool UiConsumeClick(ulong id) =>
                _ready && _api.UiConsumeClick != null && _api.UiConsumeClick(_api.UserData, id) != 0;
            internal static string UiGetValue(ulong id) =>
                _ready && _api.UiGetValue != null ? ReadBorrowedUtf8(_api.UiGetValue(_api.UserData, id)) : string.Empty;
            internal static bool UiSetValue(ulong id, string value)
            {
                if (!_ready || _api.UiSetValue == null) return false;
                var bytes = Utf8(value);
                fixed (byte* ptr = bytes) return _api.UiSetValue(_api.UserData, id, ptr) != 0;
            }
            internal static bool UiSetProperty(ulong id, string property, string value)
            {
                if (!_ready || _api.UiSetProperty == null || string.IsNullOrWhiteSpace(property)) return false;
                var propertyBytes = Utf8(property); var valueBytes = Utf8(value);
                fixed (byte* propertyPtr = propertyBytes)
                fixed (byte* valuePtr = valueBytes)
                    return _api.UiSetProperty(_api.UserData, id, propertyPtr, valuePtr) != 0;
            }
            internal static bool UiSetClass(ulong id, string className, bool enabled)
            {
                if (!_ready || _api.UiSetClass == null || string.IsNullOrWhiteSpace(className)) return false;
                var bytes = Utf8(className);
                fixed (byte* ptr = bytes) return _api.UiSetClass(_api.UserData, id, ptr, enabled ? 1 : 0) != 0;
            }

            internal static bool HasManagedScript(ulong id, string className)
            {
                if (!_ready || _api.HasManagedScript == null) return false;
                var bytes = Utf8(className); fixed (byte* ptr = bytes)
                    return _api.HasManagedScript(_api.UserData, id, ptr) != 0;
            }
            internal static bool AddManagedScript(ulong id, string className)
            {
                if (!_ready || _api.AddManagedScript == null) return false;
                var bytes = Utf8(className); fixed (byte* ptr = bytes)
                    return _api.AddManagedScript(_api.UserData, id, ptr) != 0;
            }
            internal static bool RemoveManagedScript(ulong id, string className)
            {
                if (!_ready || _api.RemoveManagedScript == null) return false;
                var bytes = Utf8(className); fixed (byte* ptr = bytes)
                    return _api.RemoveManagedScript(_api.UserData, id, ptr) != 0;
            }

            private static byte[] PairUtf8(string a, string b, out int split)
            {
                var aa = Encoding.UTF8.GetBytes((a ?? string.Empty) + "\0");
                var bb = Encoding.UTF8.GetBytes((b ?? string.Empty) + "\0");
                var all = new byte[aa.Length + bb.Length];
                Buffer.BlockCopy(aa, 0, all, 0, aa.Length);
                Buffer.BlockCopy(bb, 0, all, aa.Length, bb.Length);
                split = aa.Length;
                return all;
            }

            internal static float GetPropertyFloat(ulong id, string component, string property, float fallback)
            {
                if (!_ready || _api.GetPropertyFloat == null) return fallback;
                var bytes = PairUtf8(component, property, out var split); float value = fallback;
                fixed (byte* ptr = bytes) return _api.GetPropertyFloat(_api.UserData, id, ptr, ptr + split, &value) != 0 ? value : fallback;
            }
            internal static bool SetPropertyFloat(ulong id, string component, string property, float value)
            {
                if (!_ready || _api.SetPropertyFloat == null) return false;
                var bytes = PairUtf8(component, property, out var split);
                fixed (byte* ptr = bytes) return _api.SetPropertyFloat(_api.UserData, id, ptr, ptr + split, value) != 0;
            }
            internal static bool GetPropertyBool(ulong id, string component, string property, bool fallback)
            {
                if (!_ready || _api.GetPropertyBool == null) return fallback;
                var bytes = PairUtf8(component, property, out var split); int value = fallback ? 1 : 0;
                fixed (byte* ptr = bytes) return _api.GetPropertyBool(_api.UserData, id, ptr, ptr + split, &value) != 0 ? value != 0 : fallback;
            }
            internal static bool SetPropertyBool(ulong id, string component, string property, bool value)
            {
                if (!_ready || _api.SetPropertyBool == null) return false;
                var bytes = PairUtf8(component, property, out var split);
                fixed (byte* ptr = bytes) return _api.SetPropertyBool(_api.UserData, id, ptr, ptr + split, value ? 1 : 0) != 0;
            }
            internal static string GetPropertyText(ulong id, string component, string property, string fallback)
            {
                if (!_ready || _api.GetPropertyText == null) return fallback;
                var bytes = PairUtf8(component, property, out var split);
                fixed (byte* ptr = bytes)
                {
                    var result = _api.GetPropertyText(_api.UserData, id, ptr, ptr + split);
                    return result == null ? fallback : ReadBorrowedUtf8(result);
                }
            }
            internal static bool SetPropertyText(ulong id, string component, string property, string value)
            {
                if (!_ready || _api.SetPropertyText == null) return false;
                var pair = PairUtf8(component, property, out var split); var valueBytes = Utf8(value);
                fixed (byte* ptr = pair) fixed (byte* valuePtr = valueBytes)
                    return _api.SetPropertyText(_api.UserData, id, ptr, ptr + split, valuePtr) != 0;
            }
            internal static Vector2 GetPropertyVector2(ulong id, string component, string property, Vector2 fallback)
            {
                if (!_ready || _api.GetPropertyVec2 == null) return fallback;
                var bytes = PairUtf8(component, property, out var split); NativeVec2 value = new(fallback);
                fixed (byte* ptr = bytes) return _api.GetPropertyVec2(_api.UserData, id, ptr, ptr + split, &value) != 0 ? value.ToManaged() : fallback;
            }
            internal static bool SetPropertyVector2(ulong id, string component, string property, Vector2 value)
            {
                if (!_ready || _api.SetPropertyVec2 == null) return false;
                var bytes = PairUtf8(component, property, out var split); NativeVec2 native = new(value);
                fixed (byte* ptr = bytes) return _api.SetPropertyVec2(_api.UserData, id, ptr, ptr + split, &native) != 0;
            }
            internal static Vector3 GetPropertyVector3(ulong id, string component, string property, Vector3 fallback)
            {
                if (!_ready || _api.GetPropertyVec3 == null) return fallback;
                var bytes = PairUtf8(component, property, out var split); NativeVec3 value = new(fallback);
                fixed (byte* ptr = bytes) return _api.GetPropertyVec3(_api.UserData, id, ptr, ptr + split, &value) != 0 ? value.ToManaged() : fallback;
            }
            internal static bool SetPropertyVector3(ulong id, string component, string property, Vector3 value)
            {
                if (!_ready || _api.SetPropertyVec3 == null) return false;
                var bytes = PairUtf8(component, property, out var split); NativeVec3 native = new(value);
                fixed (byte* ptr = bytes) return _api.SetPropertyVec3(_api.UserData, id, ptr, ptr + split, &native) != 0;
            }
            internal static Color GetPropertyColor(ulong id, string component, string property, Color fallback)
            {
                if (!_ready || _api.GetPropertyColor == null) return fallback;
                var bytes = PairUtf8(component, property, out var split); NativeColor value = new(fallback);
                fixed (byte* ptr = bytes) return _api.GetPropertyColor(_api.UserData, id, ptr, ptr + split, &value) != 0 ? value.ToManaged() : fallback;
            }
            internal static bool SetPropertyColor(ulong id, string component, string property, Color value)
            {
                if (!_ready || _api.SetPropertyColor == null) return false;
                var bytes = PairUtf8(component, property, out var split); NativeColor native = new(value);
                fixed (byte* ptr = bytes) return _api.SetPropertyColor(_api.UserData, id, ptr, ptr + split, &native) != 0;
            }
        }

        public static unsafe class EntryPoint
        {
            private const int InitializeCommand = 1;
            private const int LoadGameAssemblyCommand = 2;
            private const int CreateScriptCommand = 3;
            private const int StartAllCommand = 4;
            private const int UpdateAllCommand = 5;
            private const int ShutdownCommand = 6;
            private const int ApplyFieldCommand = 7;
            private const int ValidateCandidateScriptCommand = 8;
            private const int CommitCandidateAssemblyCommand = 9;
            private const int DiscardCandidateAssemblyCommand = 10;
            private const int TriggerEnterCommand = 11;
            private const int TriggerExitCommand = 12;
            private const int StartScriptCommand = 13;
            private const int DestroyScriptCommand = 14;
            private const int TriggerStayCommand = 15;

            private sealed class GameLoadContext : AssemblyLoadContext
            {
                private readonly AssemblyDependencyResolver _resolver;

                internal GameLoadContext(string mainAssemblyPath)
                    : base($"Vespera.GameScripts.{Guid.NewGuid():N}", isCollectible: true)
                {
                    _resolver = new AssemblyDependencyResolver(mainAssemblyPath);
                }

                protected override Assembly? Load(AssemblyName assemblyName)
                {
                    // Vespera.NET must retain one type identity in the default context.
                    if (string.Equals(assemblyName.Name, typeof(Component).Assembly.GetName().Name, StringComparison.Ordinal))
                        return typeof(Component).Assembly;

                    var resolved = _resolver.ResolveAssemblyToPath(assemblyName);
                    return resolved is null ? null : LoadAssemblyUnlocked(this, resolved);
                }
            }

            private sealed class ScriptInstance
            {
                internal required Component Component { get; init; }
                internal required ulong EntityId { get; init; }
                internal required uint SlotIndex { get; init; }
                internal required string TypeName { get; init; }
                internal bool Faulted { get; set; }
                internal bool Active { get; set; }
            }

            private static Assembly? _gameAssembly;
            private static GameLoadContext? _gameLoadContext;
            private static Assembly? _candidateGameAssembly;
            private static GameLoadContext? _candidateGameLoadContext;
            private static readonly List<ScriptInstance> Components = new();

            private static void Trace(ScriptInstance instance, string callback) =>
                Native.TraceRuntimeEvent(instance.EntityId, instance.TypeName, callback);

            [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
            public static int Dispatch(int command, nint arg0, nint arg1, ulong entityId, float value)
            {
                try
                {
                    return command switch
                    {
                        InitializeCommand => Initialize((NativeApi*)arg0),
                        LoadGameAssemblyCommand => LoadGameAssembly((byte*)arg1),
                        CreateScriptCommand => CreateScript(entityId, (byte*)arg1, arg0 == 0 ? 0u : ((NativeScriptSlot*)arg0)->Slot),
                        StartAllCommand => StartAll(),
                        UpdateAllCommand => UpdateAll(value),
                        ShutdownCommand => Shutdown(),
                        ApplyFieldCommand => ApplyField(entityId, (byte*)arg0, (byte*)arg1),
                        ValidateCandidateScriptCommand => ValidateCandidateScript((byte*)arg1),
                        CommitCandidateAssemblyCommand => CommitCandidateAssembly(),
                        DiscardCandidateAssemblyCommand => DiscardCandidateAssembly(),
                        TriggerEnterCommand => DispatchTrigger(entityId, unchecked((ulong)arg0.ToInt64()), 0),
                        TriggerExitCommand => DispatchTrigger(entityId, unchecked((ulong)arg0.ToInt64()), 2),
                        TriggerStayCommand => DispatchTrigger(entityId, unchecked((ulong)arg0.ToInt64()), 1),
                        StartScriptCommand => StartScript(entityId),
                        DestroyScriptCommand => DestroyScript(entityId, arg0 == 0 ? 0u : ((NativeScriptSlot*)arg0)->Slot),
                        _ => 0,
                    };
                }
                catch (Exception ex)
                {
                    Native.Log(2, $"Managed dispatch failure: {ex.Message}");
                    return 0;
                }
            }

            private static int Initialize(NativeApi* api)
            {
                // Vespera.NET itself is process-lifetime. Keep Time.ElapsedTime on the
                // same process/runtime clock across scene switches and game-assembly
                // reloads so animation restarts and timers can use a stable epoch.
                return Native.Initialize(api) ? 1 : 0;
            }

            private static Assembly LoadAssemblyUnlocked(AssemblyLoadContext context, string path)
            {
                // Read assemblies into memory so the game DLL/PDB are replaceable while the
                // runtime is alive. This supports the 0.5.x manual + automatic script reload loop.
                using var peFile = new FileStream(path, FileMode.Open, FileAccess.Read,
                    FileShare.ReadWrite | FileShare.Delete);
                using var pe = new MemoryStream();
                peFile.CopyTo(pe);
                pe.Position = 0;

                var pdbPath = Path.ChangeExtension(path, ".pdb");
                if (File.Exists(pdbPath))
                {
                    using var pdbFile = new FileStream(pdbPath, FileMode.Open, FileAccess.Read,
                        FileShare.ReadWrite | FileShare.Delete);
                    using var pdb = new MemoryStream();
                    pdbFile.CopyTo(pdb);
                    pdb.Position = 0;
                    return context.LoadFromStream(pe, pdb);
                }
                return context.LoadFromStream(pe);
            }

            [MethodImpl(MethodImplOptions.NoInlining)]
            private static WeakReference? ReleaseGameAssembly()
            {
                foreach (var instance in Components)
                {
                    if (instance.Active)
                    {
                        try { Trace(instance, "OnDisable"); instance.Component.OnDisable(); }
                        catch (Exception ex) { Native.Log(2, FormatScriptException(instance, "OnDisable", ex)); }
                        instance.Active = false;
                    }
                    try
                    {
                        Trace(instance, "OnDestroy");
                        instance.Component.OnDestroy();
                    }
                    catch (Exception ex)
                    {
                        Native.Log(2, FormatScriptException(instance, "OnDestroy", ex));
                    }
                }
                Components.Clear();
                Audio.StopAllManagedSources();
                _gameAssembly = null;
                var context = _gameLoadContext;
                _gameLoadContext = null;
                if (context is null) return null;
                var weak = new WeakReference(context, trackResurrection: false);
                context.Unload();
                return weak;
            }

            private static void VerifyCollectibleUnload(WeakReference? weak)
            {
                if (weak is null) return;
                for (var attempt = 0; weak.IsAlive && attempt < 6; ++attempt)
                {
                    GC.Collect();
                    GC.WaitForPendingFinalizers();
                    GC.Collect();
                }
                if (weak.IsAlive)
                    Native.Log(1, "Previous C# AssemblyLoadContext is still alive; check static events/tasks/GCHandles before relying on repeated reloads.");
            }

            [MethodImpl(MethodImplOptions.NoInlining)]
            private static WeakReference? ReleaseCandidateAssembly()
            {
                _candidateGameAssembly = null;
                var context = _candidateGameLoadContext;
                _candidateGameLoadContext = null;
                if (context is null) return null;
                var weak = new WeakReference(context, trackResurrection: false);
                context.Unload();
                return weak;
            }

            private static int LoadGameAssembly(byte* path)
            {
                var text = Marshal.PtrToStringUTF8((nint)path);
                if (string.IsNullOrWhiteSpace(text)) return 0;

                // 0.5.5 makes replacement a real two-phase transaction. Loading here only
                // prepares a collectible candidate. Native code preflights every enabled
                // scene attachment against it before CommitCandidateAssembly is allowed to
                // invoke OnDestroy() or release the currently running scripts.
                var staleCandidate = ReleaseCandidateAssembly();
                VerifyCollectibleUnload(staleCandidate);

                var fullPath = Path.GetFullPath(text);
                GameLoadContext? candidateContext = null;
                try
                {
                    candidateContext = new GameLoadContext(fullPath);
                    var candidateAssembly = LoadAssemblyUnlocked(candidateContext, fullPath);
                    _candidateGameLoadContext = candidateContext;
                    _candidateGameAssembly = candidateAssembly;
                    candidateContext = null;
                    Native.Log(0, $"Prepared managed game assembly candidate: {_candidateGameAssembly.GetName().Name}");
                    return 1;
                }
                catch (Exception ex)
                {
                    if (candidateContext is not null)
                    {
                        var weak = new WeakReference(candidateContext, trackResurrection: false);
                        candidateContext.Unload();
                        candidateContext = null;
                        VerifyCollectibleUnload(weak);
                    }
                    Native.Log(2, $"Replacement C# assembly could not be prepared; current scripts remain active: {ex.GetType().Name}: {ex.Message}");
                    return 0;
                }
            }

            private static int ValidateCandidateScript(byte* className)
            {
                if (_candidateGameAssembly is null) return 0;
                var name = Marshal.PtrToStringUTF8((nint)className);
                if (string.IsNullOrWhiteSpace(name)) return 0;

                var type = _candidateGameAssembly.GetType(name, throwOnError: false, ignoreCase: false);
                if (type is null || type.IsAbstract || type.ContainsGenericParameters || !typeof(Component).IsAssignableFrom(type))
                {
                    Native.Log(2, $"Replacement C# assembly is incompatible with the loaded scene: component type '{name}' is missing or invalid.");
                    return 0;
                }

                var constructor = type.GetConstructor(
                    BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic,
                    null, Type.EmptyTypes, null);
                if (constructor is null)
                {
                    Native.Log(2, $"Replacement C# assembly is incompatible with the loaded scene: component '{name}' has no parameterless constructor.");
                    return 0;
                }
                return 1;
            }

            private static int CommitCandidateAssembly()
            {
                if (_candidateGameAssembly is null || _candidateGameLoadContext is null) return 0;

                var nextAssembly = _candidateGameAssembly;
                var nextContext = _candidateGameLoadContext;
                _candidateGameAssembly = null;
                _candidateGameLoadContext = null;

                var previousContext = ReleaseGameAssembly();
                _gameAssembly = nextAssembly;
                _gameLoadContext = nextContext;
                VerifyCollectibleUnload(previousContext);
                Native.Log(0, $"Committed managed game assembly: {_gameAssembly.GetName().Name}");
                return 1;
            }

            private static int DiscardCandidateAssembly()
            {
                var candidate = ReleaseCandidateAssembly();
                VerifyCollectibleUnload(candidate);
                return 1;
            }

            private static int CreateScript(ulong entityId, byte* className, uint slotIndex)
            {
                if (_gameAssembly is null) return 0;
                var name = Marshal.PtrToStringUTF8((nint)className);
                if (string.IsNullOrWhiteSpace(name)) return 0;

                var type = _gameAssembly.GetType(name, throwOnError: false, ignoreCase: false);
                if (type is null || type.IsAbstract || !typeof(Component).IsAssignableFrom(type))
                {
                    Native.Log(2, $"C# component type not found or invalid: {name}");
                    return 0;
                }

                if (Activator.CreateInstance(type, nonPublic: true) is not Component component) return 0;
                component.Attach(entityId);
                Components.Add(new ScriptInstance
                {
                    Component = component,
                    EntityId = entityId,
                    SlotIndex = slotIndex,
                    TypeName = name,
                });
                Native.Log(0, $"Attached {name} to entity {entityId}");
                return Components.Count; // stable 1-based handle for this host lifetime
            }

            private static int ApplyField(ulong scriptHandle, byte* fieldName, byte* serializedValue)
            {
                if (scriptHandle == 0 || scriptHandle > (ulong)Components.Count) return 0;
                var instance = Components[(int)scriptHandle - 1];
                var component = instance.Component;
                var name = Marshal.PtrToStringUTF8((nint)fieldName);
                var text = Marshal.PtrToStringUTF8((nint)serializedValue) ?? string.Empty;
                if (string.IsNullOrWhiteSpace(name)) return 0;

                var flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
                var field = component.GetType().GetField(name, flags);
                if (field is null || field.GetCustomAttribute<ExposeAttribute>(inherit: true) is null)
                {
                    field = component.GetType().GetFields(flags).FirstOrDefault(candidate =>
                        candidate.GetCustomAttribute<ExposeAttribute>(inherit: true) is not null
                        && candidate.GetCustomAttributes<FormerlySerializedAsAttribute>(inherit: true)
                            .Any(alias => string.Equals(alias.Name, name, StringComparison.Ordinal)));
                    if (field is not null)
                        Native.Log(1, $"{component.GetType().FullName}: applying legacy serialized field '{name}' to renamed field '{field.Name}'");
                }
                if (field is null || field.GetCustomAttribute<ExposeAttribute>(inherit: true) is null)
                {
                    Native.Log(2, $"{component.GetType().FullName}: serialized field '{name}' is missing or is not marked [Expose]");
                    return 0;
                }

                if (!TryParseExposedValue(field.FieldType, text, out var value))
                {
                    Native.Log(2, $"{component.GetType().FullName}.{name}: could not parse serialized value '{text}' as {field.FieldType.Name}");
                    return 0;
                }

                field.SetValue(component, value);
                return 1;
            }

            private static bool TryParseExposedValue(Type type, string text, out object? value)
            {
                var culture = CultureInfo.InvariantCulture;
                if (type == typeof(string)) { value = text; return true; }
                if (type == typeof(bool))
                {
                    if (bool.TryParse(text, out var b)) { value = b; return true; }
                    if (text == "1") { value = true; return true; }
                    if (text == "0") { value = false; return true; }
                }
                else if (type == typeof(int) && int.TryParse(text, NumberStyles.Integer, culture, out var i))
                {
                    value = i; return true;
                }
                else if (type == typeof(float) && float.TryParse(text, NumberStyles.Float, culture, out var f))
                {
                    value = f; return true;
                }
                else if (type.IsEnum)
                {
                    if (Enum.TryParse(type, text, false, out var parsed))
                    {
                        value = parsed; return true;
                    }
                }
                else if (type == typeof(Vector2))
                {
                    var parts = text.Split(new[] { ' ', ',', ';' }, StringSplitOptions.RemoveEmptyEntries);
                    if (parts.Length == 2
                        && float.TryParse(parts[0], NumberStyles.Float, culture, out var x)
                        && float.TryParse(parts[1], NumberStyles.Float, culture, out var y))
                    {
                        value = new Vector2(x, y); return true;
                    }
                }
                else if (type == typeof(Vector3))
                {
                    var parts = text.Split(new[] { ' ', ',', ';' }, StringSplitOptions.RemoveEmptyEntries);
                    if (parts.Length == 3
                        && float.TryParse(parts[0], NumberStyles.Float, culture, out var x)
                        && float.TryParse(parts[1], NumberStyles.Float, culture, out var y)
                        && float.TryParse(parts[2], NumberStyles.Float, culture, out var z))
                    {
                        value = new Vector3(x, y, z); return true;
                    }
                }
                else if (type == typeof(Color))
                {
                    var parts = text.Split(new[] { ' ', ',', ';' }, StringSplitOptions.RemoveEmptyEntries);
                    if ((parts.Length == 3 || parts.Length == 4)
                        && float.TryParse(parts[0], NumberStyles.Float, culture, out var r)
                        && float.TryParse(parts[1], NumberStyles.Float, culture, out var g)
                        && float.TryParse(parts[2], NumberStyles.Float, culture, out var b))
                    {
                        var a = 1.0f;
                        if (parts.Length == 4 && !float.TryParse(parts[3], NumberStyles.Float, culture, out a))
                        {
                            value = null; return false;
                        }
                        value = new Color(r, g, b, a); return true;
                    }
                }
                value = null;
                return false;
            }

            private static int StartScript(ulong scriptHandle)
            {
                if (scriptHandle == 0 || scriptHandle > (ulong)Components.Count) return 0;
                var instance = Components[(int)scriptHandle - 1];
                try
                {
                    Trace(instance, "Start");
                    instance.Component.Start();
                    if (instance.Component.Entity.Exists && instance.Component.Entity.Enabled)
                    {
                        Trace(instance, "OnEnable");
                        instance.Component.OnEnable();
                        instance.Active = true;
                    }
                    return 1;
                }
                catch (Exception ex)
                {
                    instance.Faulted = true;
                    Native.Log(2, FormatScriptException(instance, instance.Active ? "OnEnable" : "Start", ex));
                    return 0;
                }
            }

            private static int DestroyScript(ulong entityId, uint slotIndex)
            {
                for (var index = Components.Count - 1; index >= 0; --index)
                {
                    var instance = Components[index];
                    if (instance.EntityId != entityId || instance.SlotIndex != slotIndex) continue;
                    if (instance.Active)
                    {
                        try { Trace(instance, "OnDisable"); instance.Component.OnDisable(); }
                        catch (Exception ex) { Native.Log(2, FormatScriptException(instance, "OnDisable", ex)); }
                        instance.Active = false;
                    }
                    try
                    {
                        Trace(instance, "OnDestroy");
                        instance.Component.OnDestroy();
                    }
                    catch (Exception ex) { Native.Log(2, FormatScriptException(instance, "OnDestroy", ex)); }
                    Components.RemoveAt(index);
                    return 1;
                }
                return 0;
            }

            private static int StartAll()
            {
                foreach (var instance in Components)
                {
                    try
                    {
                        Trace(instance, "Start");
                        instance.Component.Start();
                        if (instance.Component.Entity.Exists && instance.Component.Entity.Enabled)
                        {
                            Trace(instance, "OnEnable");
                            instance.Component.OnEnable();
                            instance.Active = true;
                        }
                    }
                    catch (Exception ex)
                    {
                        instance.Faulted = true;
                        Native.Log(2, FormatScriptException(instance, instance.Active ? "OnEnable" : "Start", ex));
                    }
                }
                return 1;
            }

            private static int UpdateAll(float deltaTime)
            {
                Time.Advance(deltaTime);
                Audio.UpdateManagedSources();
                for (var index = Components.Count - 1; index >= 0; --index)
                {
                    var dead = Components[index];
                    if (dead.Component.Entity.Exists) continue;
                    if (dead.Active)
                    {
                        try { Trace(dead, "OnDisable"); dead.Component.OnDisable(); }
                        catch (Exception ex) { Native.Log(2, FormatScriptException(dead, "OnDisable", ex)); }
                        dead.Active = false;
                    }
                    try { Trace(dead, "OnDestroy"); dead.Component.OnDestroy(); }
                    catch (Exception ex) { Native.Log(2, FormatScriptException(dead, "OnDestroy", ex)); }
                    Components.RemoveAt(index);
                }
                foreach (var instance in Components)
                {
                    if (instance.Faulted) continue;
                    try
                    {
                        var enabled = instance.Component.Entity.Exists && instance.Component.Entity.Enabled;
                        if (enabled && !instance.Active)
                        {
                            Trace(instance, "OnEnable");
                            instance.Component.OnEnable();
                            instance.Active = true;
                        }
                        else if (!enabled && instance.Active)
                        {
                            Trace(instance, "OnDisable");
                            instance.Component.OnDisable();
                            instance.Active = false;
                        }
                        if (enabled) instance.Component.Update(deltaTime);
                    }
                    catch (Exception ex)
                    {
                        // Fault once instead of flooding the log every frame. Reloading scripts
                        // later will recreate the instance and give it a clean state.
                        instance.Faulted = true;
                        Native.Log(2, FormatScriptException(instance, "Update/lifecycle", ex));
                    }
                }
                return 1;
            }

            private static int DispatchTrigger(ulong receiverEntityId, ulong otherEntityId, int phase)
            {
                var other = new Entity(otherEntityId);
                foreach (var instance in Components)
                {
                    if (instance.Faulted || instance.EntityId != receiverEntityId) continue;
                    try
                    {
                        if (phase == 0) { Trace(instance, "OnTriggerEnter"); instance.Component.OnTriggerEnter(other); }
                        else if (phase == 1) { Trace(instance, "OnTriggerStay"); instance.Component.OnTriggerStay(other); }
                        else { Trace(instance, "OnTriggerExit"); instance.Component.OnTriggerExit(other); }
                    }
                    catch (Exception ex)
                    {
                        instance.Faulted = true;
                        var name = phase == 0 ? "OnTriggerEnter" : phase == 1 ? "OnTriggerStay" : "OnTriggerExit";
                        Native.Log(2, FormatScriptException(instance, name, ex));
                    }
                }
                return 1;
            }

            private static string FormatScriptException(ScriptInstance instance, string phase, Exception ex)
            {
                var detail = $"{instance.TypeName}.{phase} on entity {instance.EntityId} threw {ex.GetType().Name}: {ex.Message}";
                if (!string.IsNullOrWhiteSpace(ex.StackTrace)) detail += "\n" + ex.StackTrace;
                return detail;
            }

            private static int Shutdown()
            {
                var candidateContext = ReleaseCandidateAssembly();
                var previousContext = ReleaseGameAssembly();
                VerifyCollectibleUnload(candidateContext);
                VerifyCollectibleUnload(previousContext);
                Native.Reset();
                return 1;
            }
        }
    }
}
