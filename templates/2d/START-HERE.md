# Vespera 2D / UI Foundation (Experimental)

This starter is intentionally different from the 3D / 2.5D template.

Vespera 1.0 does **not** claim a complete orthographic world/editor workflow. Instead, this template provides a practical screen-space 2D foundation for UI-heavy games, card games, puzzle games, menus, HUD-driven games, and prototypes:

- `assets/ui/main.rml` is the 2D canvas.
- `assets/ui/theme.rcss` owns layout and presentation.
- `managed/StarterGame.cs` owns the game loop through Vespera's public C# API.
- `Input.Value(...)` reads the project's action map.
- `UiElement.SetProperty(...)` moves game elements without native engine code.
- Editor Play and exported games use the same project-owned script and shared player.

## Try it

Press **Play**, then move the blue player with **WASD** or the **arrow keys**. Touch the gold target to score. The Reset button is a normal RmlUi button consumed from C#.

## Make it yours

Replace `#player` and `#goal` with your own RmlUi content. For example, use image elements, cards, panels, text, health bars, inventory slots, dialogue boxes, or a board/grid. Keep game state in project C# and use RmlUi for the screen-space presentation.

## Current 1.0 boundary

This is a **2D / UI foundation**, not a hidden 3D workaround presented as a finished 2D engine mode. Dedicated orthographic world rendering, 2D scene gizmos, and a full 2D physics/editor workflow remain post-1.0 work.
