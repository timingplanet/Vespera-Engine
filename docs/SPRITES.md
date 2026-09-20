# Sprites & Animation

Vespera supports billboard-style sprite rendering alongside mesh content. A Sprite Renderer controls size, tint, animation clip, speed, time offset, and paused state.

## Create a sprite entity

Use **Create Sprite Entity** from the editor. Select it and configure the Sprite Renderer in the Inspector.

Sprite animation can use authored sprite clips. The editor can create and edit Sprite Clip assets/resources, and the Asset Catalog recognizes `.slspriteclip` and `.slspritesheet` files.

## C# control

```csharp
var sprite = Entity.SpriteRenderer;
if (sprite is not null)
{
    sprite.Size = new Vector2(1.5f, 2.0f);
    sprite.Tint = new Color(1, 1, 1, 1);
    sprite.Play("Run", speed: 1.25f);
}
```

Animation helpers include:

```csharp
sprite.Pause();
sprite.Resume();
sprite.Restart();
sprite.Stop();
```

`Play` sets the clip and speed and can restart animation time.

## Sprite sheets

A `.slspritesheet` descriptor references a source texture. Keep the texture and sheet in the Asset Catalog so the dependency system can package them together.

## Scene resources

Scene Text can also contain sprite clip resources referenced by Sprite Renderer components. When working with those clips, treat their names as scene-local animation resources and keep names unique enough to be clear in the editor.

## Moving sprite assets

Sprite sheets and clips are dependency-aware catalog assets. Use controlled Move / Rename operations for referenced assets so stable identities and fallback paths can be repaired.
