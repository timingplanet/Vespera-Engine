# Your First Vespera Game

This walkthrough uses the **3D / 2.5D Game** starter because it exercises the same project-owned workflow a real game uses without depending on the reference QA project.

## Create the project

Launch the Project Hub and create a **3D / 2.5D Game**. The starter contains:

- a Project v10 `.vesperaproject`;
- a startup `.slscene` with one sector room and point light;
- project-owned C# source and managed project;
- an RmlUi startup document and stylesheet;
- authored input bindings.

The empty `game_target` in starter projects is intentional. It means standalone builds use Vespera's shared data-driven `vespera_player` instead of requiring a custom CMake executable.

## Run it in the editor

Open the project and enter Play Mode with **Ctrl+P**. The starter C# component is attached by the project's own scene data. The Game view renders the project's startup RML through the same `UiSurface` semantics used by managed code.

The starter exists to prove the project pipeline, not to prescribe your game's architecture. Replace its entities, C# and RML as your game takes shape.

## Edit gameplay

Project C# lives under the project's `managed/` folder. Vespera builds and stages the selected project's managed assembly rather than copying the reference-game output into arbitrary projects.

C# provides the primary gameplay surface: entities, transforms/components, scene queries, input, audio, assets, UI, prefab spawning/destruction, save data and lifecycle callbacks.

A project may also configure a Lua entry in Project Settings. Lua receives project-level `Start`, `Update(dt)` and `Stop` lifecycle and is intentionally a lightweight companion rather than a second component framework.

## Edit runtime UI

The project's startup UI is an explicit stable Project v10 asset reference. Select the `.rml` or `.rcss` asset in the Project browser to edit source and see dependency diagnostics.

Saving the active Play RML document or one of its linked stylesheets reloads the active RmlUi surface. Vespera-controlled asset moves repair local RML/RCSS paths and stable project fallbacks.

## Change project/runtime settings

Use Project Settings for the startup scene, startup RML, Lua entry, executable/package information, window settings, input bindings and build configuration. Avoid hand-editing the project file for normal authoring.

## Make a standalone build

Open **Build -> Build Game...**. The preflight lists project validation problems, missing build roots and broken asset references before enabling the build buttons.

Start with **Development -> Build & Run**. Once that succeeds, use Release for a shipping-oriented package. See [Build & Ship](BUILD_AND_SHIP.md) for what each configuration means and what to verify before distributing a build.
