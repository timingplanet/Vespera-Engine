# Asset Pipeline

Vespera indexes supported files beneath the project's asset root into an **Asset Catalog**. Catalog entries have a type, project-relative path, stable asset ID, and importer metadata.

## Stable asset IDs

The `.vmeta` file beside an asset stores its stable identity. Serialized Vespera assets can store both an ID and a fallback path. The ID is authoritative when it resolves; the path helps repair or diagnose references.

This matters because paths change. If you move `textures/door.png` to `art/doors/door.png` through Vespera's controlled authoring workflow, the asset can retain the same ID while fallback paths are rewritten where supported.

## Import and refresh

Adding a supported file under the asset root makes it eligible for catalog discovery. Use **Refresh Asset Catalog** after adding or changing files outside the editor.

Texture metadata includes importer settings stored in `.vmeta`. Other authored descriptor files such as materials, sprite clips, sprite sheets, audio clips, scenes, prefabs, RML/RCSS, and Lua scripts are also catalogued.

## Move and rename

In Project / Assets, use **Move / Rename…** on a catalogued asset. The authoring layer can update known references across Vespera serialized assets and RML/RCSS path references.

Avoid moving referenced assets in Explorer unless you intentionally plan to refresh and repair them afterward.

## Delete safety

Use **Check Delete Safety** before deleting an asset. The dependency scanner can report known references that make deletion unsafe.

Dependency checks are especially useful for chains such as:

```text
Texture → Material → Prefab → Scene
RML → RCSS / images / fonts
Audio source → .slaudio
Sprite sheet → source texture
```

## Build closure

Build Game does not blindly copy the entire project. The packager computes required project content starting from startup assets and dependencies, plus explicit build includes configured by the project.

If gameplay code loads an asset dynamically in a way dependency scanning cannot infer, add that asset as an explicit build root instead of relying on it to be discovered accidentally.

<div class="notice"><strong>Rule of thumb:</strong> treat asset IDs as identity and paths as location. Keep `.vmeta` with its asset, use controlled moves, and validate the package when adding dynamic loading.</div>
