# Vespera Runtime UI — RmlUi production foundation

## Decision

The 0.9.7 Windows evaluation succeeded. Vespera 0.9.8 adopts **RmlUi 6.2 + FreeType 2.14.1** as its low-level production UI foundation.

This does **not** mean game code becomes coupled directly to RmlUi. Vespera retains the product/API boundary:

```text
C# / future Lua / MCP / Editor
             |
        Vespera UI API
             |
         RmlUiSurface
    DOM / layout / controls
             |
 Vespera renderer adapter
             |
       UiRenderPacket
          |      |
        D3D12  Vulkan later
```

RmlUi does not own Vespera's SDL window, D3D12 device/swap chain, game loop, AssetCatalog, `.vmeta` identities, project packaging, managed ABI, MCP surface or editor architecture.

## 0.9.8 productionization

- RmlUi and FreeType are pinned FetchContent dependencies.
- `RmlUiSurface` remains a PIMPL boundary so RmlUi headers do not become the public Vespera gameplay API.
- The surface can load/reload documents, route Vespera input, set text/form values/classes/properties/visibility/disabled state, consume clicks, and emit Vespera `UiRenderPacket` geometry.
- `.rml` and `.rcss` are recognized AssetCatalog kinds.
- Local RML `href`/`src` and RML/RCSS `url(...)` references contribute dependency edges so package closure can pull styles/images/fonts referenced by UI assets.
- The previous isolated guild UI remains as a visual comparison/showcase and has explicit block layout to avoid the text collisions seen in the first evaluation screenshot.


## 0.9.9c managed gameplay bridge

0.9.9c moves the C# runtime UI boundary onto `UiSurface`. `ManagedScriptHost` receives only a `UiSurface*` and resolves C# elements through opaque `UiHandleTable` handles, so the managed bridge no longer depends on `UiDocument` / `UiRuntimeState` or exposes backend identity through `UiElement.Id`. The reference HUD remains `.slui` through `LegacyUiSurface`; an RmlUi-backed game surface can satisfy the same managed calls.

Managed ABI v10 adds string value get/set plus generic property/class mutation, bringing the service table to 84 ordered C++/C# fields. `UiElement.ValueText`, `SetProperty`, and `SetClass` provide RML-friendly form/style controls without exposing RmlUi classes to game code. Existing semantic calls (text, visible, interactable, focus, numeric value, colors, assets and clicks) now also travel through `UiSurface`. Unsupported backend operations return failure instead of being emulated.

MCP/editor UI authoring is still the legacy `.slui` model in this checkpoint. That migration remains separate from the gameplay bridge so existing automation and undo behavior are not silently changed.

## 0.9.9b engine-owned semantic UI surface

0.9.9b adds `UiSurface`, a deliberately narrow Vespera-owned interface for operations gameplay and automation need on named UI elements. It does **not** abstract layout, file format, authoring, input processing or renderer packet construction. `.slui` keeps its RectTransform-like document model; RmlUi keeps DOM/RCSS layout.

`LegacyUiSurface` adapts the existing `.slui` `UiDocument` + `UiRuntimeState`. `RmlUiSurface` implements the same interface directly while keeping RmlUi behind the existing PIMPL. The common operations cover lookup, text/value access, visibility, disabled/focus/click semantics and optional generic class/property mutation. Legacy `.slui` returns failure for CSS-only operations rather than emulating a CSS system.

This checkpoint intentionally does **not** redirect the current managed ABI or MCP UI commands. C# `UI` / `UiElement`, editor UI authoring and existing QA still use `.slui` directly. The next migration slice can move those callers onto `UiSurface` without simultaneously introducing the abstraction and changing public runtime behavior.

## 0.9.9a stable authoring paths

Vespera keeps RML/RCSS source files standards-friendly rather than introducing an `asset://` URI dialect. The AssetCatalog still records stable IDs for every recognized asset. During a **Vespera-controlled** move/rename, the authoring layer uses that identity plus the pre-move dependency graph to rewrite local RML/RCSS `href`, `src`, and `url(...)` paths to the moved asset. If the RML/RCSS source file itself moves, its local references are rebased from the new directory. Query/fragment suffixes are preserved.

This is deliberately a guarded authoring workflow. Known RML/RCSS path dependents must be rewritable or the move is refused. Rewrites use atomic text replacement, and a rewrite failure after the source move triggers best-effort restoration of already-rewritten dependents plus rollback of the source and `.vmeta`. Older non-RML path-only references retain their existing move blockers.

Arbitrary external filesystem moves remain a different problem: once an ordinary path-only RML reference is broken outside Vespera, the file itself does not contain enough stable identity to prove which moved asset it originally meant. 0.9.9a does not pretend to solve that ambiguity.

## Compatibility

`.slui` v3 remains supported during migration because it is still used by the existing reference QA/C# runtime-automation path. Do not delete the legacy implementation until the RmlUi path has equivalent C#/Lua/MCP/editor/runtime test coverage and migration strategy.

## Current limitations

- One active `RmlUiSurface` per process.
- The current packet adapter uses RmlUi's compatibility/basic geometry path; advanced filters/transforms/layers/shader effects are not yet fully represented by `UiRenderPacket`.
- RML/RCSS source text remains path-based. Vespera-controlled moves repair local references, but arbitrary external moves cannot yet be reconstructed automatically once the authored path is broken.
- No dedicated RML/RCSS visual editor is shipped yet; Project browser recognition is present first.
- C# and project-level Lua runtime element operations are backend-neutral through `UiSurface`; MCP/editor visual UI authoring still targets legacy `.slui`.
- Screenshot/pixel visual-regression capture remains future QA infrastructure.

## Direction

Prefer using RmlUi for typography, layout, forms, focus, scrolling, styles and transitions. Spend Vespera engineering effort on excellent game-facing APIs, stable assets, editor authoring, project templates, automation, packaging and renderer integration rather than reimplementing a general UI engine.

## 0.9.9b1 RmlUi sizing hotfix

Windows visual feedback showed that the remaining oversized-control feel was primarily authored RCSS sizing rather than a renderer or `UiSurface` problem. The Project Hub now uses a tighter content/panel/input width budget, and the guild showcase uses shrink-to-fit inline-block layout for primary/secondary action buttons while retaining full-width selection/list rows. RmlUi documents `min-width` with an initial value of `0px`; explicit `min-width: 0px` in the Hub is therefore descriptive, not the claimed fix.



## 0.9.9c1 responsive list sizing correction

The remaining guild-showcase list stretching was traced to authored RCSS, not to the RmlUi context or renderer adapter. `.member` and `.mission` were explicitly `width: 100%` inside responsive panels, so they correctly grew with those panels. The carried-forward fix keeps narrow-window responsiveness while capping member rows at 260px and mission rows at 340px; the War Room action also receives enough minimum width to avoid unnecessary wrapping.

## 0.9.9f explicit project startup RML

The shared `vespera_player` can mount an RML document as the active game-facing `UiSurface`, so the managed ABI v10 bridge can operate against RmlUi in an ordinary project runtime rather than only the Hub/showcase. Project v8 owns that choice explicitly through the stable `startup_ui` reference; Project v9 Lua receives the same active `UiSurface` when a `lua_entry` is configured. The startup document is an automatic build root and participates in stable fallback repair/safe-delete preflight. Older projects without `startup_ui` keep the first-RML-build-root behavior only as a compatibility fallback.
