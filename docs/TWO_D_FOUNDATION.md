# 2D / UI Foundation

The **2D / UI Foundation (Experimental)** starter is a real screen-space game template built from RmlUi plus project-owned C# logic.

It is best suited to:

- card games;
- board/grid games;
- puzzle games;
- menu-heavy games;
- HUD-driven prototypes;
- other games whose main playfield can be represented as screen-space UI.

## How the starter works

The template contains:

```text
assets/ui/main.rml     # playfield and HUD
assets/ui/theme.rcss   # layout and appearance
managed/StarterGame.cs # movement, scoring, reset, game state
```

The starter maps WASD and arrow keys to `move_horizontal` / `move_vertical`. C# reads those actions and moves RmlUi elements with `UiElement.SetProperty(...)`.

## A useful pattern

```csharp
var x = Input.Value("move_horizontal");
var y = Input.Value("move_vertical");

_player.SetProperty("left", $"{playerX:0.0}px");
_player.SetProperty("top", $"{playerY:0.0}px");
```

Buttons are ordinary RmlUi elements:

```csharp
if (_reset?.Clicked ?? false)
    ResetGame();
```

## Replace the starter visuals

The blue player square and gold goal are just RmlUi content. Replace them with image elements, cards, panels, inventory slots, board cells, dialogue, or your own styled widgets.

Keep game state and rules in C#. Use RML/RCSS as the presentation layer.

## What this template is not

This is **not** a full orthographic world-authoring mode. Vespera does not currently provide a dedicated 2D scene viewport, 2D world gizmo workflow, tilemap system, or a complete 2D physics stack.

If your game needs authored world-space 2D rather than screen-space UI, treat that as a current engine limitation rather than trying to force the UI starter into a job it was not designed for.
