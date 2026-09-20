# Contributing to Vespera Engine

Vespera is a native C++20 engine with a C# gameplay layer, optional project-level
Lua, RmlUi runtime UI, and a Windows Direct3D 12 editor/runtime path.

## Before changing engine code

Read `docs/TESTING.md` and `docs/RELEASE_VALIDATION.md` before making broad engine changes.
Keep changes small enough to reproduce and validate, and do not silently work around engine bugs.

## Build on Windows

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\build.ps1
```

Run the editor:

```powershell
.\run.ps1
```

Run lightweight behavioral tests:

```powershell
.\test.ps1
```

For whole-release validation:

```powershell
.\test-rc.ps1
```

`tools/run-rc-gate.ps1` remains available for lower-level diagnosis.

## Pull-request expectations

- Explain the user-facing problem being solved.
- Include a minimal reproduction for bug fixes when practical.
- Add or update regression coverage for reproducible defects.
- Preserve serialized compatibility unless the change explicitly includes migration.
- Do not broaden a focused fix into an unrelated refactor.
- Update documentation when public behavior changes.

## Scope

Current platform and feature boundaries are documented in `docs/LIMITATIONS.md`. New or experimental work should not be presented as production-supported before it has passed the appropriate release gates.
