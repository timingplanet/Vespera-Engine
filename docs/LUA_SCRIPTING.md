# Lua scripting

Vespera 0.10.0b embeds Lua 5.4.9 as the lightweight runtime/mod scripting path. C# remains the primary full-game scripting surface. Lua deliberately reuses Vespera's existing gameplay semantics instead of defining a parallel engine API.

## Project entry script

Project Text v9 adds one optional stable Lua entry reference:

```text
lua_entry_asset "<stable-id>" "scripts/game.lua"
```

The entry is an automatic build root, participates in stable-ID move repair, and is protected by safe-delete preflight. It can be selected or cleared from Project Settings or through the existing project-setting automation surface.

When configured, the same entry runs in Editor Play and the shared `vespera_player`. A project may use C#, Lua, both, or neither.

## Lifecycle

The entry script may define any of these globals:

```lua
function Start()
    Vespera.Log.info("Lua started")
end

function Update(dt)
end

function Stop()
end
```

Missing lifecycle functions are valid. Runtime errors from `Update` disable further updates until the script is reloaded, preventing a failing script from flooding the frame log.

## API surface

The global `Vespera` table currently exposes:

- `Vespera.version`
- `Vespera.Log.info/warn/error`
- `Vespera.Input.value/down/pressed/released`
- `Vespera.Scene.find/find_tag/current/load/reload`
- `Vespera.Entity.exists/name/set_name/enabled/set_enabled`
- `Vespera.Entity.position/set_position/rotation/set_rotation/scale/set_scale`
- `Vespera.Entity.get_property/set_property`
- `Vespera.UI.exists/text/set_text/value/set_value/set_visible/set_disabled/set_property/set_class/click/consume_clicks`
- `Vespera.Assets.resolve`
- `Vespera.Audio.play/stop`

Generic entity component-property access uses the same stable component keys and property metadata as C#. Vector2 values are exposed to Lua as `{x=..., y=...}` even though some native 2.5D internals store the second axis as Z.

Example:

```lua
function Update(dt)
    if Vespera.Input.pressed("probe") then
        local player = Vespera.Scene.find_tag("player")
        if player then
            local p = Vespera.Entity.position(player)
            Vespera.Log.info(string.format("player %.2f %.2f %.2f", p.x, p.y, p.z))
        end
    end
end
```

## Runtime safety boundary

The default Lua state opens only the base, table, string, math, UTF-8, and coroutine libraries. Vespera does not expose the standard `io`, `os`, `debug`, or `package` libraries, and removes `dofile` and `loadfile` from the base environment. The engine itself loads the configured project entry file.

This is a deliberate lightweight runtime/mod boundary, not a general host-filesystem scripting shell.

## Current boundary

0.10.0b does **not** add per-entity Lua script components, Lua-specific trigger callbacks, a `require`/module loader, or a second Lua-only gameplay event bus. C# remains the richer component/lifecycle path. Lua will grow only where real project workflows need lightweight runtime scripting without duplicating the managed API.
