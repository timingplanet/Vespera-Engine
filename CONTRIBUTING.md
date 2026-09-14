# Contributing to Vespera Engine

Vespera is a native C++20 engine with a C# gameplay layer, optional project-level
Lua, RmlUi runtime UI, and a Windows Direct3D 12 editor/runtime path.

## Before changing engine code

Read [Testing](docs/TESTING.md) and [Release Validation](docs/RELEASE_VALIDATION.md) before making substantial engine changes. Keep changes small enough to reproduce and validate, and do not silently work around engine bugs.

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
.\tools\run-rc-gate.ps1
```

## Pull-request expectations

- Explain the user-facing problem being solved.
- Include a minimal reproduction for bug fixes when practical.
- Add or update regression coverage for reproducible defects.
- Preserve serialized compatibility unless the change explicitly includes migration.
- Do not broaden a focused fix into an unrelated refactor.
- Update documentation when public behavior changes.

## Scope

The 1.0 limitations are documented in `docs/LIMITATIONS.md`. New post-1.0
features should not be presented as already-supported 1.0 workflows.
