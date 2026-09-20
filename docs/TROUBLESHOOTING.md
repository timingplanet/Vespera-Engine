# Troubleshooting

Start with the smallest boundary that is failing: source build, project open, editor authoring, C# build, Play Mode, or exported package. Avoid changing multiple systems at once while diagnosing.

## The engine will not build

Check:

1. Visual Studio 2022 or 2026 is installed with C++/CMake tooling.
2. CMake is available either on PATH or through Visual Studio.
3. You are building x64.
4. `tools/build-managed-editor.ps1` exists in the source package.
5. If C# is needed, the .NET 8+ SDK is installed.

Run:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\build.ps1
```

Read the first real error rather than the final cascade.

## C# scripts do not appear in the Inspector

- Build C# Scripts.
- Fix any compiler errors.
- Use Refresh C# Metadata.
- Confirm your class is public, derives from `Vespera.Component`, and is in the project's configured managed assembly.
- Mark fields with `[Expose]` if you expect them in the Inspector.

A failed build should preserve the last-good managed output, so the diagnostic text is more useful than repeatedly restarting the editor.

## Input returns zero/false

Open **Project Settings → Input Actions** and verify the action name exactly matches the string used in code. Check device, code, scale, and deadzone.

Use `Down` for held state, `Pressed` for a one-frame press transition, and `Value` for axes.

## An asset disappeared after I moved it

If the move happened outside Vespera:

1. Refresh Asset Catalog.
2. Inspect the asset's `.vmeta` and catalog identity.
3. Check dependent scenes/materials/prefabs/RML.
4. Restore the original file plus `.vmeta` together if identity was accidentally broken.

Use **Move / Rename…** in the editor for future referenced moves.

## A standalone build is missing an asset

The packager follows known dependencies. Content loaded only by a string from gameplay code may not be discoverable automatically.

Add the content as an explicit build include, rebuild, then inspect `Vespera.PackageManifest.txt` and `Vespera.PackageReport.txt`.

## RmlUi element lookup returns null

- Verify the startup RML document is configured.
- Verify the element has the exact `id` you pass to `UI.Find`.
- Confirm the UI is active in the current scene/runtime state.
- Use `UI.FindRequired` while debugging a mandatory element to get a direct failure instead of silently continuing.

## Exported C# game will not start on another PC

If using framework-dependent deployment, install a compatible .NET runtime on that machine or switch the project to **Portable (.NET bundled)** and rebuild.

## The editor project state looks corrupted

Do not hand-edit the scene until you have a copy. Save backups first. Run the editor's scene validation and project/integrity checks available to your workflow. For reproducible engine bugs, preserve the smallest project that demonstrates the failure.
