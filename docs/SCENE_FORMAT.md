# Scene Text Format

`.slscene` is a human-readable serialized scene format. You normally edit scenes through the Vespera Editor; this page exists so tools and advanced users can understand what is on disk.

## Header

A current scene begins with:

```text
sectorline_scene 15
```

The number is the **scene schema version**, not a release-history lesson. Loaders use it to parse the file correctly.

## What a scene stores

Scene Text can contain:

- scene/world and sector data;
- stable entity IDs;
- entity names, enabled state, tags, layers, and parent relationships;
- Transform data;
- built-in component blocks;
- prefab source references;
- C# script attachments and exposed-field overrides;
- scene-owned sprite clip resources;
- stable asset references plus fallback paths where applicable.

## Stable IDs

Scene entity IDs are stable within authored scene state. Runtime-created entities get their own IDs. Prefabs intentionally do not force a scene ID onto an instance; instantiation allocates a fresh one.

## Asset references

Vespera serialized references generally prefer a stable asset ID with a project-relative fallback path. Do not strip the ID simply because the path looks readable—the ID is what allows a controlled move to preserve identity.

## Transactional loading

Scene loading validates the parsed content before replacing the live scene. Invalid cross-references or malformed structure should fail the load rather than partially replacing good live state.

## Should I edit `.slscene` by hand?

Usually, no. The editor provides validation, undo/redo, stable-reference repair, prefab workflows, and component-aware authoring.

Hand editing is appropriate when:

- building your own tooling against the documented format;
- investigating a minimal parser problem;
- repairing a copy after you understand the exact invalid record.

Keep a backup before doing it.
