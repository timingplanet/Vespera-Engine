# Exporting — 1.0.0

Vespera packages the deterministic stable-ID build closure rather than copying the entire Assets tree.

## Shared player runtime

Vespera 0.16 retains the shared-player behavior that removes the requirement that every project define a custom native `game_target`. When `game_target` is empty, `export.ps1` packages the engine-built `vespera_player` and the packager renames it to the project-authored `executable_name`. A custom `game_target` remains supported for specialized hosts such as the reference QA runtime.

The packaged player expects exactly one `.vesperaproject` beside the executable and loads it automatically. For source-project testing, use:

```powershell
.\run-project.ps1 -Project "C:\Path\To\Game.vesperaproject"
```

If the project declares `managed_project` + `managed_assembly`, export builds that project through the existing last-good managed helper into an isolated export staging directory. This no longer assumes the project is the reference game. Projects with no managed assembly remain native-only and do not require .NET payloads.

Project v8 adds a first-class **`startup_ui`** stable asset reference. When authored, the shared player resolves that RML document by stable ID and uses it as the active runtime `UiSurface`. The startup UI is automatically a build root, so it does not need to be repeated in `build_include`. If an older project has no `startup_ui`, the player preserves the pre-v8 first-RML-build-root convention as an explicit compatibility fallback.

## Shared-player startup branding

The shared player package includes Vespera's engine-branding payload under `branding/`. On launch it presents `vespera_splash.png` during project startup and plays `vespera_logo_sting.wav` at a restrained default volume. The 1.0.0 default keeps the Vespera startup artwork visible for at least 4 seconds; real loading time counts toward that interval.

A project-authored `game_icon` is preferred for the runtime window icon. Projects without one use the Vespera filled app icon. The exported executable itself currently embeds the shared Vespera icon resource; project-specific executable resource stamping remains a later shipping-polish task.

## Project v8 Startup UI

Project v8 adds:

- `startup_ui_asset "<stable-id>" "ui/main.rml"` — preferred stable reference;
- `startup_ui "ui/main.rml"` — path-only fallback supported for authored/legacy edge cases.

Safe-delete preflight blocks deleting the active startup UI. Stable fallback repair updates its readable path after an ID-preserving move. `vespera_get_project_settings` reports both the path and stable ID, and `vespera_set_project_setting` accepts `startup_ui` by project-relative path or stable ID.

Project v1-v9 files remain load-compatible. Saving emits Project v10.

## Project v9 Lua entry

Project v9 adds an optional stable `lua_entry_asset` / path fallback for one project-level Lua script. When configured, it is an automatic build root and is packaged with its dependency closure. The shared player starts the Lua entry alongside C# when both are configured. See `LUA_SCRIPTING.md`.

## Project v10 VSync policy

Project v10 serializes the project-owned `vsync` setting. Normal starter projects default to VSync on; projects such as Performance Lab can explicitly disable it without hard-coding renderer behavior.

## Project v7 Build Settings

Project v7 retains the v6 shipping metadata and adds the settings needed for a less source-tree-shaped build workflow:

- `company_name`
- `product_version`
- `package_name`
- `managed_deployment` — `framework-dependent` or `portable`
- `executable_name` — public packaged executable basename, independent from the internal CMake target
- `build_output_directory` — project-relative by default; `builds` when omitted by an older project
- `game_icon_asset` / legacy-path fallback — stable project asset reference used by build closure/metadata
- `development_diagnostics` — controls Development symbol packaging

The v7 shipping fields, v8 startup-UI fields and v9 optional stable `lua_entry` remain unchanged inside Project v10. Project v10 additionally owns the serialized VSync policy; older Project v1-v9 files remain load-compatible and receive safe in-memory defaults; saving emits Project v10.

The existing startup scene is always a build root. `build_include_asset` entries remain the explicit additional-scene/dynamic-asset mechanism.

0.9.6 retains MCP authoring for those explicit roots through `vespera_set_build_include`, and `vespera_get_project_settings` reports them. Use explicit stable-ID roots for assets loaded directly/dynamically by game code when the deterministic descriptor dependency graph cannot infer the reference. The reference project now explicitly includes `animations/watcher_walk.slspriteclip`, which the runtime loads directly.

## Build configurations

| Vespera configuration | Native configuration | Symbols |
| --- | --- | --- |
| Debug | Debug | yes |
| Development | RelWithDebInfo | when Development Diagnostics is enabled |
| Release | Release | no by default |

For shared-player Windows `.exe` packages, the packager changes only the copied **Release** executable to the GUI subsystem so shipping builds do not open an unwanted console. The source-tree Release runtime remains console-oriented for `run-project.ps1` diagnostics. Shared-player logs are mirrored to `%LOCALAPPDATA%\Vespera\Logs\<executable>.log` (package-local `logs/` fallback on non-Windows), so Release startup/fatal diagnostics do not depend on a console.

`Build -> Build Game...` is the normal user path in 1.0.0. The underlying export bridge reuses the existing multi-config CMake tree and builds only the requested runtime target plus `vespera_packager` before packaging. `build.ps1` remains the full source-checkout build/QA path.

## In-editor Build Game

Open **Build -> Build Game...** or press `Ctrl+Shift+B`. The window exposes Debug / Development / Release, executable name, output directory, managed deployment, Development diagnostics, build preflight, **Build**, **Build & Run**, recent build output, and **Open Output Folder**. Builds execute asynchronously so the editor remains responsive.

The editor saves the current scene/project, refreshes the catalog, and refuses to start packaging if project validation or deterministic build closure has errors. Normal developers do not need to type the PowerShell commands below; those remain useful for source-checkout automation/CI/QA.

## PowerShell

Pass the project explicitly. Vespera no longer silently falls back to the QA reference game when `-Project` is omitted.

```powershell
$Project = "examples\performance_lab\VesperaPerformanceLab.vesperaproject"
.\export.ps1 -Project $Project
```

Debug or Release:

```powershell
.\export.ps1 -Project $Project -Configuration Debug
.\export.ps1 -Project $Project -Configuration Release
```

Export and launch the packaged executable with the package directory as its working directory:

```powershell
.\export.ps1 -Project $Project -Configuration Release -Launch
```

Force framework-dependent or portable managed deployment:

```powershell
.\export.ps1 -Project $Project -ManagedDeployment FrameworkDependent
.\export.ps1 -Project $Project -ManagedDeployment Portable
```

An explicit dotnet root can still be supplied:

```powershell
.\export.ps1 -Project $Project -ManagedDeployment Portable -DotnetRoot "C:\Program Files\dotnet"
```

`-AllowReferenceGameFallback` exists only for intentional QA/reference-game automation that historically relied on a bare `export.ps1` call.

`-Output` overrides Project Build Settings. Relative overrides are resolved from the project directory rather than Vespera's engine source root.

## Package contents

Before output mutation, managed projects must provide a complete staged managed runtime. After copying, the C++ packager self-checks the packaged project/runtime, managed bridge/game assembly, portable .NET host/runtime when requested, and shared-player branding payload. Build Game, MCP/direct packaging and `export.ps1` therefore share the same package-completeness behavior.

A normal package contains:

- only the required transitive asset closure;
- matching `.vmeta` identity sidecars;
- the `.vesperaproject` file;
- staged Vespera.NET/game managed runtime files where present;
- private .NET runtime files when Portable is selected;
- the runtime executable renamed to `executable_name`;
- an adjacent PDB when the configuration/symbol policy requests it;
- `Vespera.BuildAssetIndex.txt`;
- `Vespera.PackageManifest.txt` (v3);
- `Vespera.PackageReport.txt`.

The package report includes configuration/native configuration, deployment mode, executable, game-icon path, counts, bytes by asset kind, warnings and the exact deterministic asset closure. It is meant to be useful to developers and automated QA without requiring knowledge of Vespera's internal build folder structure.

Project game icon is package metadata, a stable build root, and the preferred runtime window icon. The executable resource itself still uses Vespera's shared application icon; project-specific PE resource stamping is not part of the current 1.0 release gate.

## Engine release gate

Engine contributors can validate the clean shipping path with one Windows command after a meaningful checkpoint:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\tools\run-release-gate.ps1
```

The gate runs behavioral tests plus one Release build, creates a fresh 3D starter through the Project Hub template creator, exports it as Release + portable .NET, validates/launches the package and persistent runtime log, then exports/launches Emberlight Guild and Performance Lab through the ordinary shared-player path. It is QA automation; normal game developers should use **Build Game**.

## Safety

Source Assets are never modified by export. Dangerous clean-output locations that are the project/assets directory, an Assets descendant, or a dangerous parent are rejected.

Game-icon assets use the same stable ID + readable fallback semantics as other authored references. Safe-delete preflight blocks deleting the active game icon; fallback repair updates its readable path after an ID-preserving move.

## Editor and MCP

The editor exposes a Build Game window and Build menu for Debug/Development/Release packaging and Build & Run. The lower-level direct package API remains available to automation/MCP when matching native outputs already exist; the user-facing Build Game workflow drives the build+package bridge when they do not.

`vespera_export_project` accepts Debug/Development/Release. `output_directory` is optional; when omitted it uses Project Build Settings. The response includes packaged runtime and package-report paths plus counts/bytes so 0.9.5 torture tests can inspect the result.

## Project Hub template note (0.9.8)

The Project Hub starter templates intentionally leave `game_target` empty because they use the shared `vespera_player` runtime. In 0.9.9 each template also declares a project-owned managed project/assembly, so `export.ps1` builds that C# project through the guarded last-good helper and packages it under `managed/` beside the shared runtime. Do not treat the reference-game executable or managed assembly as an implicit dependency of new projects.
