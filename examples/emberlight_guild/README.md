# Emberlight Guild sample

A small UI-heavy Vespera sample that intentionally uses the same public workflow as a normal game project rather than a bespoke native showcase executable.

It demonstrates:

- Project v9 stable startup scene + startup RML;
- project-owned C# through `Vespera.NET`;
- RmlUi + external RCSS;
- backend-neutral `UiSurface` interaction from C#;
- button/class/property mutation;
- SaveData persistence for guild name/resources/upgrades;
- the shared `vespera_player` and normal Build Game/export closure.

Open `EmberlightGuild.vesperaproject` from the Project Hub. Build its C# through the normal editor workflow, enter Play, or use **Build -> Build Game...** for a standalone package.

This is a game-development sample, not the QA reference project. Engine torture tests should continue using `examples/reference_game`.
