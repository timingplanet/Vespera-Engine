# Scene format

## v15 Mesh Renderer Material references

Scene v15 adds an optional stable Material link immediately after a Mesh Renderer:

```text
mesh_renderer cube "Concrete Brick" 1 1 1 1
mesh_material_asset "19c98b1357c54ffe80fa708aef01caea" "materials/concrete_lit.slmat"
```

The stable ID is authoritative and the readable project-relative path is a fallback/repair aid. Material properties themselves live in `.slmat`; resolved shader/texture caches are transient and are never serialized into a scene. Scene v1-v14 remain load-compatible. Prefab v6 uses the same `mesh_material_asset` record while Prefab v1-v5 remain load-compatible.


## v14 entity hierarchy

Scene v14 adds an optional `parent <entity-id>` record inside entity blocks. Root entity transforms remain world-space; parented entity transforms are local to the referenced parent. Parent references are validated transactionally during load and cycles are rejected. Scene v1-v13 remain load-compatible.

## v13 primitive Mesh Renderer

Scene v13 adds the optional built-in primitive record:

```text
mesh_renderer "cube|plane|cylinder|sphere" "texture-name-or--" r g b a
```

Transform owns position/rotation/scale. v1-v12 remain load-compatible. Prefab v5 uses the same Mesh Renderer record.

# Vespera Scene Text (`.slscene`)

Current writer version: **15**. The loader accepts versions **1 through 15**.

```text
sectorline_scene 15
```

Version history:

- v1: camera, materials, sectors, vertices, sides/portals
- v2: static sprite actors
- v3: sprite animation clips and directional/animation state
- v4: stable sprite actor ids and enabled state
- v5: general Entities with required Transform and optional Sprite Renderer components
- v6: Cylinder Collider component records
- v7: Entity `identity` metadata (`tag`, `layer`)
- v8: optional project-relative `prefab_source` metadata on Entity instances
- v9: Point Light component records
- v10: first single managed C# Script record
- v11: multiple managed script attachments plus serialized `[Expose]` field overrides
- v12: stable-ID-first prefab source references with readable fallback paths

## Entity blocks (v5+)

Current scenes serialize scene objects as Entity blocks:

```text
entity 1002 1 "Chamber Watcher"
  identity "light" "Lighting"
  prefab_source_asset "3ea2605121c0ad8e0875f49b7b5c334c" "prefabs/watcher.slprefab"
  transform 1.7 0.60 8.30 0 3.14159265 0 1 1 1
  sprite_renderer 1.65 2.10 "Test Sprite" "Watcher Walk" 0.75 0.18 0 1.00 0.76 0.88 1.0
  cylinder_collider 0.48 2.10 0 1.05 0 0
endentity
```

The `entity` header stores stable non-zero object id, enabled flag, and name.

### Identity (v7+)

Every v7+ Entity has exactly one:

```text
identity "tag" "layer"
```

Both strings must be non-empty. Older scenes migrate to `"Untagged" "Default"` when identity metadata did not exist.

### Prefab source (v8+, stable form in v12)

Scene v12 writes prefab links using the same stable-ID-first/readable-fallback model as the rest of the 0.7.x asset pipeline:

```text
prefab_source_asset "<stable-id>" "prefabs/watcher.slprefab"
```

The stable `.vmeta` ID is authoritative. The fallback path remains project-relative and human-readable. Scene v8-v11 path-only records remain load-compatible:

```text
prefab_source "prefabs/watcher.slprefab"
```

The editor hydrates resolvable legacy path-only prefab links to stable IDs in memory, so a later save can migrate them to v12 without requiring a one-off converter. The runtime does not silently live-link instances; editor **Apply**, **Revert**, and **Unpack** remain explicit.

### Transform

Every v5+ Entity requires exactly one:

```text
transform posX posY posZ rotX rotY rotZ scaleX scaleY scaleZ
```

Rotations are radians. Scale axes must be non-zero.

### Sprite Renderer

Optional:

```text
sprite_renderer width height "Fallback Texture" "Clip Name" speed timeOffset paused tintR tintG tintB tintA
```

World position/facing/scale remain on Transform.

### Cylinder Collider (v6+)

Optional:

```text
cylinder_collider radius height centerX centerY centerZ isTrigger
```

Radius and height must be positive. `isTrigger` is `0` or `1`.

The current blocking movement helper remains X/Z-circle based. The 0.4.x 2.5D raycast uses collider height/Y center when deciding whether a constant-height ray can hit an entity collider.

### Point Light (v9+)

Optional:

```text
point_light r g b a intensity radius
```

Intensity must be non-negative and radius must be positive.

### C# Script attachments (v10+, field blocks in v11+)

Scene v10 supported one single-line C# Script record. Scene v11 supports an ordered list and explicit serialized field overrides:

```text
managed_script "ReferenceGame.Scripts.ManagedSpinner" 1
  managed_field "RadiansPerSecond" "float" "0.65"
end_managed_script
```

The script record stores the full managed type name and enabled flag. `managed_field` stores semantic field name, canonical type name, and serialized value. Only fields marked `[Expose]` are applied by the managed host. v11 data is applied after the Entity handle is bound and before `Start()`. Multiple script blocks may appear on one Entity. Missing or temporarily unresolved script/field data is preserved rather than silently removed.

## Entity prefab text (`.slprefab`)

0.6.3 writes Entity prefab format v4; the loader remains backward-compatible with v1 through v3:

```text
sectorline_prefab 4
name "Lamp"
enabled 1
identity "enemy" "Actors"
transform 0 0 0 0 3.14159265 0 1 1 1
point_light 1.0 0.72 0.42 1.0 1.35 5.0
managed_script "Game.LampPulse" 1
  managed_field "PulseSpeed" "float" "1.25"
end_managed_script
end_prefab
```

Prefab v4 represents one Entity with the same current native component data plus an ordered managed-script attachment list and exposed-field overrides. The loader still accepts v1-v3 prefabs; older versions simply cannot contain records introduced later. Editor-created prefab roots are normalized to local position `(0,0,0)` so scene placement remains instance data. It intentionally does not contain a scene object id; instantiation always allocates a fresh stable id.

Texture references are stored by stable texture name. Sprite animation clip references resolve against the target Scene's clip resources. Nested/multi-entity prefabs and property-level override tracking are later extensions rather than assumptions baked into the current format.

## Sprite clips

Sprite clips remain scene resources:

```text
sprite_clip "Watcher Walk" 8 2 4.0 1 "D0F0" "D0F1" ...
```

Fields are name, direction count (1/4/8), frame count, FPS, loop flag, then direction-major texture references.

## Migration

- v1 contains no sprite objects and loads normally.
- v2-v3 sprite records receive stable ids during migration.
- v4 sprite records already contain id/enabled state.
- v2-v4 sprites migrate to Entity + Transform + Sprite Renderer.
- v5 adds Entity/component blocks.
- v6 adds Cylinder Collider.
- v1-v6 entities receive default `Untagged` / `Default` identity when loaded into the current model.
- v1-v7 entities receive an empty prefab source.
- v1-v8 entities have no Point Light records.
- v1-v9 entities have no C# Script records.
- v10 C# Script records migrate into the new attachment list with no field overrides.

Saving any successfully loaded older scene writes current v11 format.

Loading remains transactional: the live Scene is replaced only after the entire parsed document and cross-references pass validation.

Texture bytes are not embedded in `.slscene` or `.slprefab`; records reference textures registered by the project/resource layer.
