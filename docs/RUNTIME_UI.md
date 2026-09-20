# Runtime UI API

The managed UI API gives gameplay code a stable semantic interface over the active UI document.

## Find an element

```csharp
var score = UI.Find("score");
var required = UI.FindRequired("health-bar");
```

`Find` returns `null` when the element is unavailable. `FindRequired` throws with a clear message, which is useful when an element is mandatory for the game to function.

## Common properties

```csharp
var panel = UI.FindRequired("pause-panel");
panel.Visible = true;
panel.Interactable = true;
panel.Text = "Paused";
```

`Visible` is friendly terminology over the UI enabled state.

Form-capable elements expose:

```csharp
field.ValueText = "Player";
slider.Value = 0.75f;
field.Focused = true;
field.ReadOnly = false;
```

## Click handling

```csharp
if (UI.Find("continue")?.Clicked ?? false)
    ContinueGame();
```

A click is consumed once when polled.

## RmlUi styling

For RmlUi-backed elements:

```csharp
element.SetProperty("left", "120px");
element.SetProperty("top", "48px");
element.SetClass("selected", true);
```

These operations intentionally return a boolean because CSS-style mutation is not supported by every legacy UI node.

## Color and asset helpers

`UiElement` includes semantic helpers for color state, progress colors, images, and fonts:

```csharp
element.SetTextColor(new Color(1, 0.8f, 0.2f, 1));
element.SetImage(Assets.FromPath("textures/icon.png"));
```

## Checking existence

A retained `UiElement` can become unavailable after UI or scene changes. Check `Exists` when holding references across those boundaries.
