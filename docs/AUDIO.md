# Audio

Vespera's Asset Catalog recognizes WAV, OGG, and MP3 source files. The authored `.slaudio` clip descriptor currently resolves a WAV source asset.

## Add audio to the project

Place audio beneath the project's asset root and refresh the Asset Catalog if it was added externally. For an authored clip descriptor, create/configure a `.slaudio` that points at the source asset.

## Play a one-shot

```csharp
Audio.PlayOneShot("audio/click.wav", 0.8f);
```

With an `AssetReference`:

```csharp
var click = Assets.FromPath("audio/click.wav");
Audio.PlayOneShot(click, 0.8f);
```

## Play and control a voice

```csharp
var music = Audio.Play("audio/music.wav", volume: 0.6f, loop: true);

// Later:
music?.Stop();
```

`Audio.MasterVolume` controls the global level, and `Audio.StopAll()` stops active managed voices.

## Spatial audio

```csharp
var source = Audio.PlaySpatial(
    "audio/hum.wav",
    Entity.Position,
    volume: 1.0f,
    loop: true,
    minDistance: 1.0f,
    maxDistance: 18.0f);

source?.Follow(Entity);
```

A managed `AudioSource` can update its position or follow an entity. Stop the source when the effect should end.

## Listener

Use the camera as the listener:

```csharp
AudioListener.UseCamera();
```

Or assign `AudioListener.Position` directly for a custom listener.

## Packaging

Audio participates in the Asset Catalog and dependency closure. If a sound is loaded only from code and no serialized dependency points to it, add it as an explicit build include so it cannot be omitted from the package.
