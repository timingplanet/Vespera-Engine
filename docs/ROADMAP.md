# Vespera Engine roadmap

**Current checkpoint: 1.0.0 — final public release tree / validation.**

## 0.15.x — Performance + Presentation

Finish the fresh-project reliability and startup-presentation work, then use a real ordinary project to measure and improve the engine instead of tuning blindly. Starter projects must load cleanly even with missing/broken assets, Play must fail clearly when a project/startup scene did not load, and templates must be validated end-to-end. Vespera's editor/Hub/player use proper multi-resolution Windows icons, splash/loading artwork and startup sting; loading presentation should reflect real startup work rather than add fake delays.

`examples/performance_lab` is the measurement dogfood project. Its target coverage is an automated cinematic benchmark route with Baseline / Gameplay / Heavy / Torture presets; FPS, frame time, 1% low and worst-frame reporting; renderer/update telemetry; script/entity/sprite/light/UI stress; scene-switch and asset-heavy loading coverage; and benchmark result export. 0.15.x patch releases are reserved for optimizations and reliability fixes exposed by those measurements.

0.15.2 added uncapped project VSync control for the Lab, renderer visibility/draw/light telemetry, managed bulk-transform/performance APIs, primitive/sprite instancing, script-attachment hot-path cleanup and JSON/CSV result export. The user-run Torture workload rose from the original ~52 FPS bottleneck to the presentation ceiling, so Performance Lab remains a regression harness rather than the main development target.

## 0.16.x — Release + clean-machine hardening

Sequence matters:

1. **Clean-machine ship path first.** Prove fresh unzip -> build -> Hub -> create project -> Play -> Build Game -> Release -> launch standalone without relying on caches, generated leftovers or developer-machine state. Exercise Emberlight Guild and Performance Lab as ordinary projects and verify Development vs Release packaging, C# deployment, Lua, RmlUi, stable asset references, save data, scene changes and packaged dependency closure.
2. **Make the explicit 2D decision second.** Either implement a genuine orthographic 2D workflow/editor experience good enough to advertise for 1.0, or stop presenting the existing 2D foundation template as a complete dedicated 2D mode.
3. **Release polish third.** Once packaging/player behavior is stable, finish normal-Windows-app behavior: unwanted console suppression in Release, icons, startup/loading presentation, crash/error logs and related package cleanup. Doing this after clean-machine validation avoids polishing paths that packaging fixes may change.

0.16.0 implements the first large hardening slice: package completeness checks, runtime-owned branding staging, deterministic runtime discovery, Release-only GUI subsystem conversion, persistent runtime error logs, a one-command release gate, and a headless Project Hub creation path used only by QA/CI while reusing the same template creator as the visual Hub. The 2D decision is also resolved conservatively: the old 2D starter is now explicitly **2D / UI Foundation (Experimental)** and is not advertised as a complete orthographic 2D mode for 1.0.

0.16.2 fixed the Windows release-gate blockers and then **passed the complete release gate on the user's Windows machine**. That makes 0.16.2 the validated release-hardening baseline rather than an unfinished milestone.

## Pre-RC / RC — stop adding systems

Feature work effectively freezes. Focus on regression fixes, adversarial QA, performance budgets, editor UX annoyances, import/move/delete edge cases, managed/Lua lifecycle problems, repeated Play/Stop and scene-switch torture, large-project tests, documentation verification, package/license/version cleanup and final compatibility checks. Defer anything not required to finish and ship a small game.


The 1.0.0 release tree promotes that validated RC baseline after final licensing, documentation-site, public-tree hygiene, package legal-notice, and 2D/UI starter polish. No serialized format or managed ABI bump is introduced by this release-prep pass.

1.0.0-rc.1 is the first feature-freeze candidate. It does not add a new gameplay/rendering subsystem. It adds the one-build `tools/run-rc-gate.ps1` adversarial path, longer repeated Play/Stop coverage, saved-scene switch/restore torture, Release exported-runtime automation, and final startup/release presentation cleanup. The Vespera startup splash now has a 4-second minimum visibility window, with real load time counting toward it.

The rc.1 rule was simple: fix release blockers and genuinely ugly user-facing defects only. That validated baseline is now carried into the exact 1.0.0 public tree, which still requires the final clean-tree gate and human Hub -> Play -> Build Game -> Release -> standalone check before publication.

## 1.0 — Windows/D3D12 production release

The bar is straightforward: a normal developer can download Vespera, create/open a project, author a small real game, use C# as the primary scripting layer and Lua where appropriate, build UI in RmlUi, use save data/scenes/assets/audio/prefabs/components, test inside the editor, press **Build Game**, and receive a usable standalone Windows game without needing Vespera's internal PowerShell workflow. Emberlight Guild and Performance Lab must both ship through that exact public path.

Vulkan/Linux maturity, hardware ray tracing, DLSS/FSR, visual scripting, advanced physics/GI, consoles and a large multiplayer framework do not block 1.0.
