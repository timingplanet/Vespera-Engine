# Lua Scripting

Lua is an optional **project-level** runtime scripting path. It is useful for lightweight game/mod logic that does not need the full per-entity C# component model.

C# and Lua can coexist in the same project.

## Configure the entry script

Place a `.lua` file under the project asset root, select it in Project / Assets, then use **Project Settings → Lua Entry Script → Use Selected Lua**.

A Lua entry can define:

```lua
function Start()
    Vespera.Log.info("Lua started")
end

function Update(dt)
    -- per-frame logic
end

function Stop()
    Vespera.Log.info("Lua stopped")
end
```

## Input

```lua
if Vespera.Input.pressed("probe") then
    Vespera.Log.info("probe pressed")
end
```

Lua uses the same named project input actions as C# and standalone Play.

## Scene and entities

A source-tree example uses the semantic runtime API like this:

```lua
local player = Vespera.Scene.find_tag("player")
if player then
    local p = Vespera.Entity.position(player)
    Vespera.Log.info(string.format("player %.2f %.2f %.2f", p.x, p.y, p.z))
end
```

Use the Vespera-provided scene/entity/UI/asset/audio/input surface rather than assuming access to native engine internals.

## Runtime restrictions

The Lua environment is intentionally constrained for project-level gameplay. It does not expose unrestricted filesystem/process/debug facilities such as `io`, `os`, `debug`, `package`, `dofile`, or `loadfile`.

This means Lua files should use project assets and Vespera's semantic APIs rather than building their own file/module loader.

## When to choose C# instead

Use C# when you need:

- per-entity components in the Inspector;
- exposed fields;
- trigger lifecycle callbacks;
- richer typed gameplay code;
- reusable component classes attached to many entities.

Use Lua when a single lightweight project runtime script is enough.
