# Vespera 1.1 release validation

The Vespera 1.1 release tree is feature-frozen. This gate validates the exact public release tree before publication or redistribution. New renderer, scripting, physics, platform, editor, or gameplay systems do not belong in this final validation tree unless they are required to fix a release blocker.

## One-command Windows gate

From a fresh extracted source package:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\test-rc.ps1
```

The top-level runner prints plain PASS/FAIL/SKIP lines. Its automated half intentionally performs only **one native Release build**: it runs the normal release/public-workflow gate and reuses those Release binaries for the heavier editor/runtime stress and regression pass. After automation passes, it launches four Release visual smokes (D3D12 editor/game and Vulkan editor/game) and asks the tester to mark each pass/fail/skip.

The combined gate covers:

- source/package preflight, public-release hygiene checks, and source-structure validation;
- all lightweight engine behavioral tests;
- one Release native build;
- Project Hub creation of a fresh 3D starter and the experimental 2D/UI foundation;
- fresh-starter Release + portable .NET export, package closure, standalone launch and persistent runtime log;
- Emberlight Guild and Performance Lab normal Release exports/launches;
- live editor version fingerprinting before mutation;
- managed compiler-error injection, exact source restore and successful rebuild;
- 24 Play/Pause/Step/Resume/Stop restoration cycles by default;
- 16 saved-scene switch/restore cycles by default;
- controlled Prefab move -> stable-reference repair -> save -> reopen -> restore;
- editor Play runtime/input/UI/lifecycle telemetry;
- Release exported-runtime automation through the same runtime QA surface;
- MCP asynchronous managed-build operation polling;
- bounded 150-entity authoring/Play stress with clean-scene reopen cleanup;
- integrity checks before and after destructive scenarios.

The cycle counts can be raised without changing source:

```powershell
.\test-rc.ps1 -PlayCycles 48 -SceneSwitches 32
```

If the native Release build already completed successfully for this exact checkout, `-NoBuild` reuses it. Do not use `-NoBuild` after source/native changes. `-SkipManual` is available for automation-only CI/reproduction; a release build is not visually certified until the normal runner completes the manual smokes.

For low-level diagnosis, `tools/run-rc-gate.ps1` remains available and supports `-SkipReleaseGate`. That is a developer shortcut for reproducing the later stress/regression pass, not the normal command used to certify a fresh release tree.

## Final human UX check

A green automated gate is followed by one short normal-user check:

1. Launch `./vespera.ps1`.
2. Create or open a normal 3D project through Project Hub.
3. Enter Play, interact briefly, and Stop.
4. Use **Build -> Build Game... -> Release -> Build & Run**.
5. Confirm the packaged game starts as a normal Windows application, has no unwanted console window, and writes a useful runtime log if startup fails.

## Release blocker policy

Fix before 1.1 when a defect can crash/hang, corrupt or lose authored state, produce an unusable Release package, break clean-machine project creation/build/export, make C#/Lua/RmlUi/save/scene lifecycle unreliable, silently choose the wrong project/runtime/dependency, or create a clearly broken normal-user workflow.

Do not delay 1.1 for production-certified Vulkan/Linux, ray tracing, upscalers, visual scripting, advanced physics/GI, large multiplayer systems, or a complete dedicated orthographic 2D editor. The current 2D starter remains **2D / UI Foundation (Experimental)**.

## Startup presentation

Vespera's default startup artwork remains visible for at least **4 seconds** in Application-hosted runtimes and the native editor. Real loading time counts toward the four-second window. This is a deliberate presentation choice so the engine ident remains readable on fast machines rather than flashing for roughly one second.
