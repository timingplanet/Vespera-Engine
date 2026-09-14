# Input system

Vespera has one semantic `InputSystem` / `InputMap` shared by native gameplay and C#. SDL3 remains the platform implementation detail; normal gameplay should address named actions instead of SDL keys directly.

## Project v5 authored bindings

0.8.7 moves the reference project's action bindings into `.vesperaproject` data:

```text
vespera_project 5
...
input_bind "move_forward" "key" "W" 1.0 0.0
input_bind "move_forward" "key" "S" -1.0 0.0
input_bind "move_forward" "gamepad_axis" "LeftY" -1.0 0.18
input_bind "move_right" "key" "D" 1.0 0.0
input_bind "look_x" "gamepad_axis" "RightX" 1.0 0.16
input_bind "sprint" "gamepad_button" "LeftStick" 1.0 0.0
```

Record shape:

```text
input_bind "<action>" "<device>" "<code>" <scale> <deadzone>
```

Current device strings are `key`, `gamepad_button`, and `gamepad_axis`. The loader validates semantic code names and deadzones, while Project v1-v4 remain load-compatible and simply have no authored bindings until saved/upgraded.

`configure_project_input_map(project, map)` rebuilds an `InputMap` from these records. Both the standalone reference runtime and editor Play use this function, eliminating their previous duplicate hardcoded mapping setup.

## Named actions

```cpp
float movement = context.input.action_value("move_forward");
bool sprinting = context.input.action_down("sprint");
if (context.input.action_pressed("probe")) { /* ... */ }
```

Bindings contribute scaled values and the final action value is clamped to `[-1, 1]`. Available action queries are `action_value`, `action_down`, `action_pressed`, and `action_released`.

C# reads the same map:

```csharp
float move = Input.Value("move_forward");
bool sprinting = Input.Down("sprint");
if (Input.Pressed("probe"))
    Log.Info("Probe pressed from C#");
```

C# does not own a second input map.

## Alternate runtime hosts

0.8.7 adds a semantic host-feed layer to `InputSystem` (`host_begin_frame`, `host_set_key`, `host_add_mouse_delta`, and gamepad feed functions). `Application` and editor Play can therefore drive the same public input/action model without exposing internal state arrays.

The editor Game view currently feeds keyboard state plus SDL3 relative mouse deltas while it owns input capture. The standalone runtime continues to feed SDL3 gamepad hot-plug/button/axis state as before. Project v5 gamepad records are therefore already portable data even though direct controller feed into the editor Game view remains a later 0.8 polish item.

## Mouse

Mouse look uses raw relative per-frame deltas rather than normalized action axes. Mouse motion is an unbounded delta while a stick is a normalized continuous value, so keeping those representations separate avoids conflating sensitivity/deadzone semantics. The Game view uses SDL3 window-relative mouse mode while captured; Escape releases capture.

## Current reference actions

The reference project authors 11 named actions across 16 bindings: movement, look, sprint, probe, scene next/previous, one-shot/spatial audio tests and lifecycle test. Space remains a physical-key manual C# reload fallback in both standalone and editor Play.

## Later input work

- runtime rebinding UI
- editor Game-view gamepad feed
- multiple local players/controllers
- rumble
- mouse buttons/wheel action bindings
- touch / Steam Input integration
