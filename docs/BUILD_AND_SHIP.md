# Build & Ship

Vespera's normal standalone-build workflow lives inside the editor. PowerShell export scripts remain available for engine automation and CI-like QA, but they are not the intended game-developer interface.

## Open Build Game

Use **Build -> Build Game...** or **Ctrl+Shift+B**.

The window exposes:

- **Debug / Development / Release** configuration;
- executable name;
- build output directory;
- framework-dependent or portable managed deployment;
- Development diagnostics/symbols;
- deterministic asset-closure preflight;
- **Build**, **Build & Run**, and **Open Output Folder**;
- build status and recent build output.

Builds run asynchronously so the editor remains responsive.

## Read the preflight before building

The preflight reports the actual project issues instead of only a total count. Fix errors before Vespera enables Build. Typical blockers include:

- missing startup scene;
- missing managed project/assembly configuration;
- invalid input bindings;
- missing build roots;
- broken asset dependencies;
- unsafe executable/package names;
- invalid startup RML or Lua entry references.

Warnings describe non-fatal fallback behavior. For example, a project with no custom `game_target` uses the shared `vespera_player` by design. A Release build without a project-authored game icon uses the Vespera fallback icon.

## Configurations

**Debug** is for debugging and automation-heavy development.

**Development** is the recommended everyday standalone test package. It can retain useful diagnostics/symbol behavior according to Project Build Settings.

**Release** is the shipping-oriented configuration. The packaged shared-player Windows executable is converted to the GUI subsystem so it behaves like a normal game instead of opening a console window; the source-tree Release player remains console-friendly for direct development runs. Test the actual Release package before distribution; do not assume a Development package proves Release deployment.

## Managed deployment

Projects with C# can be packaged as:

- **Framework-dependent** — smaller package; the target machine needs a compatible .NET runtime.
- **Portable** — Vespera copies a compatible runtime beside the game so the package does not rely on a machine-wide .NET install.

Projects without a managed assembly do not need a .NET payload.

## What Vespera packages

Vespera computes a deterministic transitive build closure rooted at authored project references such as the startup scene, startup RML, Lua entry and explicit build includes. It does not blindly copy the entire Assets tree.

For supported asset reorganization, move/rename assets through Vespera's Project workflow so stable IDs and known RML/RCSS path dependents can be repaired together. Arbitrary Explorer/filesystem moves of already path-authored RML/RCSS references are not guaranteed to be reconstructible after the original path is lost.

The package includes the game runtime, `.vesperaproject`, required assets, managed output when configured, runtime branding/support files, Vespera license/third-party notices, and package/index/report metadata. The packager refuses incomplete managed stages and performs a post-copy payload self-check. Portable C# packages verify their private `hostfxr` and selected `Microsoft.NETCore.App` runtime. Starter projects use the shared `vespera_player`; specialized projects may still name a custom runtime target.

## Before distributing a 1.0-era Windows build

Verify at minimum:

1. Release preflight has no errors or broken dependencies.
2. The package launches from its output folder, not only from the source checkout.
3. Startup scene, RmlUi, C# and optional Lua all initialize correctly.
4. Input/audio/save behavior works on the packaged runtime.
5. The game has the intended product name/version and, when desired, a project-authored icon.
6. Portable managed deployment is tested on a machine without relying on your development .NET installation when portability is required.
7. No Vespera QA automation endpoint is enabled in a normal shipping launch.
8. Check `%LOCALAPPDATA%\Vespera\Logs\<game>.log` when diagnosing a Release startup/crash failure; shipping player logs no longer depend on a console window.

Vespera's currently validated release platform is Windows x64 with Direct3D 12. Other renderer/platform targets remain later roadmap work.

## Engine release gate

Engine contributors validating the Vespera distribution can run one combined Windows gate after a fresh checkout/build change:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\tools\run-release-gate.ps1
```

It runs the behavioral tests, performs one Release build, creates a fresh starter through the Project Hub template path, exports/launches a Release portable package, checks its runtime log, then exports/launches Emberlight Guild and Performance Lab as ordinary shared-player projects. This is an engine-release gate, not a command normal game developers need.
