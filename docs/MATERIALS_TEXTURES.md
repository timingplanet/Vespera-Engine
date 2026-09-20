# Textures & Materials

Textures are imported source assets. Materials are authored `.slmat` assets that can reference textures and provide rendering properties to mesh content.

## Texture sources

The Asset Catalog recognizes:

- PNG
- JPEG (`.jpg`, `.jpeg`)
- BMP
- TGA

The texture importer directly handles BMP and routes PNG/JPEG through the image import path. Texture source intent/import settings are stored in the texture's `.vmeta` sidecar.

Select a texture in Project / Assets to inspect its import settings. Refresh the catalog after replacing a texture file externally.

## Materials

Create a material with **Create → Create Material Asset**. The material appears as a `.slmat` asset.

A material can hold a stable reference to its base texture. Assign materials in the Inspector by selecting or dropping the material asset onto a Mesh Renderer.

From C#:

```csharp
var renderer = Entity.EnsureMeshRenderer();
renderer.SetMaterial("materials/stone.slmat");
renderer.Tint = new Color(1.0f, 0.9f, 0.85f, 1.0f);
```

Or use a stable `AssetReference`:

```csharp
renderer.Material = Assets.FromPath("materials/stone.slmat");
```

## Moving textures or materials

Materials participate in stable reference repair. Move referenced textures/materials through Project / Assets so the catalog can retain identity and rewrite fallback paths.

## Practical workflow

1. Add a texture under `assets/`.
2. Refresh the Asset Catalog if needed.
3. Create a `.slmat` material.
4. Assign the texture to the material in the Inspector.
5. Assign the material to a Mesh Renderer.
6. Save the scene and test in Play Mode.
