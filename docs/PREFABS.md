# Prefabs

A prefab is a reusable entity asset stored as `.slprefab`. Use prefabs for objects you want to place repeatedly or spawn at runtime: enemies, pickups, props, lights, triggers, reusable gameplay objects, and similar content.

## Create a prefab

Select an entity and use **Create Prefab from Selected**. Choose a location under the project's asset root.

The prefab stores the entity's component/script state as reusable source content. The prefab does not own the scene object's ID; every instantiated copy receives a fresh stable entity ID.

## Instantiate in the editor

Find the prefab in Project / Assets and choose **Instantiate Prefab**, or use the normal asset/editor placement flow. Instances remember their prefab source until unpacked.

## Apply, Revert, and Unpack

For a prefab-backed entity:

- **Apply to Prefab** writes the current instance state back to its prefab source.
- **Revert from Prefab** reloads the prefab's source state into the instance.
- **Unpack Prefab** removes the prefab-source relationship while leaving the entity in the scene.

Use these intentionally. Apply changes the reusable source; Revert discards local instance edits in favor of that source.

## Spawn from C#

```csharp
var enemy = Prefab.Instantiate(
    "prefabs/enemy.slprefab",
    new Vector3(5, 0, 3),
    "Enemy");

if (enemy is not null)
    enemy.Tag = "enemy";
```

You can also instantiate from an `AssetReference`.

## Moving prefab assets

Prefabs participate in the stable asset-reference system. If scenes or other assets reference a prefab, use **Move / Rename…** from the Project browser so Vespera can preserve the asset ID and repair fallback paths.

Before deleting a referenced prefab, run the editor's delete-safety check. A stable reference surviving a move is preferable to discovering a broken package at build time.
