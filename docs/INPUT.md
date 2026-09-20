# Input

Vespera uses **named project actions**. Gameplay code asks for `move_forward`, `jump`, `interact`, or another action name; the project decides which keyboard or gamepad controls feed that action.

This keeps gameplay code independent from a specific key layout.

## Author bindings

Open **Project Settings → Input Actions**.

Each binding has:

- **Action** — the name used by gameplay code.
- **Device** — for example `key`, `gamepad_button`, or `gamepad_axis`.
- **Code** — the key/button/axis code.
- **Scale** — multiplies the input value. Use `-1` for the opposite direction of an axis.
- **Deadzone** — useful for analog axes.

The editor provides buttons for adding Key, Gamepad Button, and Gamepad Axis bindings.

A movement action can have more than one binding. The 3D starter, for example, maps W/S to opposite values of one forward action and D/A to opposite values of one right action.

## Read actions from C#

```csharp
var horizontal = Input.Value("move_right");

if (Input.Down("sprint"))
    speed *= 1.8f;

if (Input.Pressed("interact"))
    Interact();

if (Input.Released("interact"))
    Log.Info("Interact released");
```

### Which method should I use?

| Method | Use it for |
|---|---|
| `Input.Value(action)` | Axes, analog input, positive/negative movement. |
| `Input.Down(action)` | Continuous held state. |
| `Input.Pressed(action)` | One action on the press transition. |
| `Input.Released(action)` | One action on the release transition. |

## Lua input

The project-level Lua API exposes the same action concept. For example:

```lua
if Vespera.Input.pressed("probe") then
    Vespera.Log.info("probe pressed")
end
```

## Play Mode and standalone

Project bindings drive both in-editor Play Mode and the standalone runtime. If input works in one but not the other, first verify that you are testing the same project settings and packaged project file.

## Common mistakes

- Calling an action name that is not bound in Project Settings.
- Giving positive and negative directional bindings the same scale.
- Forgetting a deadzone on a noisy gamepad axis.
- Reading `Pressed` when you meant “held every frame”; use `Down` for that.
