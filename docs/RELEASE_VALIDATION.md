# Vespera 1.0 release validation

The Vespera 1.0.0 source tree is feature-frozen. This gate validates the exact public release tree before publication. New renderer, scripting, physics, platform, editor, or gameplay systems do not belong in this final validation tree unless they are required to fix a release blocker.

## One-command Windows gate

From a fresh extracted source package:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\tools\run-rc-gate.ps1
```

The RC gate intentionally performs only **one native Release build**. It first runs the normal release/public-workflow gate and then reuses those Release binaries for the heavier editor/runtime torture pass.

The combined gate covers:

- source/package preflight, public-release hygiene checks, and source-structure validation;
- all lightweight engine behavioral tests;
- one Release native build;
- Project Hub creation of a fresh 3D starter and the experimental 2D/UI foundation;
- fresh-starter Release + portable .NET export, package closure, standalone launch and persistent runtime log;
- Emberlight Guild and Performance Lab ordinary-project Release exports/launches;
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
.\tools\run-rc-gate.ps1 -PlayCycles 48 -SceneSwitches 32
```

If the native Release build already completed successfully for this exact checkout, `-NoBuild` reuses it. Do not use `-NoBuild` after source/native changes.

If the release/public-workflow half has already passed for the exact same checkout and you are only reproducing a failure in the later adversarial pass, `-SkipReleaseGate` reruns the Release editor/runtime torture without repeating package launches or the native build. This is a diagnosis shortcut, not the command used to certify a fresh RC.

## Final human UX check

A green automated gate is followed by one short normal-user check:

1. Launch `./vespera.ps1`.
2. Create or open a normal 3D project through Project Hub.
3. Enter Play, interact briefly, and Stop.
4. Use **Build -> Build Game... -> Release -> Build & Run**.
5. Confirm the packaged game starts as a normal Windows application, has no unwanted console window, and writes a useful runtime log if startup fails.

## Release blocker policy

Fix before 1.0 when a defect can crash/hang, corrupt or lose authored state, produce an unusable Release package, break clean-machine project creation/build/export, make C#/Lua/RmlUi/save/scene lifecycle unreliable, silently choose the wrong project/runtime/dependency, or create a clearly broken normal-user workflow.

Do not delay 1.0 for Vulkan/Linux, ray tracing, upscalers, visual scripting, advanced physics/GI, large multiplayer systems, or a complete dedicated orthographic 2D editor. The current 2D starter remains **2D / UI Foundation (Experimental)**.

## Startup presentation

Vespera's default startup artwork remains visible for at least **4 seconds** in Application-hosted runtimes and the native editor. Real loading time counts toward the four-second window. This is a deliberate 1.0 presentation choice so the engine ident remains readable on fast machines rather than flashing for roughly one second.
