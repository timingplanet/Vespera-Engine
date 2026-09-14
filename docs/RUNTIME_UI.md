# Vespera runtime UI

## 0.9.8 direction

RmlUi 6.2 + FreeType 2.14.1 is now Vespera's chosen low-level production UI foundation. Vespera owns the gameplay/API/asset/editor/automation boundary and renderer integration; RmlUi supplies DOM/layout/text/forms/styling.

See `RMLUI.md` for the production architecture.


## 0.9.9c managed semantic bridge

Managed C# UI now routes through the engine-owned `UiSurface` contract. `ManagedScriptHost` accepts a surface instead of legacy document/runtime pointers, and `UiHandleTable` gives C# opaque numeric handles that revalidate against the active backend. The existing reference HUD is still `.slui` through `LegacyUiSurface`; RmlUi can satisfy the same managed operations.

The current managed ABI is **v12 / 88 ordered native+C# fields**; the UI callbacks themselves were introduced in v10. `UiElement` retains the existing text/visible/interactable/focus/value/color/asset/click surface and adds `ValueText`, `SetProperty`, and `SetClass` for form controls and RML/RCSS workflows. CSS-only operations continue to fail on legacy `.slui` instead of pretending support. MCP/editor UI authoring remains legacy in 0.9.9c.

## 0.9.9b semantic surface seam

`UiSurface` is now the engine-owned game-facing UI contract. `LegacyUiSurface` adapts `.slui`, and `RmlUiSurface` implements the same semantic element interface. The seam intentionally excludes layout/serialization/render preparation. Managed C# and MCP remain on the legacy document in 0.9.9b; migrating those callers through `UiSurface` is the next compatibility-preserving step.

## Legacy `.slui` compatibility

The existing engine-native `.slui` v3 implementation remains supported while production APIs and QA migrate. It continues loading v1/v2/v3 documents and remains the current reference-runtime semantic automation path.

`.slui` v3 includes Canvas, Panel, Text, Image, Button, ProgressBar, ScrollView, List, Grid, Tabs, Modal, Tooltip and TextInput plus rounded surfaces, borders, shadows, image fit/9-slice and richer typography. Windows uses antialiased installed/project fonts with a deterministic bitmap fallback.

The legacy `.slui` loader/runtime remains regression coverage during migration. Its existing `UiElement` semantics, pointer/focus/text input, runtime telemetry and exported-runtime automation are preserved through `LegacyUiSurface`.

## Migration requirements before retiring `.slui`

- equivalent C# and Lua semantic wrappers over RmlUi elements;
- stable project asset integration for RML/RCSS/images/fonts;
- editor authoring/preview and diagnostics;
- MCP creation/edit/query operations with efficient batched changes;
- runtime input/focus/text telemetry parity;
- standalone package/export dependency coverage;
- compatibility/migration guidance for existing `.slui` documents.

## 0.9.9b1 RmlUi sizing hotfix

Windows visual feedback showed that the remaining oversized-control feel was primarily authored RCSS sizing rather than a renderer or `UiSurface` problem. The Project Hub now uses a tighter content/panel/input width budget, and the guild showcase uses shrink-to-fit inline-block layout for primary/secondary action buttons while retaining full-width selection/list rows. RmlUi documents `min-width` with an initial value of `0px`; explicit `min-width: 0px` in the Hub is therefore descriptive, not the claimed fix.

