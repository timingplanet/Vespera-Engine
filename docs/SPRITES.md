# Sprite rendering - 0.5.0

Vespera remains sprite-first: characters, enemies, pickups, decorations, and effects should be able to remain sprite-based without requiring 3D meshes.

Starting in 0.3.0, sprites are no longer special standalone scene objects. They are rendered by a **Sprite Renderer component attached to a normal Entity**.

## Entity + Transform + Sprite Renderer

Every Entity has a required Transform containing:

- world position
- XYZ rotation
- XYZ scale

An optional `SpriteRendererComponent` contains:

- world-space base width/height
- static/fallback texture
- optional animation clip name
- animation speed multiplier
- animation time offset
- paused state
- RGBA tint

Directional actor facing comes from **Transform Y rotation**. The renderer still draws an upright cylindrical billboard: the quad rotates around world Y to face the camera horizontally while remaining upright regardless of camera pitch.

Billboard orientation and entity facing are separate concepts. The quad faces the camera; Transform Y rotation tells the directional-frame resolver which side of the entity the camera should see.

Transform scale multiplies the Sprite Renderer base size.

## Animation clips

`SpriteAnimationClip` remains a scene resource with:

- name
- direction count: 1, 4, or 8
- frame count per direction
- frames per second
- loop/one-shot behavior
- texture reference for each direction/frame pair

Frames are stored direction-major. For an 8-direction, 2-frame clip:

```text
D0F0 D0F1 D1F0 D1F1 ... D7F0 D7F1
```

Direction 0 is the entity front. Additional indices rotate clockwise when viewed from above.

## Renderer-independent resolution

`resolve_sprite_frame()` lives outside the D3D12 backend. It receives the Scene, Entity, Sprite Renderer component, and runtime time and returns:

- resolved texture
- direction index
- animation frame index
- whether a clip was actually used

This keeps D3D12, future Vulkan, editor previews, tests, and gameplay tooling on one directional/animation rule set.

## Depth and alpha

Sprite quads use the same depth target as sector geometry. Transparent texels are alpha-cutout with a 0.5 threshold; opaque pixels participate in normal depth testing.

Smooth alpha blending and sorted translucency are later concerns.

## Editor authoring

The Entity Inspector exposes the required Transform and lets the user add/remove Sprite Renderer. Sprite Renderer fields expose size, fallback texture, clip, animation timing, pause, and tint.

Sprite clips remain directly authorable from Project / Assets with create/duplicate/delete/rename, 1/4/8 directional layouts, frame counts, FPS/looping, and per-frame texture assignment.

Disabled Entities remain authorable in the editor but are skipped by runtime sprite rendering.

## Reference entities

The 0.4.5 reference project keeps the procedural 8-direction × 2-frame `Watcher Walk` test art on two sprite-rendered Entities. A third `Gameplay Marker` Entity has Transform only and intentionally produces no rendered sprite.


0.4.5 also ships `assets/prefabs/watcher.slprefab`; the reference runtime loads and instantiates an additional watcher through the public prefab API to ensure directional sprite components survive external prefab serialization.
