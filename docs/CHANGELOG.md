# Changelog

## 1.1.0

- Promotes the validated 1.1 release-candidate line to the final Vespera Engine 1.1.0 release.
- Keeps Windows x64 + Direct3D 12 as the production-supported renderer and default Windows path.
- Ships Vulkan as an opt-in parity/testing backend on Windows and keeps Linux + Vulkan explicitly experimental.
- Includes the hardened release/export workflow, portable or framework-dependent .NET packaging, public-source hygiene checks, installed-layout validation, release automation, and the final Windows RC regression gate.
- Includes the final public documentation cleanup and Vespera community Discord link.
- Preserves scene, prefab, project, package, stable component identifiers, MCP tool count, and managed ABI compatibility from the validated RC.6 baseline.
- No post-RC engine, renderer, gameplay, scene-format, or scripting-runtime feature changes are included in this promotion.

## 1.1.0-rc.6

- Fixes the Windows RC harness cleanup order after a fully successful adversarial QA pass.
- Stops the automation-controlled Release editor before deleting the disposable QA project so managed assemblies are no longer left locked by the editor process.
- Adds retry-based disposable-directory cleanup for transient Windows file-lock release delays.
- Preserves `-KeepEditor` as a debugging mode; full post-QA cleanup/public-staging validation is skipped when it is explicitly requested.
- No runtime, renderer, scene-format, managed-ABI, or editor feature changes.

## 1.1.0-rc.5

- Fixed Release-editor C# build/recovery on source checkouts. `VesperaBuilder` discovery now prefers the requested configuration but falls back across Development/Release/Debug builder binaries, because the builder executable's own native configuration does not constrain which managed configuration it can compile.
- Added editor-support coverage for builder fallback ordering and release-tooling regression checks for the managed-build discovery path.
- This specifically addresses the RC.4 Windows adversarial failures where `managed_build_recovery` could observe the intentional compiler failure but could not rebuild after exact source restoration, and the subsequent MCP managed-build smoke failed for the same missing-builder reason.

## 1.1.0-rc.4

- Fixes the final staged-public hygiene failure exposed by the complete RC.3 Windows gate: public-source preparation now strips generated managed `bin`/`obj` directories and project-local `.vespera` caches created by C# build/Play workflows.
- Extends public-release validation and release-tooling regression coverage so generated managed intermediates cannot silently re-enter staged source packages.
- Cleans Windows/MSVC warning noise found in the RC.3 transcript: redundant `WIN32_LEAN_AND_MEAN` and Lua platform defines, a byte-fill conversion, a shadowed local, and an unreachable `constexpr` fallback.
- Keeps the feature freeze in place: no scene/prefab/project/package format, managed ABI, MCP tool-count, editor structure, or renderer architecture changes.
- RC.3's Windows log reached and passed behavioral tests, Release build, fresh-project portable export/startup, Emberlight Guild export/startup, and Performance Lab export/startup before failing only on generated managed intermediates in staged public-source hygiene.

## 1.1.0-rc.3

- Fixes the Windows behavioral-test gate exposed by RC.2: `test.ps1` now builds the complete `VESPERA_TESTS_ONLY` tree before running all registered CTest tests, instead of building only `vespera_engine_logic_tests` and leaving the editor-support/platform-process executables missing.
- Persists complete CTest output to `build-tests/ctest-output.log` and replays it through PowerShell so failures are visible in the RC transcript even under Windows PowerShell 5.1.
- Fixes `test-rc.ps1` automated-gate result handling by consuming success-stream output at the host; a failing gate can no longer return a non-empty output array that PowerShell mistakenly treats as a successful Boolean result.
- Manual D3D12/Vulkan visual smokes therefore remain skipped after an automated RC failure, preserving the original failure instead of launching unrelated GUI checks.
- Adds source/release-tooling regression contracts for behavioral-test completeness, CTest diagnostics, and scalar RC pass/fail handling.
- Keeps renderer/editor/runtime implementation, serialized formats, MCP tool count, and managed ABI unchanged from RC.2.

## 1.1.0-rc.2

- Hardens the Windows RC test harness after RC.1 could collapse a staged-public validation failure into only a generic exit-code message.
- Writes a full `test-rc.ps1` PowerShell transcript under the system temp directory and prints its path at start and on failure.
- Runs direct public-source validation before GUI automation, then retains the stricter clean staged-public validation later in the gate.
- Makes staged-public preparation replay the validator's complete stdout/stderr and retain the failed staged tree for inspection instead of hiding the underlying reason.
- Clearly warns when the editor is being opened under automation so an automation-controlled window is not mistaken for a manual smoke test.
- Prevents the release-tooling regression suite from creating `__pycache__` debris in an otherwise clean source tree.
- Excludes versioned handoff notes and common desktop metadata from public-source staging while retaining the real source package contents.
- Keeps renderer/editor architecture, serialized formats, MCP tool count, and managed ABI unchanged from RC.1.

## 1.1.0-rc.1

- Adds a top-level `test-rc.ps1` runner for the full Windows release-candidate check. It streams plain `[PASS]`, `[FAIL]`, and `[SKIP]` lines in PowerShell, runs the existing automated RC gate once, then launches D3D12/Vulkan editor and reference-game Release visual smokes for human pass/fail marking.
- Makes `run-game.ps1` configuration-aware so Release RC testing launches the Release reference executable instead of silently falling back to Debug.
- Propagates Debug/Release intent consistently through source-tree managed C# builds (`build.ps1`, `run.ps1`, `run-project.ps1`, `export.ps1`, and the RC managed stage) instead of compiling managed code as Debug during a native Release workflow.
- Fixes the RC gate's post-QA public hygiene phase: it now validates a freshly staged public tree rather than rejecting the gate's own `build`/`build-tests` outputs.
- Expands release-tooling/source-contract coverage for the user-facing RC runner, Release reference-game path, managed configuration propagation, and staged post-QA public validation.
- Keeps renderer architecture, scene/prefab/project/package formats, MCP tool count, and managed ABI unchanged from alpha.23.

## 1.1.0-alpha.23

- Hardened the 1.1 cross-platform release gate without changing scene, prefab, project, package, or managed ABI formats.
- Added deterministic installed-distribution root-name validation so renamed/mispackaged portable roots are rejected even when their internal manifest is otherwise valid.
- Expanded release-tooling regression coverage to verify exact package versions and reject valid-looking distributions under the wrong archive root.
- Added a dependency-light `test-linux.sh` gate for strict C++20 logic tests plus source, release-tooling, public-tree, and version validation.
- Hardened Linux build/run/export scripts with tool/configuration/binary/project preflights and explicit Vulkan editor/runtime launching.
- Added release CI source/public/tooling preflights and exact-version validation for Windows portable/installer and Linux portable artifacts.
- Refreshed current 1.1 platform/boundary documentation and removed stale experimental-era comments from active CMake configuration.

## 1.1.0-alpha.21 — distribution/release-path hardening

- Made official release assembly discard stale native build trees before configuring so published binaries cannot inherit an old CMake cache or prior-checkout objects.
- Hardened portable archive validation to require exactly one top-level distribution root and reject unsupported ZIP/TAR entry types.
- Tightened distribution manifest validation so declared Hub/editor/player/builder/.NET paths must match the supported installed layout.
- Generalized source-archive hygiene so arbitrary `build*`, `release-*`, and `dist-*` trees and symlinked files are excluded instead of relying on a short hard-coded build-directory list.
- Strengthened Linux release-asset CI: the assembled distribution now launches the packaged Vulkan editor and the packaged exported game under software Vulkan/Xvfb, in addition to structural and Build Game validation.
- Added release-tooling regression coverage for extra archive roots and arbitrary generated build-directory leakage.
- Kept scene/prefab/project/package formats, managed ABI, renderer architecture, and normal Windows D3D12 runtime behavior unchanged.

## 1.1.0-alpha.20 — release-tree consistency + documentation hardening

- Cleans generated Python cache debris from the source package, removes a personal-name example from Markdown/site/search-index runtime-UI documentation, and extends public hygiene scans to HTML/JS/CSS surfaces.
- Fixes the public-release validator to require the real `website/build-and-ship.html` page and aligns the formal experimental 2D product label with validation.
- Synchronizes the 1.1 platform story across README/Markdown docs/website search surfaces: Windows x64 + D3D12 remains production-supported, Vulkan is an active preview, and Linux + Vulkan remains experimental pending broader validation.
- Updates stale MCP/version-facing documentation and refreshes the roadmap/architecture overview around the already-implemented renderer, editor-host, automation, and distribution boundaries.
- Preserves scene, prefab, project, package, stable component identifiers, MCP tool count, and managed ABI behavior; this checkpoint is release/tooling/documentation hardening rather than a runtime feature redesign.

## 1.1.0-alpha.14 — Vulkan editor + prebuilt distribution hardening

- Makes the Vulkan editor host selectable on Windows while keeping D3D12 as the default, so the same Scene/Game preview path used by Linux can be validated on real Windows Vulkan hardware.
- Moves Vulkan world lighting to the checked-in per-pixel shader path and keeps render-pass/pipeline objects stable across ordinary swapchain resizes so the editor overlay remains compatible.
- Moves writable editor state and C# Play output out of the Vespera installation directory into user/project-owned locations, including project-local `.vespera/managed`.
- Expands the native VesperaBuilder path for Build C#, Build Game, Linux export, and source-tree launch helpers.
- Adds reusable assembled-distribution validation plus CI smoke coverage for the installed Linux Hub/Editor and release-stage tool identities.
- Continues the prebuilt Windows portable/installer and Linux tarball release pipeline; final cross-platform hardware validation is still pending.


## 1.1.0-alpha.13 — platform, Vulkan, and distribution checkpoint

- Adds native builder/distribution groundwork for prebuilt Vespera packages.
- Adds installation-aware editor/Hub discovery and cross-platform process support.
- Expands Linux build/export/CI paths and portable managed-host handling.
- Begins the Vulkan per-pixel lighting and Linux/Vulkan editor-host integration.
- This is a durable development checkpoint; Windows/Linux end-to-end validation is still pending.

## 1.1.0-alpha.12 — editor UI language + public-surface cleanup

- Removed stale milestone, QA, renderer-backend, and old Sectorline implementation labels from normal editor UI.
- Simplified asset refresh/build diagnostics so the default Console and Project panel emphasize actionable issues instead of internal counters.
- Cleaned Project Settings, Build Game, asset inspection, extension/MCP menus, and Play Mode status copy without changing serialized formats or runtime behavior.
- Preserved advanced asset IDs and hashes behind clearer labels/technical details while keeping contributor and automation surfaces intact.
- Renamed the reference-game `GameplayApiDogfood` component to the public-facing `GameplayShowcase` without changing its sample behavior.

## 1.1.0-alpha.10 — editor hardening + UI cleanup

- Validates the decomposed editor architecture on Windows after repairing extraction-boundary include/declaration issues.
- Fixes editor Game-view texture alias resolution so friendly serialized names such as `Floor Tiles` resolve to catalog assets such as `floor_tiles` without rewriting saved scene names.
- Hardens Play Mode relative-mouse release across Alt-Tab/focus loss by releasing capture on the window that originally acquired it.
- Centralizes semantic editor colors and reduces per-panel one-off styling.
- Reworks the workspace toolbar into clearer project, transport, and build/action groups and adds a compact bottom status bar for mode, dirty state, selection, assets, build, and automation state.
- Cleans up Build/Tools menu ownership, Hierarchy and Console toolbars, Project browser headings, Inspector empty/asset states, Game-view runtime status, and Scene-view control grouping.
- Preserves scene/project/prefab/package formats and the managed ABI.

## 1.1.0-alpha.6 — lighting parity + Linux execution foundation

- Fixes the major Vulkan/D3D12 brightness mismatch found in side-by-side Windows validation by preferring UNORM Vulkan swapchain formats, matching the D3D12 reference backbuffer transfer behavior instead of applying an extra sRGB presentation conversion.
- Logs the selected Vulkan swapchain format/present mode for easier renderer-parity diagnosis.
- Adds a cross-platform hostfxr loader for managed C# on Linux using `libhostfxr.so`/`dlopen` while preserving the existing Windows hosting path and Managed ABI v12.
- Adds `tools/build-managed-runtime.py` for atomic cross-platform standalone managed staging without requiring PowerShell.
- Adds `build-linux.sh`, `run-game-linux.sh`, and `run-hub-linux.sh` development helpers.
- Adds `VESPERA_SMOKE_FRAMES` finite-run support so player/Hub/editor CI can launch real graphical paths and exit deterministically.
- Expands Linux CI to build the full runtime/editor stack with RmlUi + Lua, stage C#, and smoke the Vulkan reference game, Project Hub, and editor shell under Xvfb + Mesa software Vulkan.
- Linux runtime logs use `XDG_STATE_HOME/vespera/logs` (or `~/.local/state/vespera/logs`) when available.
- D3D12 renderer code, serialized formats, package formats, and Managed ABI are unchanged.

## 1.1.0-alpha.5 — Vulkan runtime parity pass

- Added Vulkan rendering for authored primitive mesh entities using hierarchy-resolved transforms, materials, texture layers, and frustum visibility accounting.
- Added animated billboard sprite rendering with authored clips, alpha blending, back-to-front ordering, frustum culling, and depth testing without transparent-pixel depth writes.
- Added bounded point-light contribution and point-light telemetry on the Vulkan path. Lighting is evaluated per vertex on the CPU in this portability checkpoint; exact D3D12 per-pixel lighting parity remains follow-up work.
- Added runtime UI packet rendering through a Vulkan UI atlas, descriptor set, alpha-blended pipeline, and non-depth-tested screen-space draw path. This covers both legacy runtime UI packets and RmlUi packets produced by the shared runtime surface.
- Switched Vulkan dynamic geometry uploads to append within each frame so multiple scene/UI submissions do not overwrite earlier draw data before GPU submission.
- Added separate opaque, transparent-sprite, and runtime-UI Vulkan pipelines while retaining the existing sampled texture-array material path.
- Extended Vulkan renderer statistics for scene passes, primitive/sprite visibility and draw calls, active point lights, culled lights, and UI draw calls.
- D3D12 remains the automatic Windows backend and its implementation is unchanged by this checkpoint.
- Scene, prefab, project, package, `.vmeta`, legacy UI, and managed ABI formats are unchanged.

## 1.1.0-alpha.4 — Vulkan world textures + reference-game backend selection

- Added Vulkan world texture-array upload, shader-readable image transitions, a repeating point sampler, combined image-sampler descriptors, and descriptor binding for sector-world draws.
- Extended the Vulkan bootstrap shaders/vertex payload with UV and texture-layer data; sampled texture color is multiplied by the existing renderer-neutral sector material tint.
- Keeps layer zero as a white fallback and uses a visible checkerboard for invalid/mismatched texture-array layers, matching the established D3D12 world-texture behavior.
- Added `run-game.ps1 -Renderer auto|d3d12|vulkan|null` and matching reference-game command-line parsing so Vulkan can be tested with the normal WASD/mouse gameplay controller instead of the shared-player static camera.
- Preserves the existing Vulkan geometry/depth/swapchain path and the stable Windows D3D12 automatic backend.
- No scene/project/prefab/package format or managed ABI changes.

## 1.1.0-alpha.3 — Vulkan world geometry + depth

- Adds Vulkan sector-world rendering through the shared `RenderBackend` path.
- Adds a depth attachment per swapchain image with depth testing and depth writes enabled.
- Adds bootstrap vertex/index buffers and indexed triangle submission for sector meshes.
- Adds source-controlled bootstrap SPIR-V generation plus CI `spirv-val` validation.
- Keeps textures/materials, sprites, primitive mesh entities, lighting, runtime UI, and Vulkan editor integration explicitly deferred to later renderer-parity checkpoints.
- No serialized formats or managed ABI fields change.

## 1.1.0-alpha.2 — Vulkan bootstrap / first presented frame

- Adds a dynamically loaded Vulkan backend using SDL3 for loader and window-surface integration; no machine-wide Vulkan SDK is required at runtime.
- Fetches pinned Khronos Vulkan-Headers at build time and keeps Vulkan optional through `VESPERA_VULKAN_RENDERER`.
- Adds physical-device selection, graphics/present queue discovery, `VK_KHR_swapchain`, surface-format/present-mode selection, image views, a clear-only render pass, framebuffers, command buffers, semaphores, and fences.
- Adds swapchain recreation for resize/out-of-date/suboptimal presentation and VSync policy changes.
- Vulkan produces a synchronized clear/present frame through the shared `RenderBackend` API; scene geometry and runtime UI remain no-ops in this checkpoint.
- Windows resolves `auto` to Direct3D 12; Linux resolves `auto` to Vulkan when the backend is compiled.
- No serialized formats or managed ABI fields change.

## 1.1.0-alpha.1 — renderer/backend portability foundation

- Separates renderer selection/factory logic from the Direct3D 12 implementation so additional backends can coexist without changing scene/game APIs.
- Adds stable backend identities for Automatic, Direct3D 12, Vulkan, and Null rendering.
- Adds shared-player `--renderer auto|d3d12|vulkan|null` selection with clear rejection when a requested backend is not compiled into the build.
- Keeps Direct3D 12 as the automatic Windows backend and retains the Null renderer as the non-GPU fallback used by portability work.
- Adds GCC and Clang Linux CI coverage for the renderer-independent behavioral suite.
- No project, scene, prefab, package, or managed ABI format changes.

## 1.0.0 — Windows / D3D12 production release tree

- Promoted the Windows-validated `1.0.0-rc.1` baseline into the exact 1.0 public release tree; no scene/project/prefab/package format or managed ABI bump is introduced here.
- Added the final MIT license for Vespera Engine and a dedicated third-party notices file.
- Shared-player builds now stage Vespera legal notices beside the runtime, and packaged games carry them under `legal/` with package self-check coverage.
- Turned **2D / UI Foundation (Experimental)** into a distinct playable screen-space starter driven by project-owned C# + RmlUi while keeping full orthographic world authoring explicitly post-1.0.
- Added a dependency-free GitHub Pages documentation website and a concise 1.0 limitations page.
- Added public-release hygiene validation for missing PowerShell helper scripts, personal/machine paths, generated debris, broken documentation links, licensing files, and the 2D template/public platform claims.
- Public source packaging excludes internal handoff/hotfix/cache/build debris and validates the copied tree before it can be treated as release-ready.
- Folded the RC QA fixes into source: bounded scale stress restores by reopening the clean scene, and destructive reference-project QA runs against a disposable copy.

## 1.0.0-rc.1 — feature-freeze release candidate

- Promotes the Windows-validated 0.16.2 release-hardening baseline into the first SemVer prerelease candidate. CMake keeps numeric `1.0.0` components while the public engine/MCP/export label is `1.0.0-rc.1`.
- Adds `tools/run-rc-gate.ps1`: one Release build/public-workflow gate followed by adversarial QA against the same binaries, avoiding repeated native rebuilds during RC validation.
- RC QA raises Play/Pause/Step/Stop torture to a configurable 2..64 cycles, adds repeated saved-scene switch/restore validation through the public editor API, and can run exported-runtime automation against the already-built Release configuration.
- The RC gate also includes managed compiler-error recovery, stable-asset move/save/reopen repair, runtime telemetry/input, MCP async managed-build polling, and the existing bounded 150-entity stress workload.
- Vespera startup branding now remains visible for a minimum **4 seconds** in Application-hosted runtimes and the native editor; real startup/loading time counts toward the interval, and SDL events continue pumping during any remaining hold so Windows does not present the app as frozen.
- No scene/prefab/project/package-manifest/managed-ABI/VAP/MCP format or tool-count change. Feature work is frozen unless a release blocker requires it.

## 0.16.2 — source-package/release-gate completeness hotfix

- Restores the required `tools/build-managed-editor.ps1` helper that was accidentally omitted from the distributed 0.16.1 source ZIP.
- `build.ps1` now fails immediately if the managed build helper is missing, before starting an expensive native build.
- `tools/run-release-gate.ps1` now validates critical source/package files before running tests or building Release.
- Retains the 0.16.1 portable .NET root-precedence fix; no serialized-format or public API change.

## 0.16.1 — portable .NET selection hotfix

- Fixed Windows portable export honoring the wrong .NET installation when an explicit `-DotnetRoot`/`ProjectPackageOptions::dotnet_root` was supplied. Candidate roots now preserve caller/environment/default priority instead of being lexicographically sorted.
- Hardened the portable-package regression with a competing ambient runtime so explicit-root precedence is deterministic across platforms.
- `test.ps1` now prints the tail of CTest `LastTest.log` automatically on failure before throwing.
- No serialized format, managed ABI, VAP, MCP, renderer, gameplay, or package-manifest version changes.

## 0.16.0 — release + clean-machine hardening

- Fixed the 0.15.2 behavioral-test heap-use-after-free: the hierarchy regression test now reacquires a root entity by stable ID after vector growth instead of retaining an invalidated reference. Runtime hierarchy semantics were not the fault.
- Hardened project packaging before output mutation: managed projects require a complete staged runtime (`Vespera.Managed.runtimeconfig.json`, `Vespera.NET.dll`, and the authored game assembly), and completed packages self-check the project, executable, managed payload, portable hostfxr/runtime, and shared-player branding closure.
- Moved shared-player branding/support staging into the C++ packager so Build Game, direct packaging/MCP, and `export.ps1` produce the same runtime payload instead of relying on a PowerShell-only post-copy step.
- Release packaging patches only the **copied shared-player package executable** to the Windows GUI subsystem. Source-tree Release runtimes keep their console for `run-project.ps1` diagnostics; shipping Release packages no longer open an unwanted console window.
- Shared-player runtimes mirror log output to `%LOCALAPPDATA%\Vespera\Logs\<executable>.log` on Windows (package-local `logs/` fallback elsewhere) and catch/log top-level C++ fatal exceptions; a Windows unhandled-exception filter appends an emergency exception code/address marker for hard crashes.
- `export.ps1` requires an explicit project unless the intentional QA fallback switch is supplied, requires the exact shared-player runtime path, and rejects ambiguous custom-target executables rather than choosing one stale artifact.
- Added Project Hub QA/CI creation arguments that reuse `create_project_from_template(...)` without opening the Hub window. Normal users still use the visual Hub.
- Added `tools/run-release-gate.ps1`: one Release build/test gate that creates a fresh 3D starter, exports it Release + portable, validates/launches the package and runtime log, then exports/starts Emberlight Guild and Performance Lab through the ordinary shared-player path.
- Made the 1.0 2D scope explicit: the existing starter is now **2D / UI Foundation (Experimental)**. Dedicated orthographic 2D world/editor tooling is not advertised as part of Vespera 1.0.
- Scene v15, Prefab v6, Project v10, `.vmeta` v3, legacy `.slui` v3, Package Manifest v3, managed ABI v12 / 88 fields, VAP 60 and MCP 71 remain compatible.

## 0.15.2 — runtime batching + renderer telemetry

- Advanced Project Text to v10 with project-owned VSync policy; older projects default to VSync on, while Performance Lab runs uncapped.
- Added renderer submission/visibility/light telemetry and exposed it through managed `PerformanceSnapshot`.
- Advanced managed ABI to v12 / 88 ordered fields with bulk transform writes plus performance snapshots; Performance Lab batches its animation writes.
- Added D3D12 primitive and billboard-sprite instancing after conservative frustum culling.
- Replaced per-frame quadratic managed-script attachment reconciliation with an allocation-free stable fast path and hashed mismatch recovery.
- Kept point-light selection bounded to a small candidate reserve and partial-sorts only the nearest 32 visible lights instead of allocating/sorting proportional to total scene size.
- New in-memory projects now report the current Project Text v10 format immediately; older v1-v9 files still load and upgrade in memory with VSync defaulting on.
- Expanded Performance Lab HUD/results with update/render/draw/visibility counters and timestamped JSON/CSV benchmark exports.
- Made direct `export.ps1` require an explicit project; the historical QA reference-game fallback now requires `-AllowReferenceGameFallback`.
- Scene v15, Prefab v6, `.vmeta` v3, `.slui` v3, Package Manifest v3, VAP 60 and MCP 71 are unchanged. Windows/MSVC/D3D12 validation is still required.

## 0.15.0 cinematic Performance Lab / startup presentation revision

- Rebuilt Performance Lab as a visible scripted fly-through instead of a mostly static authored cube field. The runtime now generates roughly 13K individual mesh renderables at Torture, updates more than 9K transforms per frame through the public C# API, moves all 32 point lights, and flies the camera through four progressively denser gallery zones.
- Added public C# active-camera access (`Camera.State` / `Camera.SetPose`), advancing the managed ABI to v11 / 86 ordered native+C# fields. Editor Play yields its reference first-person camera controller on frames where managed gameplay explicitly writes the camera.
- Application-hosted startup branding and the native editor now keep the Vespera splash visible for at least 1.25 seconds, counting real load time toward that minimum instead of adding a fixed delay after slow starts.
- Scene/Prefab/Project/metadata/package formats, VAP 60 and MCP 71 are unchanged. Windows/MSVC runtime validation is still required for this working revision.

## 0.15.0 — performance + presentation hardening

- Removed the editor's hidden dependency on `examples/reference_game/reference_textures.hpp`; opened projects now import texture assets from their own AssetCatalog instead of registering reference-game filenames such as `floor_tiles.bmp`.
- Added resilient scene texture loading with an obvious checkerboard placeholder. Missing/undecodable project textures warn and preserve the authored texture name instead of aborting the project/startup scene.
- Play Mode now refuses to silently run the editor's empty scratch scene when no project or explicitly opened scene exists.
- Added small-window optimized Vespera PNG/BMP icons and rebuilt the Windows ICO with 16-256 px representations.
- Project Hub startup now uses the Vespera splash + logo sting; the native editor presents the splash during real startup work without adding an artificial delay or replaying the Hub sting.
- Added `examples/performance_lab` as a normal Project v9/shared-player benchmark with cumulative Baseline / Gameplay / Heavy / Torture presets, hundreds of entities, managed update load, 32 point lights, live frame metrics and a fixed-duration full benchmark report.
- Added source/behavior regression coverage for starter/reference-texture decoupling and the Performance Lab build closure.
- Scene/Prefab/Project/metadata/package formats, managed ABI v10/84, VAP 60 and MCP 71 are unchanged.

## 0.14.1 — Windows editor build hotfix

- Fixed the Windows/MSVC editor build blocker where `RuntimePerformanceCounters` was used without including `<vespera/core/game.hpp>`.
- Added a source-contract regression guard for the explicit performance-counter include.
- No runtime, format, ABI, asset, sample, or automation-surface behavior changes from 0.14.0.

## 0.14.0 — standalone sample dogfood

- Added `examples/emberlight_guild` as a normal Project v9 game sample with a project-owned C# assembly, startup scene, RML/RCSS UI and SaveData-backed guild progression.
- The sample uses the shared player and normal deterministic Build Game/export closure rather than a bespoke sample executable.
- Behavioral coverage now verifies the sample has no project-validation errors and that its startup scene -> RML -> RCSS closure is complete.
- The reference game remains the adversarial QA target; the native RmlUi spike remains a low-level integration showcase.

## 0.13.0 — first-hour and release-readiness

- Added concise external-user `Getting Started`, `First Game`, and `Build & Ship` guides so normal game developers do not need internal contributor QA documents.
- Build Game preflight now lists concrete project validation issues, missing roots, broken dependencies and stale build-root fallbacks in-editor before packaging.
- Release preflight notes when the project has no authored game icon and will use the Vespera fallback.
- 0.12.0 naming compatibility and serialized identifiers remain unchanged.

## 0.12.0 — controlled Vespera internal naming migration

- Renamed the primary C++ namespace from `sectorline::` to `vespera::` and moved public headers to `<vespera/...>`.
- Renamed primary CMake targets/executables to `vespera_engine`, `vespera_editor`, and `vespera_reference_game`.
- Preserved deprecated CMake target aliases and generated forwarding headers under `<sectorline/...>` so pre-1.0 integrations have a transition path.
- Renamed public build options to `VESPERA_BUILD_*` / `VESPERA_WARNINGS_AS_ERRORS` while accepting old cached `SECTORLINE_*` options with deprecation messages.
- Preserved serialized `sectorline.*` component keys and `sectorline_scene` / `sectorline_prefab` format magic intentionally; those are compatibility identifiers, not current branding.
- `VESPERA_GPU_VALIDATION=1` is now the preferred D3D12 validation switch; the old environment variable remains accepted as a deprecated alias.

## 0.11.0 — pre-RC reliability telemetry

- Added lightweight frame performance counters to Application-hosted runtimes and the Windows editor.
- `vespera_get_state` and standalone runtime state now report frame timing, scene/component counts, and asset/dependency counts without adding another MCP tool.
- Added reusable scene scale statistics and behavioral regression coverage.
- Added optional `run-qa.ps1 -Stress`: authors 150 primitives through the public VAP command path, mixes colliders/lights, enters Play, captures telemetry, stops, and cleans the scene back to its original entity count.
- 0.10.0 remains feature-frozen; 0.11.0 begins the pre-RC reliability/scale phase.

## 0.10.0 final — scripting/UI convergence freeze

- Promoted the 0.10.0 line to a suffix-free checkpoint; intermediate a/b/c source checkpoints are consolidated here.
- Editor Play and the shared player use project RML through the engine-owned `UiSurface`; legacy `.slui` remains compatibility/QA only.
- Added the Project v9 lightweight Lua 5.4.9 runtime/mod path alongside C#, with shared scene/input/audio/assets/UI semantics and reference-runtime QA dogfood.
- Added in-editor RML/RCSS source authoring plus dependency/broken-reference diagnostics and active-Play hot reload on document/stylesheet saves.
- Project v9 loads v1-v9; managed ABI remains v10/84; VAP remains 60 commands; MCP remains 71 tools; cheap behavioral coverage remains 38 cases.
- 0.10.0 is feature frozen; next work is pre-RC stress/telemetry, controlled internal naming cleanup, external first-game/shipping docs, and concentrated QA.

## 0.10.0c — RmlUi source authoring + diagnostics

- Added a dedicated editor RML/RCSS source editor module instead of growing `editor/main.cpp` further.
- Selected `.rml` / `.rcss` assets now report dependency counts and broken local references in the normal Asset Inspector.
- `Open RML / RCSS Source` provides Save/Revert and preserves unsaved edits when reopening the same asset.
- Saving the active Play RML document, or an RCSS dependency used by it, reloads the live `RmlUiSurface`.
- Legacy `.slui` visual authoring is retained for compatibility/QA but is labeled as legacy; new project UI should use RML/RCSS.
- Project remains v9; managed ABI v10/84, VAP 60, MCP 71 and the 38-case cheap suite are unchanged.

## 0.10.0b — Lua runtime scripting foundation

- Embedded pinned Lua 5.4.9 behind `VESPERA_LUA_SCRIPTING`; C# remains the primary full-game scripting surface.
- Project Text advances to v9 (loads v1-v9) with an optional stable `lua_entry` reference that is a deterministic build root and participates in controlled move repair / safe-delete preflight.
- Added `LuaScriptHost` with optional project-level `Start()`, `Update(dt)`, and `Stop()` lifecycle and a restricted standard-library environment.
- Lua reuses Vespera Scene/Input/Audio/Assets/`UiSurface` semantics and generic component-property metadata rather than defining a separate gameplay API.
- Shared player, Editor Play and the reference QA runtime can run the same Lua entry alongside C#. The reference project now dogfoods Lua and C# together.
- Project Settings and the existing project automation surface can set/clear the Lua entry. `.lua` is a first-class AssetCatalog kind/importer.
- Added Lua asset/build-root and stable move/delete behavior coverage; cheap suite expands to 38 cases.
- Managed ABI remains v10 / 84 fields; Scene v15, Prefab v6, `.vmeta` v3, `.slui` v3, Package Manifest v3, VAP 60 and MCP 71 are unchanged.

## 0.10.0a — Editor Play RmlUi convergence

- Editor Play now resolves Project v8 startup RML with the same runtime policy as `vespera_player`.
- The Game view renders an owned `RmlUiSurface` and routes it to managed C# through `UiSurface`.
- Embedded Play forwards RmlUi pointer, mouse-button, navigation-key and text input through `InputSystem`.
- Existing Editor VAP/MCP runtime pointer/navigation/text/state commands now operate against either active RmlUi or legacy `.slui` without a parallel tool set.
- Legacy `.slui` Play rendering remains as reference-QA/compatibility fallback; automation migration continues next.
- No serialized-format or managed-ABI change.

## 0.9.9 final — branding/startup polish + feature freeze

- Promoted the 0.9.9 line to a suffix-free final checkpoint; the next development milestone is 0.10.0 rather than another 0.9.9 letter build.
- Added shared Vespera branding assets: filled app icon, multi-size Windows icon resource, project-loading splash art, and the cropped startup logo sting (`vespera_logo_sting.wav`).
- `ApplicationConfig` can now provide a window icon plus a startup splash/sound. The splash is presented once before synchronous game/project startup work and remains visible while loading; there is no artificial minimum delay.
- The shared `vespera_player` uses the project game icon when authored and otherwise falls back to the Vespera icon; shared-player builds show the Vespera loading splash and play the logo sting while project startup work runs.
- Project Hub, RmlUi showcase, editor and shared player now consume the same shared branding source; Windows executables embed the Vespera icon resource.
- Exported shared-player packages copy the branding payload required by the player startup sequence.
- 0.9.9 is feature-frozen after the fresh-project -> project-owned C# -> RmlUi -> shared player -> in-editor Build Game/export loop. Remaining RmlUi editor-authoring migration and Lua move to 0.10.0.

## 0.9.9g in-editor Build Game / Build & Run

- Added a first-class **Build Game** editor window for Debug, Development and Release packaging; normal users no longer need to type `export.ps1` commands.
- Added Build / Build & Run / Open Output Folder actions, project/build-manifest preflight, executable/output/deployment settings, and a rolling in-editor build log.
- Builds run asynchronously through a hidden Windows process so the editor remains responsive while native/C#/package work executes.
- Added Build menu, File-menu entry, Project Settings buttons, workspace-toolbar access and `Ctrl+Shift+B` discovery for the same workflow.
- Build Game saves the current scene/project, refreshes assets and refuses to package invalid project/build closure state.
- Optimized `export.ps1` to reuse an existing multi-config CMake tree and build only the requested runtime target plus `vespera_packager` instead of rebuilding the entire editor/reference checkout on every export.
- Editor runtime discovery now treats an empty `game_target` as the shared `vespera_player`, matching the starter/export model.
- Fixed the native editor OS-window title to show the authored project name instead of `Untitled` when a project is loaded without a scene-path title.
- Project remains v8; managed ABI remains v10 / 84 fields; Scene v15, Prefab v6, `.vmeta` v3, `.slui` v3, Package Manifest v3, 60 VAP commands and 71 MCP tools are unchanged.

## 0.9.9f Project v8 explicit startup RML

- Advanced Project format v7 -> v8 while retaining v1-v8 loading. Saving now emits v8.
- Added optional stable `startup_ui_asset` / path-only `startup_ui` project records for the active RmlUi startup document.
- Shared `vespera_player` resolves explicit startup UI first; projects without it retain the old first-RML-build-root rule only as a compatibility fallback.
- Startup UI is now an automatic deterministic build-manifest root and no longer needs a duplicate `build_include`.
- Safe-delete preflight blocks deleting the active startup UI; stable fallback repair canonicalizes its readable path after ID-preserving moves.
- Normal Project Settings can author/clear the startup RML or adopt the currently selected RML asset; project automation reports `startup_ui` + `startup_ui_asset_id` and can set/clear it using an RML path or stable ID.
- 2D and 3D templates now author `startup_ui_asset`; Empty intentionally has none.
- Removed a duplicate `executable_name` line from project serialization.
- Expanded cheap behavioral coverage to 36 cases, including startup-UI round-trip, build closure, explicit/legacy selection and move/delete repair.
- Widened the guild showcase War Room action so its full label is visible; the validated fixed/shrink Hub and live-resize behavior are otherwise unchanged.
- Managed ABI remains v10 / 84 fields; Scene v15, Prefab v6, `.vmeta` v3, `.slui` v3, Package Manifest v3, 60 editor/VAP commands and 71 MCP tools are unchanged.

## 0.9.9e project-owned starter C# / fresh-project shipping loop

- All three Project Hub templates now ship `managed/VesperaGame.Scripts.csproj`, project-owned C# source, and an authored scene attachment while continuing to use the shared `vespera_player` (`game_target` remains empty).
- The managed build helper now supplies `VesperaSdkProject` explicitly, so Hub-created projects can live outside the engine checkout without hard-coded relative references back to `managed/Vespera.NET`.
- `run.ps1 -Project ...` builds the selected project's managed assembly with last-good staging before editor launch and syncs that project's managed output into the editor, enabling first Play without a manual Build C# step.
- 3D / 2.5D starter: visible C#-controlled cube using the authored WASD/Shift input map, cylinder collision, and an RML HUD driven through the public `UiSurface` managed API.
- 2D starter: project-owned C# + RmlUi click/Enter interaction. Dedicated orthographic world/camera tooling remains intentionally unimplemented rather than being faked through hidden 3D assumptions.
- Empty starter: only an invisible Game Root with a minimal C# bootstrap; no hidden engine gameplay.
- Added real-template behavioral coverage ensuring shipped template configs/sources are present and Hub-style copying preserves the managed project + shared-player model. Cheap suite now contains 34 cases.
- Carries forward the 0.9.9d2 Windows live-resize fix and 0.9.9d3 compact guild control sizing. No serialized format, package manifest, managed ABI, VAP or MCP count change.

## 0.9.9d3 guild RmlUi control sizing regression fix

- Windows validation confirmed 0.9.9d2 fixed live resize and the Project Hub, but the guild showcase's `inline-block; width: auto` form controls collapsed to extremely narrow intrinsic widths in RmlUi and wrapped labels word-by-word.
- Replaced intrinsic auto sizing for guild list/action buttons with compact definite desktop widths and `white-space: nowrap`, while retaining block wrapper rows and `max-width: 100%` shrink safety.
- Roster controls are 220px, mission controls 280px, Start Expedition is 150px, and Upgrade War Room is 190px; none use flex-grow or percentage widths.
- Project Hub layout and the 0.9.9d2 Windows live-resize redraw path are unchanged.
- Added source-contract guards against reintroducing `width:auto` intrinsic sizing on guild buttons. No serialized format, managed ABI, asset, player/export, VAP or MCP contract change.

## 0.9.9d2 Windows live-resize + content-sized RmlUi controls

- Root-caused the remaining resize artifact to the generic SDL `Application` host, not RmlUi layout: on Windows the native sizing loop blocks normal frame presentation, so DWM stretched the last backbuffer until the mouse button was released.
- Added the editor-proven `SDL_EVENT_WINDOW_EXPOSED` event-watch redraw path to the generic `Application` host. Resizable Application-based windows now resize the renderer and present a fresh frame during the drag instead of snapping to the correct layout only after release.
- Changed Project Hub primary/secondary action buttons from fixed 190/210px widths to content-sized `inline-block` controls.
- Changed guild member/mission controls to content-sized `inline-block` buttons inside explicit block rows, preserving one-control-per-line layout without forcing 230/310px widths.
- Removed the guild action-button minimum width and reduced the progress track independently; selected/focus/hover behavior is unchanged.
- Added source-contract guards for the generic live-resize watcher and content-sized controls.
- No serialized format, managed ABI, asset, player/export, VAP or MCP contract change.

## 0.9.9d1 fixed-desktop RmlUi layout hotfix

- Root-caused the remaining Project Hub and guild-showcase stretching to authored grow-to-fill RCSS, not the RmlUi adapter: the Hub still used a growing New Project panel / flex template cards, while the showcase still used percentage/flex panel widths.
- Changed both desktop tools to **preferred fixed widths with shrink-only behavior**: extra window space becomes margin instead of being redistributed into panels and controls.
- Project Hub now uses a 1070px preferred content shell, 300px Recent panel, 680px New Project panel, 200px template cards, and 520px fields; these may shrink on narrower windows but do not grow on wider windows.
- Guild showcase now uses a 1050px preferred shell with 280/430/270px panel widths, 230px roster rows, 310px mission rows/progress, and a 230px guild-name field. The body no longer consumes extra vertical height through `flex: 1`.
- Added source-contract guards against reintroducing percentage/flex-grow desktop sizing for these two RmlUi surfaces.
- No renderer, `UiSurface`, managed ABI, serialization, assets, player/export, VAP or MCP behavior changed.

## 0.9.9d shared player / generic export foundation

- Added the reusable `vespera_player` runtime host. It accepts `--project` for source runs or discovers the single `.vesperaproject` beside an exported executable.
- Shared player loads project-owned window/input settings, stable assets, startup scene/materials, optional RML startup UI and optional C# assembly through the existing ABI v10 / `UiSurface` path.
- `export.ps1` now falls back to `vespera_player` when `game_target` is empty; custom runtime targets remain supported.
- Arbitrary project-owned managed projects can be built for export with the last-good helper using `-NoMirrors`, avoiding reference-game editor/runtime mirror side effects.
- Added `run-project.ps1` for launching source projects through the shared player.
- Added player-project discovery and startup-RML selection behavior tests; cheap suite now contains 32 cases.
- Carries forward the 0.9.9c1 guild roster/mission width fix. No Scene/Prefab/Project/.vmeta/.slui/Package Manifest or managed ABI format change.

## 0.9.9c1 RmlUi responsive-list hotfix

- Windows validation of 0.9.9c passed end to end, including managed ABI v10 / 84-field dogfood and packaged-runtime automation.
- Root-caused the guild showcase width issue to authored RCSS: `.member` / `.mission` were explicitly `width: 100%` inside responsive percentage/flex panels, so RmlUi correctly expanded them as the window widened.
- Kept the list rows responsive on narrow windows but capped roster controls at 260px and mission controls at 340px so they stop growing on desktop-sized panels.
- Raised the War Room action minimum to 210px so its label no longer wraps at the generic 150px action minimum.
- Added a source-contract guard for these responsive ceilings. No engine/UI façade, ABI, serialization, asset, VAP or MCP behavior changed.

## 0.9.9c managed UiSurface bridge

- Routed all managed C# runtime UI callbacks through the engine-owned `UiSurface`; `ManagedScriptHost` no longer owns or mutates `UiDocument` / `UiRuntimeState` directly.
- Added `UiHandleTable` so public C# `UiElement.Id` values are opaque Vespera handles rather than backend-specific `.slui` node IDs or RmlUi string IDs. Handles revalidate element existence against the active surface.
- Expanded the semantic surface for visibility, interactability, read-only/value, numeric value, colors, image/font assets, focus/blur and click consumption. Backend-specific unsupported operations fail explicitly.
- Advanced the managed ABI from v9 / 80 fields to **v10 / 84 ordered fields**, adding string value get/set plus generic property/class mutation. Source validation still checks C++/C# field-name order exactly.
- Added C# `UiElement.ValueText`, `SetProperty`, and `SetClass`. RmlUi can now satisfy managed element semantics without leaking RmlUi types into Vespera.NET.
- Editor Play and the reference runtime construct `LegacyUiSurface` around the existing `.slui` HUD, preserving current regression behavior while proving the managed host is backend-neutral.
- Reference-game C# dogfood now sets/reads progress through `ValueText`, exercising the new v10 bridge in normal runtime QA.
- Expanded cheap behavioral coverage from 26 to 28 cases for opaque handle lifetime/revalidation and managed semantic compatibility.
- Carries forward the 0.9.9b1 Project Hub/guild sizing hotfix for Windows visual verification.
- Scene v15, Prefab v6, Project v7, `.vmeta` v3, `.slui` v3, Package Manifest v3, 60 editor/VAP commands and 71 MCP tools are unchanged.

## 0.9.9b1 RmlUi sizing hotfix

- Windows validation of 0.9.9b passed end to end with `{"passed":true,"failed":[]}`.
- Tightened the Project Hub content width to 1100px, fixed Recent Projects to 320px, capped New Project at 720px, and capped form fields at 560px so the Hub no longer feels stretched on wide displays.
- Kept recent-project rows and template cards intentionally full-width within their columns; they remain selection/list controls rather than compact actions.
- Changed the guild showcase primary/secondary action buttons from block layout to `inline-block`, allowing `width: auto` to shrink-wrap them while member/mission rows remain full-width.
- Added visible focus borders to Hub recent/template/action controls for keyboard navigation.
- Explicit `min-width: 0px` is retained as intent documentation only; RmlUi already defines the initial `min-width` as 0px.
- No `UiSurface`, managed ABI, serialized format, asset, VAP or MCP behavior changed.

## 0.9.9b engine-owned UI surface foundation

- Added renderer/serialization-independent `UiSurface`, the first Vespera-owned semantic UI seam shared by legacy `.slui` and RmlUi.
- Added `LegacyUiSurface`, which adapts existing `UiDocument` + `UiRuntimeState` element-name behavior without changing `.slui` serialization, layout or rendering.
- `RmlUiSurface` now implements `UiSurface` directly while retaining its private PIMPL, input path and `UiRenderPacket` renderer bridge.
- The shared contract intentionally covers gameplay-facing element operations only: lookup, text/value, visibility, disabled state, focus, click state, plus optional generic class/property hooks. It does not fake a common layout/authoring model.
- Legacy visibility changes through the façade clear stale hover/press/focus/capture/click state for the hidden node. CSS-only class/property mutations fail explicitly on `.slui` rather than silently pretending support.
- Expanded the cheap behavioral suite from 23 to 26 cases with legacy semantic-surface, stale-interaction cleanup and unsupported-style-operation coverage.
- 0.9.9a was Windows-validated end to end before this slice. C#/MCP UI still uses the legacy document directly in 0.9.9b; migration through `UiSurface` is intentionally deferred to the next checkpoint so this abstraction can be validated without changing the managed ABI or QA behavior.
- No Scene/Prefab/Project/.vmeta/.slui/package formats, managed ABI fields, VAP command count or MCP tool count changed.

## 0.9.9a RML/RCSS stable-move foundation

- Added a reusable renderer-independent RML/RCSS path-reference scanner/rewriter for local `href`, `src`, and `url(...)` assets.
- Vespera-controlled asset moves now keep ordinary standards-friendly RML/RCSS paths valid by rewriting dependents while the AssetCatalog still knows the moved asset's stable ID.
- Moving an `.rml` or `.rcss` source itself rebases its own local relative references so linked styles/images/fonts keep resolving from the new folder.
- Query/fragment suffixes are preserved, and attribute scanning avoids false matches such as `data-src`.
- Move rewrites are guarded: if an expected RML/RCSS dependent cannot be rewritten safely, the move is refused; if an on-disk rewrite fails after the move, Vespera attempts to restore rewritten files and roll the source + `.vmeta` back.
- Legacy non-RML path-only dependencies remain blocked exactly as before; no engine-specific `asset://` syntax was introduced.
- Expanded the cheap behavioral suite from 16 to 23 cases with RML/RCSS scan, rebase, controlled target-move and unsafe legacy-reference coverage.
- 0.9.8b was Windows-validated before this work: behavioral tests, full VS 2026 build, managed build, integrity, hierarchy, Play cycle, UI layout, managed recovery, asset move/save/reopen, telemetry, MCP managed build, and packaged-runtime automation all passed.
- No Scene/Prefab/Project/.vmeta/.slui/package formats, managed ABI fields, VAP command count, or MCP tool count changed.

## 0.9.8b behavioral test foundation

- Added `test.ps1` and a `VESPERA_TESTS_ONLY` CMake path that configures without SDL3, RmlUi, ImGui, D3D12, the editor, examples, or managed builds.
- Added pinned doctest 2.5.0 for lightweight C++ behavior tests.
- Added regression coverage for InputSystem frame edges/repeats/transient state/action overrides/gamepad semantics, project-authored input configuration, project v7 save/load, project-version diagnostics, and starter-template creation/refusal behavior.
- Kept `tools/validate_source.py` as the static source-contract layer and `tools/run-qa.ps1` as the live Windows editor/runtime/export layer; behavioral tests complement rather than replace either one.
- No Scene/Prefab/Project/.vmeta/.slui/package formats, managed ABI fields, VAP command count, or MCP tool count changed.
- Production RmlUi/Project Hub behavior is intentionally unchanged in this checkpoint; the disputed Hub flex/min-width theory remains a Windows verification item rather than a claimed fix.

## 0.9.8a Project Hub / RmlUi usability hotfix

- Fixed the Windows Project Hub default project directory by resolving the real Documents known folder instead of assuming `%USERPROFILE%\Documents`; this supports OneDrive/Known Folder redirection.
- Project-template directory creation errors now include the exact path that failed.
- Added SDL keyboard-repeat capture to Vespera Input and forwarded repeats into RmlUi, so holding Backspace/arrow keys behaves like a normal text field instead of firing once.
- Added `box-sizing: border-box` to the Vespera RmlUi baselines so percentage-width inputs/buttons no longer grow past their intended boxes when padding/borders are present.
- Project Hub panels no longer stretch vertically with tall windows, and primary/secondary actions use normal action-button widths instead of filling an entire panel.
- Guild showcase height is capped/responsive instead of stretching across tall windows; action buttons are compact while roster/mission rows remain full-width list controls.
- 0.9.8 reference/MCP/runtime QA remained green before this hotfix; 0.9.8a requires a Windows visual/Project Hub regression check.

## 0.9.8a RmlUi/Project Hub usability hotfix

- Adopted RmlUi 6.2 + FreeType 2.14.1 as Vespera's low-level production UI foundation after the successful 0.9.7 Windows evaluation; `.slui` v3 remains compatibility/QA infrastructure during migration.
- Promoted `RmlUiSurface` with reload, form-value, class, disabled and visibility operations while keeping RmlUi behind the Vespera PIMPL/render-packet boundary.
- Added AssetCatalog kinds/importers/dependency scanning for `.rml` and `.rcss`.
- Added the first RmlUi-powered Vespera Project Hub with recent projects, Open Existing and clean New Project flows.
- Added 2D Game, 3D / 2.5D Game and Empty Project starter templates. The 2D template is intentionally a foundation until dedicated orthographic tooling lands.
- Added `vespera.ps1` / `run-hub.ps1` and project-aware editor launch plumbing.
- Preserved 60 editor/VAP commands, 71 MCP tools and managed ABI v9 / 80 fields.

## 0.9.7 RmlUi integration evaluation

- Kept the Windows-validated `.slui` v3 UI/runtime/automation path intact.
- Added an opt-in-by-default build-time RmlUi 6.2 + FreeType 2.14.1 evaluation using pinned FetchContent dependencies.
- Added `RmlUiSurface`, a PIMPL Vespera adapter that feeds Vespera `InputSystem` into RmlUi and captures RmlUi geometry/textures back into the existing renderer-independent `UiRenderPacket`.
- The compatibility capture path atlas-packs generated font/image textures and CPU-clips scissored triangles, so Vespera remains renderer owner instead of adopting RmlUi's sample D3D12 swap-chain backend.
- Added a separate modern guild-management-style RML/RCSS showcase executable and `run-rmlui-spike.ps1`; it does not overwrite or layer on top of the regression reference HUD.
- The showcase exercises real FreeType typography, flex layout, borders/radius, hover/active/focus states, TextInput, buttons and dynamic text/progress updates.
- RmlUi remains an evaluation dependency in 0.9.7, not yet the public Vespera UI format. Production C#/Lua/MCP/editor APIs remain Vespera-owned whichever renderer/layout implementation wins.
- Public engine/MCP version is 0.9.7; MCP surface remains 71 tools.

## 0.9.6a Windows modern-UI build hotfix

- Fixed the Windows-only GDI font rasterizer build failure caused by mixing Win32 `LONG` glyph metrics with integer literals in `std::max`. Glyph width/height now convert the GDI metrics explicitly and use `std::max<int>`, avoiding MSVC template ambiguity while preserving the 0.9.6 UI behavior.
- Added a source-validator regression check for the Win32 metric conversion. No serialized formats, managed ABI fields, automation command counts, or MCP tool counts changed.

## 0.9.6 modern runtime UI / styling

- Advanced `.slui` to v3 while retaining v1/v2 loading. v3 persists surface opacity, corner radius, border/shadow styling, image fit, 9-slice insets and richer text style.
- Reworked the native UI packet builder with rounded surfaces/images, borders, layered shadows, Contain/Cover image behavior, 9-slice rendering, wrapping/line spacing and text shadows.
- Added Windows native font rasterization through GDI, including private loading of project `.ttf`/`.otf` Font assets, UTF-8 decoding and a deterministic legacy bitmap fallback.
- Switched the D3D12 UI atlas sampler to linear filtering for antialiased glyphs and scalable skins.
- Added modern default appearance for newly-authored Panels, Buttons, Progress Bars, Tabs, Modals, Tooltips and Text Inputs.
- Expanded UI Authoring with surface-style and typography controls.
- Expanded `vespera_set_ui_node` with `.slui` v3 style properties and added MCP-only `vespera_apply_ui_batch` for up to 256 aliased UI mutations per tool call.
- MCP bridge now reports 0.9.6 and exposes 71 tools while the editor/VAP surface remains 60 commands.
- Updated the reference HUD to v3 while preserving QA node IDs and upgraded its default runtime presentation.
- Managed ABI remains v9 / 80 fields; Scene v15, Prefab v6, Project v7, `.vmeta` v3 and Package Manifest v3 remain unchanged.

## 0.9.5d runtime-observability / automation QA hotfix

- Added MCP bridge fingerprinting through `vespera_get_bridge_info`; it reports bridge version, MCP tool count and live editor/runtime endpoint versions so stale desktop registrations are detected before QA mutates a project.
- Expanded the editor/VAP surface to 60 commands and the MCP surface to 70 tools. Eight MCP tools route to an opt-in standalone runtime automation endpoint on localhost port 46788.
- Added semantic runtime QA operations for named Input actions, pointer events, focus navigation, basic TextInput editing, UI runtime-state inspection, and structured managed lifecycle/trigger events.
- Advanced managed ABI v8 -> v9 (80 native/C# fields). Runtime telemetry records sequence/frame/assembly-generation/entity/component/callback and survives teardown long enough for Editor Stop assertions.
- Guarded C# source writes/builds are now permitted during Editor Play so the existing collectible game-code auto-reload path can be exercised in Play Mode.
- Coalesced accidental `Scene.Load(currentScene)` requests, added explicit `Scene.Reload()`, and rate-limited pathological same-scene reload bursts with diagnostics.
- Added standalone/reference `--automation` support with localhost-only port 46788; MCP export can opt into runtime automation only when launching a package. Normal shipping runs do not expose the endpoint.
- Added `vespera_set_build_include` and build-include reporting. The reference project explicitly roots `animations/watcher_walk.slspriteclip`, fixing the exported runtime's missing direct-load asset.
- Extended the reference HUD with a basic TextInput dogfood node and added text/backspace/clear handling through the native runtime UI path. Caret/selection/IME and screenshot capture remain future work.
- Added `-RuntimeTelemetry` and `-RuntimeExportAutomation` QA runner coverage; the latter packages and drives a real standalone runtime and verifies the direct sprite-clip root.
- Public package/editor/MCP version label is `0.9.5d`; persisted Scene/Prefab/Project/.vmeta/.slui formats remain unchanged.

## 0.9.5c QA harness hotfix

- Fixed `tools/vespera_qa_runner.py` reusing its 5-second TCP connection timeout for long VAP responses. QA scenarios now have command-aware response timeouts, so `managed_build_recovery` can complete its intentional fail/restore/two-build sequence without a false harness timeout.
- QA timeout diagnostics now distinguish a connected command response timeout from an editor connection failure.
- `integrity-after` now runs after optional managed-recovery and asset-move/save scenarios, so the final integrity gate covers all requested destructive QA mutations.
- Public package/editor/MCP version label is `0.9.5c`; CMake numeric semantic version remains `0.9.5`.

## 0.9.5b QA stabilization hotfix

- Fixed loaded-scene stable fallback synchronization: scene saves now canonicalize stable-ID prefab/material fallback paths against the live AssetCatalog immediately before serialization, preventing a moved prefab's old path from being reintroduced by saving an already-open scene.
- `vespera_build_csharp` is asynchronous by default at the MCP bridge boundary and returns an operation ID immediately; `vespera_get_operation` polls the accepted build to completion. This avoids client/tool-call deadlines incorrectly reporting a successful long C# build as a transport failure.
- `wait_for_completion=true` remains available for MCP clients that can safely wait for the synchronous editor command.
- Public editor/runtime/MCP version reporting now identifies this package as `0.9.5b`; CMake's numeric semantic version remains `0.9.5`.
- Reference project product version updated to `0.9.5b-reference`.
- Added `run-qa.ps1 -McpManagedBuild`, which drives the stdio MCP bridge itself and verifies async Build C# operation polling to completion.
- Added the `asset_move_save_reopen` QA scenario and `run-qa.ps1 -AssetMoveSave` regression switch for the loaded-scene stale-fallback bug.

## 0.9.5a QA stabilization hotfix

- Fixed Windows stable-reference fallback repair by closing descriptor input streams before atomic rename/replace.
- Made editor C# build diagnostics per-invocation so a restored source file cannot replay a previous compiler failure.
- MCP now separates the 2-second connect timeout from command response timeouts; managed builds, QA scenarios and exports can finish without being mislabeled as an unreachable editor.
- Asset fallback repair MCP results now include warning messages as well as the warning count.
- Added project-level QA guidance for repeatable adversarial automation testing.
- Updated the reference project product version to `0.9.5-reference`.
- Engine/runtime feature version remains 0.9.5.

## 0.9.5

- Shifted the milestone into MCP/automation torture testing; 0.9.4 shipping/export is Windows-validated.
- Expanded MCP developer preview from 44 -> 52 tools with Entity metadata/reorder, asset dependency inspection, aggregate project integrity, guarded C# source read/write, managed-script attachment and repeatable QA scenarios.
- `vespera_export_project` can optionally launch the packaged runtime.
- Added bounded hierarchy, Play-cycle and multi-resolution UI QA scenarios using existing editor/native command paths.
- Added guarded managed-source authoring restricted to project `.cs` files.
- No authored format, managed ABI or package-manifest version bump.

## 0.9.4

- Advanced Project format v6 -> v7 while retaining v1-v6 loading. Added executable name, project-relative build output directory, stable game-icon asset reference and Development diagnostics/symbol policy.
- Added real Debug / Development / Release package configurations. Development now maps to native `RelWithDebInfo`; Debug uses Debug and Release uses Release.
- Exported runtime filenames now come from the project Build Settings instead of leaking internal CMake target names such as `sectorline_reference_game.exe`.
- Added stable game-icon participation in deterministic build closure, safe-delete preflight and stable fallback repair. 0.9.4 packages the icon as metadata; Windows executable resource stamping remains future work.
- Advanced package manifest v2 -> v3 and added `Vespera.PackageReport.txt` with configuration/deployment metadata, asset-kind counts, asset/payload byte totals, warnings and exact deterministic closure.
- Expanded `export.ps1` with Debug/Development/Release, project-owned output defaults, Development symbol policy and `-Launch`.
- Project Settings now exposes executable/output/icon/diagnostics Build Settings; Texture assets can be assigned as the project game icon from the Asset Inspector.
- Editor File menu adds Debug export and Export & Launch Development.
- MCP remains 44 tools but `vespera_export_project` now supports all three configurations, project output defaults, and returns runtime/report/byte information.
- Scene remains v15, Prefab v6, `.vmeta` v3, `.slui` v2 and managed ABI v8.

## 0.9.3b Game-view UI input hotfix

- Runtime UI now gets first refusal on left-clicks in the editor Game view while FPS mouse capture is released. Clicking a Button/Text Input no longer simultaneously captures relative mouse.
- Clicking empty Game-view space still captures WASD + mouse look, and Escape still releases it.
- The editor no longer feeds an invisible absolute UI pointer while relative mouse is captured; released Game-view focus can use Tab / Shift+Tab / Enter for UI navigation.
- Standalone projects that intentionally use `relative_mouse 1` remain keyboard/gamepad navigable for UI (Tab/Enter or D-pad/South in the reference game); a general script-controlled cursor/capture API remains a later gameplay-input capability.
- Engine/runtime feature version remains 0.9.3.

## 0.9.3a packaging hotfix

- Fixed the managed UI float getter bridge to write through a stack-local native float before assigning the C# `out` parameter, avoiding an unsafe address-of-ref/out compile failure on MSVC/.NET Windows builds.
- Managed build failures now echo captured compiler diagnostics to PowerShell as well as writing `Vespera.ManagedBuildDiagnostics.txt`, so future C# blockers are visible directly in the normal `build.ps1` log.
- Engine/runtime feature version remains 0.9.3; this hotfix only repairs the packaged managed build path.

## 0.9.3

- Advanced `.slui` to v2 while retaining v1 load compatibility. Added Progress Bar, Scroll View, List, Grid, Tabs, Modal, Tooltip and Text Input foundation nodes.
- Added horizontal/vertical/grid container layout, spacing/cell/column settings, inherited clipping, Scroll View offset and active-tab child selection.
- Added runtime `UiRuntimeState` pointer capture/hover/press/focus/click queue plus host-neutral focus-next/focus-previous/activate navigation state.
- Extended `InputSystem` with absolute mouse position/buttons and `Key::Tab`; standalone UI can map mouse when relative mode is off and keyboard/gamepad navigation without direct SDL access in game code.
- Added a dockable editor UI Authoring window: hierarchy, visual selection/move/resize, anchor presets, rect/margin/padding editing, layout/widget controls, 720p/1080p preview and Texture/Font drag/drop fields.
- Advanced managed ABI v7 -> v8 (79 native/C# fields) with semantic bool/float/color/asset UI callbacks and native click consumption. `UiElement` now exposes progress values, interactability, focus/read-only state, colors, Image/Font refs and `Clicked`.
- Reference HUD is now `.slui` v2 and contains a C#-animated Progress Bar; Continue clicks are dogfooded through the native runtime queue.
- Expanded MCP developer preview from 40 -> 44 tools with UI delete, reparent, validation and arbitrary-resolution resolved-layout inspection; existing UI tools now understand v2 widgets/properties.
- Scene remains v15, Prefab v6, Project v6 and `.vmeta` v3.

## 0.9.2

- Added backend-neutral runtime UI draw packets and an engine-owned image atlas path for `.slui` documents.
- Added D3D12 native UI rendering for Panel/Text/Image/Button with alpha blending, viewport-aware overlay rendering and no depth dependency.
- Standalone runtime now renders `reference_hud.slui` after the 3D scene; editor Play Mode renders the same document in the Game viewport.
- Added an engine-owned 5x7 fallback bitmap text renderer so native Text/Button content is visible without OS fonts or ImGui; project TTF/OTF rasterization remains future 0.9.x work.
- Advanced managed gameplay ABI v6 -> v7 with appended UI find/exists/text/enabled callbacks and Vespera.NET `UI` / `UiElement` wrappers.
- Reference C# dogfood now changes the runtime HUD title at Start, exercising managed UI mutation in standalone and editor Play.
- `RenderBackend` now has an explicit UI overlay hook and target dimensions; the Null backend remains a no-op implementation.
- Scene remains v15, Prefab v6, Project v6, `.vmeta` v3 and `.slui` v1.
- MCP remains a 40-tool developer preview; its existing UI-document authoring tools now produce content that is visibly consumed by runtime Play rather than serialization-only dogfood.

## 0.9.1

- Removed the redundant hover tooltip from the selectable Console while preserving arbitrary text selection + Ctrl+C and bulk-copy helpers.
- Advanced Project format to v6 with company name, product version, package name, and managed deployment mode; Project v1-v5 remain load-compatible.
- Added framework-dependent vs portable managed export selection. Portable packages can bundle a compatible private `host/fxr` + `Microsoft.NETCore.App` runtime under `dotnet/`, and the managed host prefers that local runtime before machine-wide hostfxr discovery.
- Advanced package manifest to v2 with shipping metadata, deployment mode, bundled .NET runtime version, and bundled runtime file count.
- Added real `.slui` v1 UI-document persistence for Canvas/Panel/Text/Image/Button data, anchors/offsets, colors, text, stable font/image references, and button state styling.
- `.slui` writes use guarded temporary replacement with backup/restore reporting so MCP/UI authoring does not truncate an existing document on a failed replacement.
- AssetCatalog now recognizes UI Documents, tracks UI -> image/font stable dependencies, and stable fallback repair updates moved UI asset fallback paths.
- Added `reference_hud.slui` as a build-root dogfood asset; the reference runtime loads and resolves it through the engine-native UI layout model. Visual UI rendering remains future 0.9.x work.
- Expanded MCP developer preview from 30 to 40 tools with scene opening, project setting read/write, asset delete preflight/move/fallback repair, and UI document/node authoring/inspection.
- Current untouched reference project source closure: 35 catalog assets, 78 dependency edges, 7 stable dependency refs, 0 broken, 0 stale, 32 build assets.
- Scene remains v15, Prefab remains v6, `.vmeta` remains v3, managed gameplay ABI remains v6.

## 0.9.0

- Began the shipping/export-focused 0.9.x line.
- Added `project_package.hpp/.cpp` deterministic standalone package API.
- Added native `vespera_packager` CLI and root `export.ps1` workflow.
- Packages copy only the stable-ID transitive build closure plus `.vmeta` sidecars, project file, managed staging, runtime executable, build index and package manifest.
- Added Development/Release export actions to the editor File menu.
- Added MCP tools `vespera_get_build_manifest` and `vespera_export_project` (30 tools total).
- Added source-root launch plumbing so editor/MCP export can locate built runtimes safely.
- Console body is now read-only selectable text with normal click-drag selection + Ctrl+C; bulk copy helpers remain.
- Scene v15 / Prefab v6 / Project v5 / `.vmeta` v3 / managed ABI v6 remain unchanged.

## 0.8.10

- Expanded localhost/MCP developer preview from 12 to 28 tools: entity lifecycle/hierarchy, reflected component read/write, prefabs, assets, Materials, C# build, capability discovery and existing Play/save/history tools.
- Added versioned Editor Extension API v1 registry foundation; Vespera core dogfoods the registry and exposes capability metadata.
- Added engine-native runtime UI foundation (`UiDocument`, Canvas/Panel/Text/Image/Button, anchored layout, reference-resolution scaling, margins/padding, z-order and Button hit testing) with no ImGui/D3D12 dependency.
- Console can copy all text, only visible filtered text, or individual entries/messages through the context menu.
- No Scene/Prefab/Project/.vmeta/managed ABI format bump.

## 0.8.9

- Added the localhost-only Vespera Automation Protocol (VAP v1) endpoint, disabled by default and polled entirely from the editor/main thread.
- Added a Python-standard-library MCP stdio bridge with an initial 12-tool developer-preview surface.
- Added typed automation for editor state/entity inspection, selection, primitive creation, world/local transform editing, validation, undo/redo, Play Mode control and guarded scene saving.
- Added `run.ps1 -Automation`, configurable automation port support, Tools > Automation / MCP controls and `tools/run-mcp.ps1`.
- Expanded typed editor command identities with selection, semantic transform-set, save, undo and redo operations.
- Documented the 0.9.x automation/MCP dogfood safety model: transport never mutates Scene/renderer/audio/managed state from background threads and never exposes raw engine pointers/handles.
- Scene remains v15, Prefab v6, Project v5, `.vmeta` v3 and managed gameplay ABI v6.

## 0.8.8

- Added first-class renderer-independent `.slmat` Material assets with stable `.vmeta` identity and ID-first/path-fallback base-texture references.
- Added built-in `Vespera/Lit` and `Vespera/Unlit` material modes, Base Color, Emission Color/Strength and Alpha Cutoff. The first Lit pass uses Vespera's existing point-light response; it is intentionally not presented as a full PBR material model yet.
- AssetCatalog recognizes Material assets, tracks stable Material -> Texture and Scene/Prefab -> Material dependencies, and stable fallback repair can update moved Material/texture fallback paths.
- Mesh Renderer now carries a stable Material asset reference plus transient resolved render data. Legacy direct Texture + Instance Tint remain load-compatible fallback data.
- Scene Text advances to v15 with `mesh_material_asset`; Scene v1-v14 remain load-compatible. Prefab Text advances to v6 with the same stable Material record; Prefab v1-v5 remain load-compatible.
- Project browser adds Material cards/filtering. Assets -> Create Material Asset creates a `.slmat`, and the Asset Inspector edits shader, base texture, color, emission and cutoff.
- Material assets can be dragged onto a Mesh Renderer field or directly onto primitive geometry under the Scene cursor.
- D3D12 draw constants now carry renderer-independent Material parameters while the 0.8.6 32-light constant-buffer path remains intact. Unlit ignores scene point lights; emission adds authored self-light color.
- `Vespera.NET` MeshRenderer gains `Material`, `SetMaterial()` and `ClearMaterial()` through the existing semantic property bridge, so managed ABI remains v6.
- Added Concrete Lit and Neon Unlit sample Materials; the reference build closure is now 30 source assets with 0 missing/broken/stale roots.
- Project remains v5 and `.vmeta` remains v3.

## 0.8.7

- Turned the isolated editor Play copy into an interactive runtime host: click the Game viewport to capture input, use WASD + mouse to move/look through the sector world, Shift to sprint, and Escape to release capture without stopping Play.
- Added editor-owned `PlayRuntime` services for the play scene: shared `InputSystem`, SDL3 `AudioSystem`, managed C# host, trigger tracking, player proxy, sector/collider movement, audio-listener updates, and safe managed scene-load requests.
- Managed `Start`/`Update`, trigger Enter/Stay/Exit, runtime spawning/destruction, managed audio calls and scene requests now execute against the isolated editor play scene when the project C# assembly is available.
- Pause/Resume now pauses/resumes editor-owned audio; Step advances one fixed 1/60-second gameplay tick while remaining paused. Stop still tears down runtime services and restores the original edit scene plus undo/redo state.
- Added semantic host-feed methods to `InputSystem` so editor Play and future alternate hosts can drive the same action map without exposing internal key/gamepad arrays.
- Project format advances to v5 with validated `input_bind` records for key, gamepad-button and gamepad-axis bindings. Project v1-v4 remain load-compatible.
- The reference project now authors its 11 actions / 16 bindings in Project v5, and the standalone reference game rebuilds its action map through `configure_project_input_map()` instead of maintaining a second hardcoded binding list.
- Project Settings exposes editable Input Action binding records. Editor Play currently feeds keyboard + relative mouse directly; authored controller bindings already drive standalone runtime and are ready for editor controller feed later.
- Hardened the C# Build helper for live editor Play: the process-lifetime Vespera.NET bridge is never overwritten underneath a running editor, collectible game dependencies are copied atomically, and the game DLL is committed last for safe automatic reload.
- Scene remains v14, Prefab remains v5, `.vmeta` remains v3, managed gameplay ABI remains v6.

## 0.8.6

- Added first in-editor Scene/Game Play Mode foundation: Play/Stop/Pause/Resume/Step, isolated play-scene copy, dedicated Game tab, game-camera preview and non-destructive Stop behavior. The edit-scene/history state is backed up on Play and restored on Stop, and scene saving is blocked while playing.
- Added Ctrl+P Play/Stop shortcut and toolbar play-state/time feedback.
- Scene and Game previews can both render through the existing live D3D12 editor backend; Game uses the authored scene camera and isolated play clock while Play Mode is active.
- Project assets are now drag sources. Prefabs can be dropped into the Scene viewport to instantiate in front of the editor camera, and compatible Texture assets can be dropped onto Sprite Renderer / Mesh Renderer texture fields.
- Prefab instances gained faster Hierarchy context actions plus Inspector Select Source, Apply, Revert and Unpack workflows.
- Expanded the typed Editor Command record with before/after state IDs and optional entity/asset targets, plus play-mode, prefab-apply and asset-drop command names as groundwork for later transactional MCP/extension dispatch.
- Reworked D3D12 point-light upload into a dedicated per-frame GPU constant buffer. The renderer now evaluates up to 32 active point lights per view instead of dropping everything beyond the two nearest lights. Per-view 256-byte-aligned upload slots keep Scene/Game lighting immutable until the GPU consumes the frame.
- The reference-game hall/chamber/runtime-marker lights can therefore coexist without camera-sector light swapping caused by the old two-light cap.
- Scene remains v14, Prefab remains v5, Project remains v4, `.vmeta` remains v3, managed gameplay ABI remains v6.

## 0.8.5

- Added real parent/child entity hierarchy semantics. Root transforms remain world-space; child transforms serialize local-to-parent.
- Scene Text advances to v14 with optional `parent <entity-id>` records; Scene v1-v13 remain load-compatible.
- Added renderer-independent hierarchy composition/inversion helpers and cycle-safe reparenting with world-transform preservation.
- Hierarchy world evaluation composes root-to-leaf, and group gizmo capture/cancel/selection overlays stay world-space correct for nested parent selections.
- D3D12 mesh/sprite rendering, point lights, collision, 2.5D raycasts and sprite facing now resolve entity world transforms through the hierarchy.
- Hierarchy drag/drop now parents/reparents entities; dropping on the Entities group unparents, while Ctrl-drop retains author-order reordering.
- Inspector shows Parent, Local Transform and derived World Transform for child entities; prefab Revert preserves an instance's hierarchy parent.
- Multi-selection gizmos operate in world space for parented entities, and group duplication remaps duplicated parent links when parent+child are copied together.
- Deleting a parent recursively deletes descendants, matching the expected scene-tree model.
- Project grid now renders actual CPU-decoded texture thumbnails instead of only `TEX` type cards.
- Bundled the approved Vespera standalone mark/full lockup and use the standalone mark as the SDL editor window icon.
- Prefab remains v5, Project remains v4, `.vmeta` remains v3, managed gameplay ABI remains v6.

## 0.8.4b

- Fixed the Windows/MSVC editor build regression in 0.8.4 caused by duplicate `vec3_dot` and `vec3_cross` helper definitions in `editor/main.cpp`.
- Renamed one local Inspector edit flag to remove an unrelated MSVC shadowing warning.
- Added source validation that rejects duplicate gizmo vector-helper definitions before packaging.
- No runtime, renderer, serialization, asset-format, or managed-ABI changes from 0.8.4.

## 0.8.4

- Multi-selection Move/Rotate/Scale now transforms the full selected entity set from one Scene gizmo.
- Added Center/Pivot gizmo placement for multi-selection; Local/Global orientation and existing snap/Alt-bypass behavior remain available.
- Group rotation orbits selected entities around the active gizmo pivot; group scaling expands/contracts positions around the pivot and scales the selected entities together.
- Escape restores every selected transform from the start of the gesture, and each group gesture remains one undoable edit.
- Frame Selected now focuses the center of a multi-selection.
- Hierarchy entities can be drag/dropped to reorder them; the operation is undoable and recorded as `entity.reorder`.
- No Scene/Prefab/Project/.vmeta/managed ABI format bumps.

## 0.8.3

- Added entity multi-selection foundation: Ctrl-click toggles, Shift-click ranges, Scene Ctrl-click selection, group duplicate/delete, undo/redo selection restoration, and secondary Scene markers.
- Project browser now defaults to a Unity-familiar grid with adjustable tile size and keeps a detailed list fallback.
- Asset grid preserves double-click scene/prefab actions and asset context-menu authoring actions.
- No Scene/Prefab/Project/.vmeta/managed ABI format bumps.

## 0.8.2

- Added built-in Mesh Renderer with Cube/Plane/Cylinder/Sphere primitives.
- Added Unity-familiar GameObject/Hierarchy 3D Object creation menus.
- Primitive entities use normal Transform Move/Rotate/Scale and can be textured/tinted.
- D3D12 now renders reusable built-in primitive geometry.
- Scene format v13 persists Mesh Renderer; v1-v12 remain compatible.
- Prefab format v5 persists Mesh Renderer; v1-v4 remain compatible.
- Managed semantic component bridge exposes `sectorline.mesh_renderer` without an ABI bump.

## 0.8.1

- Added Q/W/E/R View/Move/Rotate/Scale tools to the 3D Scene viewport.
- Added projected entity picking, selected entity overlays, Global/Local gizmo orientation and transform snapping.
- Added undo-aware transform gestures and typed transform command audit entries.
- Runtime/serialization/managed ABI remain unchanged.

## 0.8.0

- Starts the production-editor phase after the 0.7.x asset/project foundation.
- Adds a Unity-familiar default docking layout: Hierarchy left, Scene/Sector center, Inspector right, Project/Console tabbed at bottom.
- Renames 3D View -> Scene and top-down Scene View -> Sector for clearer workflow semantics.
- Adds View -> Reset Editor Layout.
- Makes the Project browser folder-first with Assets-root navigation, clickable breadcrumbs, search and asset-kind filtering.
- Adds Console Info/Warnings/Errors visibility filters.
- Adds `editor/editor_command.hpp` with stable typed command names and an in-editor audit trail for core scene/entity/prefab/sprite-clip commands.
- Keeps runtime/serialization/managed ABI unchanged from 0.7.9.

## 0.7.9

- Closed out the 0.7.x asset/project foundation with .vmeta v3 texture import intent, font-source validation, runtime asset invalidation groundwork, safe-delete preflight, and deterministic build asset index output.
- Recorded the Unity-familiar production-editor workflow direction for 0.8.x while retaining compact Dear ImGui and Vespera-specific branding.

## 0.7.8

- Scene Text advances to v12 with `prefab_source_asset "<id>" "<fallback>"`; v1-v11 remain load-compatible.
- Entity prefab-instance source metadata now uses the common native `AssetReference` shape instead of a path-only string.
- Editor prefab Create/Instantiate/Apply/Revert resolves stable IDs through the project `AssetCatalog`, and legacy path-only scene links are hydrated in memory when possible.
- AssetCatalog dependency scanning treats Scene v12 prefab links as stable edges, so prefab moves no longer need to be blocked merely because a scene references them.
- Stable fallback repair now rewrites stale Scene v12 prefab fallback paths in addition to Project/Sprite Sheet/Audio Clip references.
- Asset move and authored-text rewrite rollback failures now report secondary rollback/restore failures instead of claiming recovery unconditionally.
- Reference project migrates both scenes to stable prefab links and explicitly includes the dynamically loaded alternate scene in the build manifest.

## 0.7.7

- Added safe project asset Move / Rename operations that move source + `.vmeta` together and preserve stable identity.
- Added fallback-path repair for Project v4 roots, Sprite Sheet v2 sources, and Audio Clip v2 sources.
- Added editor Move / Rename workflow and Project Settings fallback-repair action.
- Added C# `AssetReference` / `Assets` API and stable-reference scene/prefab/audio overloads.
- Managed native service table advances to ABI v6 to expose the live project AssetCatalog.
- Reference-game managed dogfood now resolves scene, prefab, and audio assets by stable ID.

## 0.7.6

- Added ID-first/path-fallback `AssetReference` resolution backed by stable `.vmeta` IDs.
- Added Project v4 `startup_scene_asset` and `build_include_asset` roots with v1-v3 load compatibility.
- Added Sprite Sheet v2 and Audio Clip v2 stable `source_asset` references with v1 path-only compatibility.
- Build manifest now resolves roots by stable ID first and reports stale fallback paths separately from missing/broken references.
- Asset catalog refresh now distinguishes added, content-changed, moved/renamed, and removed assets by stable ID.
- Dependency diagnostics now report stable-reference edges and stale fallback paths without treating a valid ID-resolved edge as broken.
- Editor Project Settings/Inspector records and displays stable roots/references while retaining the compact Dear ImGui shell.
- Added move-resilience validation that relocates startup/build/descriptor/source assets with their `.vmeta` sidecars and verifies zero broken references.

## 0.7.5

- Added `.slspritesheet` regular-grid slicing into reusable directional sprite clips.
- Added `.slaudio` authored playback descriptors over raw WAV assets.
- Added Project v3 `build_include` records with v1/v2 load compatibility.
- Added deterministic transitive standalone build-asset manifest generation from startup scene + explicit roots.
- Added Sprite Sheet / Audio Clip catalog kinds, dependency edges, Inspector support, Project browser filters, and build-inclusion controls.
- Reference project dogfoods a 128x512 Watcher sheet and authored chime descriptor.
- Compact Dear ImGui editor direction remains unchanged.

## 0.7.4

- Returned editor presentation to compact ImGui direction; no dashboard-style shell work.
- Added first asset dependency/reference graph with broken-link diagnostics and reverse dependents.
- Added first-class `.slspriteclip` v1 assets and runtime/editor dogfood.
- Added folder-aware Project asset browsing.
- Added Windows WIC PNG/JPEG decode path into renderer-independent RGBA8 TextureData.
- Added PNG decoder dogfood asset.
- Kept scene v11, prefab v4, project v2, `.vmeta` v2, managed metadata v4, and gameplay ABI v5 stable.

## 0.7.1

- redesigned the default editor docking layout around a wider Project / Assets workspace
- split Project / Assets into Assets, Scene Resources, C# Scripts, and Project Settings tabs
- added project-asset selection to the normal Inspector with stable ID/importer/fingerprint details
- added searchable/type-filterable asset table with context actions
- added `.vesperaproject` v2 runtime window settings with v1 load compatibility
- reference game now consumes project window settings before SDL window creation
- added `.vmeta` v2 source-mtime cache hints with v1 migration
- added automatic lightweight project source polling and forced-hash manual reimport
- added orphan/repair/hash/fast-path asset refresh diagnostics
- added dependency-free 24/32-bit type-2/type-10 TGA texture importing
- added a small authored TGA dogfood asset to the reference project
- kept scene v11, prefab v4, managed metadata v4, and gameplay ABI v5 stable

## 0.7.0

- Added Vespera Project format v1 (`.vesperaproject`) with project name, assets root, startup scene, managed project/assembly, native target, shared load/save APIs, and native project validation.
- Editor now launches project-first, supports **File > Open Project**, project settings/save/validation, and setting the current scene as startup.
- Upgraded `AssetCatalog` from scene/prefab discovery into project asset discovery for scenes, prefabs, textures, audio and fonts.
- Added `.vmeta` format v1 with stable asset ids, importer identity, source size, and stable content hashes; refresh/reimport preserves ids.
- Added `AssetCatalog::find_by_id()` for stable-id lookup.
- Added the first real texture importer: dependency-free uncompressed 24/32-bit BMP -> renderer-independent RGBA8 `TextureData`.
- Replaced all 22 procedural reference textures with authored BMP files under `assets/textures`; editor and runtime now import the same source files.
- Expanded Project / Assets with path filtering, texture/audio/font groups, importer state and stable-id inspection.
- Made the managed build helper accept a project-provided assembly name instead of hardcoding `ReferenceGame.Scripts.dll`; build/editor/runtime reference flow reads project workspace values.
- Kept Scene Text v11, Entity Prefab v4, managed metadata v4 and gameplay ABI v5 unchanged.

## 0.6.3

- Defers managed `Entity.Destroy()` until the end of the managed update frame, runs script cleanup while the native Entity still exists, then commits native removal safely.
- Adds C# collection queries: `Scene.All`, `FindAllWithTag`, `FindAllOnLayer`, and `FindAllWithComponent` through a stable native count/fill query.
- Adds typed C# `CylinderCollider` and `PointLight` wrappers plus runtime `EnsureCylinderCollider()` / `EnsurePointLight()` helpers over the existing semantic component-property API.
- Adds managed `OnTriggerStay(Entity)` driven by the native `TriggerTracker` active-overlap set.
- Adds automatic Entity following for spatial `AudioSource`, managed-source lifetime tracking, cleanup on assembly release, `Audio.StopAll()`, and `Audio.ActiveVoiceCount`.
- Adds `SaveData` Vector2/Vector3 helpers and key enumeration, isolates in-memory state when switching/missing save slots, and adds `GameTimer.Remaining`, `Progress`, and `Restart()`.
- Adds reference-game dogfood for collection queries, typed collider/light access, deferred destroy lifecycle, trigger-stay, auto-follow audio, and save/timer helpers.
- Advances the managed native service table to ABI v5 while keeping Scene Text v11, Entity Prefab v4, and managed reflection metadata v4 unchanged.

## 0.6.2

- Expands SDL3 audio into handle-based persistent voices with stop, pause/resume, looping, per-source volume, source position, and simple listener-distance attenuation.
- Adds `AudioSource`, `Audio.Play`, `Audio.PlaySpatial`, and `AudioListener` to Vespera.NET while retaining one-shot playback.
- Adds managed `Physics.OverlapCircle2D` and `Physics.ResolveCircleMotion2D`, backed by the existing deterministic native Cylinder Collider helpers.
- Adds C# `OnEnable()` / `OnDisable()` lifecycle transitions for Entity enabled-state changes and runtime/reload cleanup.
- Adds a typed C# `SpriteRenderer` wrapper for clip/speed/pause/restart/tint/size control over the existing semantic property API.
- Keeps `Time.ElapsedTime` on a process-runtime clock across scene switches/reloads so animation restart offsets have a stable epoch.
- Adds `Key::L` and reference-game dogfood for lifecycle toggling and looping spatial audio.
- Advances the managed service table to ABI v4; Scene Text remains v11, Entity Prefab remains v4, and managed reflection metadata remains v4.

## 0.6.1

- Adds engine-owned SDL3 `AudioSystem` playback service with WAV one-shots, master volume, voice cleanup, native `GameContext` access, and C# `Audio.PlayOneShot` / `Audio.MasterVolume`.
- Adds safe end-of-frame managed scene-load requests through `Scene.Load` / `Scene.Reload`; the reference game consumes requests only after CLR `Update()` returns and rebuilds managed runtime state against the new Scene.
- Adds runtime synchronization of C# script attachments so scripts added to runtime-created/prefab-spawned entities activate without an assembly reload and removed attachments receive `OnDestroy()`.
- Adds `Entity.HasScript<T>`, `AddScript<T>`, and `RemoveScript<T>`.
- Exposes Vespera's existing semantic built-in component property layer to C# for float/bool/string/Vector2/Vector3/Color values instead of exposing native struct layout.
- Reference gameplay dogfood now changes sprite animation/tint through semantic property keys, dynamically attaches `RuntimeSpawnReporter`, plays a test WAV, and switches between two authored scenes through C#.
- Bumps the managed native service table to ABI v3 while keeping Scene Text v11, prefab v4, and managed reflection metadata v4 unchanged.
- Retains the validated 0.5.x automatic reload/last-good pipeline and all 0.6.0 Input/Time/Entity/Prefab/Raycast/Trigger/SaveData APIs.

## 0.6.0

- Begins the core-gameplay phase after the validated 0.5.x C# foundation.
- Bumps the native managed ABI to v2 and passes both Scene and InputSystem through one stable bridge context.
- Adds C# action-map input (`Input.Value/Down/Pressed/Released`).
- Adds managed `Time` and reusable `GameTimer`.
- Adds C# Scene/Entity query, name/tag/layer access, create, clone, destroy, and built-in component-presence helpers.
- Adds runtime C# prefab instantiation through the existing native `.slprefab` loader.
- Adds C# 2.5D gameplay raycasts over the existing native scene raycast implementation.
- Adds `Component.OnTriggerEnter/OnTriggerExit` and native-to-managed trigger dispatch.
- Adds a small JSON-backed `SaveData` key/value foundation for bool/int/float/string values.
- Reference game dogfoods the new APIs through `GameplayApiDogfood` and `TriggerReporter`; the Corridor Watcher also observes the managed `probe` input action.
- Scene v11, prefab v4, and managed reflection metadata v4 remain unchanged.

## 0.5.5

- Declared the first 0.5.x C# scripting foundation feature-complete pending the normal Windows smoke test; broader gameplay APIs move to 0.6.x.
- Hardened live C# replacement into a true two-phase transaction: prepare a collectible candidate, preflight every enabled scene script attachment, then commit only if the replacement is compatible.
- A replacement assembly that loads but removes/invalidates an attached C# component is now rejected before the old runtime receives `OnDestroy()`, preserving the active scripts.
- Added managed candidate prepare/validate/commit/discard dispatcher commands without changing the public native ABI v1.
- Added build-time C# component contract validation in `Vespera.ScriptTool`: exposed fields must use supported writable types, `[Range]` is restricted to numeric fields, serialized aliases must be unambiguous, and concrete components need a parameterless constructor.
- Metadata-contract failures flow through the existing staged managed-build diagnostics, so last-good DLL/metadata output remains untouched.
- Kept Scene Text v11, prefab v4, managed metadata v4, and native managed ABI v1 stable.
- Retained automatic debounced reload, manual Space reload fallback, `[FormerlySerializedAs]`, exact numeric range entry, multi-script ordering, and collectible-context unload verification.

## 0.5.4

- Fixed the Windows PowerShell atomic managed-mirror swap to provide a real temporary backup path to `System.IO.File.Replace`; this avoids the `The path is not of a legal form` failure seen when `$null` was supplied as the backup path.
- Added engine-level automatic managed game-assembly watching with low-frequency polling and a stability debounce; the reference game opts in while keeping Space as a manual reload fallback.
- Hardened live reload so a replacement assembly is loaded into a candidate collectible context before existing script instances are destroyed.
- Changed the editor C# runtime mirror to copy companion files first and atomically commit `ReferenceGame.Scripts.dll` last, preventing the watcher from observing a half-copied build.
- Added repeatable `[FormerlySerializedAs("OldFieldName")]` for semantic exposed-field rename compatibility without changing scene/prefab formats.
- Upgraded managed reflection metadata to v4 with field aliases; the Inspector recognizes legacy keys and offers an undoable **Migrate Name** action.
- Kept scene v11 / prefab v4 and the native ABI v1 stable.

## 0.5.3

- Added an explicit in-place C# game-script reload path that keeps CoreCLR and `Vespera.NET` alive while replacing only the collectible game-code `AssemblyLoadContext`.
- Reference game can reload C# with **Space** after a successful editor **Build C#**, without restarting the game.
- Managed build staging now refreshes reloadable runtime game-code/dependency files while deliberately leaving process-lifetime `Vespera.NET.dll` untouched.
- Added `Component.OnDestroy()` and invoke it before managed reload/shutdown; script exceptions during cleanup remain contained and logged with script/entity context.
- Added collectible-context unload verification with diagnostics for leaked static events/tasks/GCHandles that can prevent unloading.
- Added first-class `Entity.Transform` while retaining direct Entity Position/Rotation/Scale convenience properties.
- Expanded `[Expose]` support to enums, `Vector2`, and `Color` in addition to bool/int/float/string/`Vector3`.
- Upgraded managed reflection metadata to v3 with enum-value metadata.
- Ranged numeric Inspector fields now provide both a slider and explicit precise numeric entry.
- Added C# attachment Move Up / Move Down / Duplicate controls; attachment order remains deterministic lifecycle order.
- Reference `ManagedSpinner` dogfoods `Entity.Transform` plus an exposed enum `SpinDirection`.
- Scene format remains v11 and prefab format remains v4; no authored-data migration was required.

## 0.5.2

- Added **Build C#** to Project / Assets when the editor is launched through `run.ps1`.
- Added structured C# compiler diagnostics in the Vespera Console with error/warning code, file, line, and column.
- Added staged managed builds with **last-good output protection**: failed C# compilation or metadata generation leaves the previous working assembly/metadata untouched.
- Unified command-line and editor managed builds around `tools/build-managed-editor.ps1` while keeping the normal `build.ps1` / `run.ps1` / `run-game.ps1` workflow.
- Upgraded managed reflection metadata to v2 with `[Range]` and `[Tooltip]` hints; numeric exposed fields can use ranged Inspector controls.
- Improved C# script Inspector UX with discovered component-type selection, editable unresolved class names, reset-all-overrides, and preserved unresolved fields.
- Game scripts now load from memory into a **collectible `AssemblyLoadContext`**, avoiding a permanent game-DLL file lock and establishing the correct lifetime boundary for later reload.
- Added a native `ManagedScriptHost::reload()` primitive for future runtime/editor reload orchestration.
- Managed lifecycle exceptions now include script type, entity id, phase, exception type, and stack trace. A script that faults in `Start`/`Update` is quarantined instead of flooding the log every frame.
- Scene v11 and prefab v4 remain unchanged; this checkpoint does not churn serialized scene data.

## 0.5.1

- Reworked managed scripts from a single built-in optional component into a per-Entity script attachment list, allowing multiple C# components on one Entity.
- Scene format v11 and prefab format v4 preserve multiple scripts plus serialized exposed-field overrides while remaining backward compatible with scene v10 / prefab v3.
- Added `[Expose]` to `Vespera.NET`; serialized overrides are applied through managed reflection before `Start()`.
- Added `Vespera.ScriptTool`, build-time managed reflection metadata, editor script discovery, C# metadata refresh, and exposed-field override authoring.
- Added native ABI structure sizing and stable managed script instance handles for field application.
- Updated the reference `ManagedSpinner` to expose its spin speed and dogfood serialized C# field overrides.

## 0.5.0

- Began the public branding transition from the Sectorline codename to **Vespera Engine** while intentionally preserving current internal `sectorline` component/file identifiers for 0.x compatibility.
- Added Windows-first native .NET hosting through `hostfxr` and a small versioned C ABI; the C++20 core remains authoritative.
- Added the first `Vespera.NET` managed API with `Component`, `Start`, `Update`, `Entity`, Transform access, Entity enabled access, and managed logging.
- Fixed the initial C# dogfood build failure by making managed `Entity` a lightweight reference handle, allowing natural writes such as `Entity.Rotation = value` while still forwarding directly to the native Transform.
- Added built-in `sectorline.managed_script` scene component and editor Inspector/Add Component authoring.
- Upgraded Scene Text writer to v10 with C# Script persistence while retaining v1-v9 loading.
- Upgraded Entity prefab writer to v3 with C# Script persistence while retaining prefab v1-v2 loading.
- Added `build.ps1` integration that builds `Vespera.NET` and reference C# game scripts when a .NET 8+ SDK is available while preserving a usable native-only build otherwise.
- Added `run-game.ps1` synchronization of managed assemblies/runtime config alongside normal project assets.
- Added `ReferenceGame.Scripts.ManagedSpinner`; `Corridor Watcher` now dogfoods C# `Start`/`Update` by changing its real native Transform and therefore its directional-sprite facing.
- Added `docs/MANAGED_SCRIPTING.md` describing the host, ABI, lifecycle, initial API, and intentionally deferred managed features.

## 0.4.16

- Added built-in `sectorline.point_light` Entity component with color, intensity, and radius metadata/property access.
- Added D3D12 ambient + two-nearest-point-light radial lighting for sector geometry and sprites.
- Added editor Point Light creation, Inspector editing, Hierarchy labels, and top-down radius visualization.
- Upgraded Scene Text writer to v9 while retaining v1-v8 loading.
- Upgraded Entity prefab writer to v2 with Point Light persistence while retaining prefab v1 loading.
- Added two authored reference lights so lighting is dogfooded in the runtime and 3D editor preview.

## 0.4.13

- Fixed the editor 3D View using flat placeholder colors while the reference game used the real procedural reference textures.
- Moved the reference-project texture payload generators into one shared `reference_textures.hpp` helper consumed by both the editor and the reference game.
- The editor preview now receives the same 64x64 floor, brick, panel, ceiling, chamber, sprite, and directional watcher textures as runtime instead of 1x1 placeholders.
- Retained the texture-backed ImGui preview from 0.4.12, the 0.4.11 descriptor-heap/overlay runtime fix, and scene format v8.

## 0.4.11

- Fixed the Windows editor runtime crash caused by recording Dear ImGui D3D12 draws while Sectorline's world-texture descriptor heap was still bound.
- Added a narrow D3D12 overlay-preparation bridge that rebinds the active back buffer without the scene DSV and switches to the tool/UI shader-visible descriptor heap before overlay rendering.
- Added an explicit command-queue validity check for the ImGui DX12 initialization path.
- Added `sectorline_editor_startup.log` startup/frame milestones to make any remaining Windows-only editor crash diagnosable without a debugger.
- Kept scene format v8 and all 0.4.9/0.4.10 gameplay/editor content unchanged.

## 0.4.10

- Fixed Windows/MSVC build failure caused by the Win32 `min`/`max` macros leaking through the new D3D12 editor bridge.
- `NOMINMAX`/`WIN32_LEAN_AND_MEAN` are now established before D3D12 headers and propagated to Windows consumers.
- Public scene/world headers now use macro-safe standard-library min/max forms as an extra compatibility guard.
- No scene-format or gameplay changes from 0.4.9.

## 0.4.9

- Added the first live docked **3D View** to Sectorline Editor while retaining the top-down 2.5D Scene View.
- Added an independent editor preview camera with RMB mouse-look, WASD/QE fly navigation, speed control, Scene Camera reset, and frame-selection support.
- Refactored `RenderBackend` into a staged `begin_frame` / `render_scene` / `end_frame` path while retaining the normal runtime `render()` convenience call.
- Added renderer-independent pixel `RenderViewport` support so tools can render a Scene into a sub-region of the active target.
- Refactored the D3D12 backend to support staged rendering, camera overrides, sub-region viewport/scissor/depth clears, and external tooling commands before present.
- Added a narrow D3D12 native tooling-access interface for editor backend integration without exposing D3D12 through Scene/game APIs.
- Moved the Windows editor from Dear ImGui's SDLRenderer backend to its D3D12 backend and added a small shader-visible tooling descriptor allocator.
- Added high-DPI logical-to-framebuffer conversion for the docked 3D viewport rectangle.
- Opening a new scene resets the independent 3D editor camera cleanly.
- Kept scene format v8, prefab/resource behavior, gameplay APIs, and reference-game content unchanged.
- This is the first renderer/editor viewport foundation; a general offscreen texture/render-target abstraction, 3D picking, and transform gizmos remain later work.

## 0.4.5

- Added external single-Entity `.slprefab` assets with public load/save/instantiate APIs.
- Prefab instantiation always allocates a fresh stable Entity id and can retain a project-relative source asset path.
- Upgraded `.slscene` writer to v8 with optional `prefab_source` Entity metadata while retaining v1-v7 loading.
- Added first engine-level `AssetCatalog` filesystem discovery layer for native `.slscene` and `.slprefab` assets.
- Project / Assets now lists cataloged scenes and prefabs and can refresh the catalog explicitly.
- Added editor **Create Prefab from Selected**, double-click/context prefab instantiation, and `[Prefab]` Hierarchy annotations.
- Added Entity Inspector prefab **Apply**, **Revert**, and **Unpack** workflow.
- Prefab writes use temporary-save + public-loader verification + one-file backup before replacing an existing prefab.
- Added prefab-source validation so scene links cannot use absolute paths or escape the assets root through `..`.
- Added a reusable Watcher prefab to the reference project and dogfooded runtime prefab loading/spawning plus AssetCatalog discovery.
- Kept the current D3D12 renderer, sector/sprite/collider gameplay foundation, and editor authoring workflows unchanged.

## 0.4.0

- Added Entity `tag` and `layer` identity metadata with v7 scene serialization and v1-v6 migration defaults.
- Added Scene queries by tag, layer, built-in component type/key, plus public Entity cloning with fresh stable ids.
- Added SectorWorld point/name query helpers.
- Added trigger overlap ids plus deterministic `TriggerTracker` enter/exit events.
- Added 2.5D scene raycast against sector walls and Cylinder Colliders, including portal-height handling, trigger inclusion, ignored entity, tag/layer filters, and nearest-hit data.
- Added built-in component property metadata and generic key-based property read/write access for future tooling/bindings.
- Added Entity Tag/Layer Inspector authoring, Hierarchy filtering, identity labels, and one-click Trigger Volume creation.
- Reference scene now contains a tagged invisible Chamber Threshold trigger; reference game logs trigger transitions and exposes an Enter-key probe ray.
- Centralized runtime/editor version reporting through a CMake-generated `sectorline/core/version.hpp`.
- Kept D3D12 rendering, sprite animation, sector authoring, material/clip authoring, undo/redo, scene validation, and automatic editor-to-game asset syncing intact.

## 0.3.3

- Added stable built-in component keys intended to remain common across editor tooling, C#, Lua, docs, MCP, and future visual scripting.
- Added generic built-in `Entity::has_component`, `add_component`, and `remove_component` operations.
- Added `CylinderColliderComponent` with radius, height, local center, and trigger state.
- Added public 2.5D scene-collider overlap and basic sliding motion helpers.
- Integrated solid Cylinder Colliders into the reference first-person controller.
- Added generic **Add Component...** authoring in the Entity Inspector.
- Added Cylinder Collider Inspector controls, Hierarchy annotations, and top-down collider/trigger rings.
- Upgraded `.slscene` writer to v6 with `cylinder_collider` records while retaining v1-v5 load compatibility.
- Added Cylinder Collider validation.
- Added `Scene::find_entity_by_name()` as a small game/script-friendly query API.
- Fixed the editor-to-runtime test loop: `run-game.ps1` now synchronizes source assets before launching, so saved editor changes appear without manual copying or rebuilding.
- Added a solid collider to the reference `Chamber Watcher` for direct runtime testing.

## 0.3.0

- Added the first general `Entity` scene model with stable id, name, and enabled state.
- Added required `TransformComponent` with position, XYZ rotation, and XYZ scale.
- Added optional `SpriteRendererComponent` and moved sprite presentation/animation state out of standalone scene objects.
- Added initial built-in component metadata plus public entity create/destroy/find/id-allocation APIs.
- Refactored sprite animation resolution to consume Entity Transform + Sprite Renderer data; directional facing now uses Transform Y rotation.
- Refactored the D3D12 sprite path to render enabled entities that contain Sprite Renderer.
- Upgraded `.slscene` writer to v5 entity/component blocks.
- Kept v1-v4 scene loading compatible; legacy v2-v4 sprite records migrate to Entity + Transform + Sprite Renderer automatically.
- Expanded scene validation for entity ids, Transform scale, Sprite Renderer values, textures, and clip references.
- Reworked editor Hierarchy/selection/Scene View around Entities.
- Added Empty Entity and Sprite Entity creation.
- Added Transform Inspector editing and add/remove Sprite Renderer authoring.
- Added a transform-only `Gameplay Marker` to the reference scene to verify that Entities are not synonymous with rendered sprites.
- Preserved existing sector, portal, material, sprite-clip, undo/redo, and verified-save workflows.

## 0.2.10

- Added sector create/duplicate/delete authoring through the normal undoable editor command path.
- Added safe portal-index repair when sectors are deleted and stripped portal links from duplicated sectors.
- Added direct sector-side selection/highlighting in the top-down Scene View.
- Added per-side material and portal-target editing in the Sector Inspector.
- Added automatic reciprocal linking/repair when a selected portal edge has a matching shared edge.
- Added public `find_matching_sector_side()` world geometry helper.
- Added validation warnings for unmatched/non-reciprocal portal links and errors for self-portals.
- Added material create/duplicate/delete/rename workflows in Project / Assets.
- Added Material Inspector editing for tint, texture, and UV scale.
- Added `SectorWorld::set_material()`, `erase_material()`, and `erase_sector()` with reference/index repair.
- Kept `.slscene` format at v4 because existing serialization already represents the authored data.

## 0.2.8

- Upgraded `.slscene` writer to v4 with stable sprite actor ids and enabled state.
- Kept v1-v3 scene loading compatible with automatic id migration.
- Added fresh-id allocation for created/duplicated sprite actors and save-time duplicate-id validation.
- Disabled sprite actors are skipped at runtime and shown dimmed in the top-down editor.
- Added full sprite clip resource authoring: create/duplicate/delete/rename, 1/4/8 directions, 1-16 frames, FPS, looping, and texture assignment.
- Clip rename updates actor references; clip deletion clears dangling actor references.
- Expanded editor selection/undo integration for sprite clip assets.


## 0.2.5

- Added named sprite animation clips as scene resources.
- Added 1/4/8-direction camera-relative sprite frame selection.
- Added multi-frame animation timing with loop/one-shot modes.
- Added per-actor facing yaw, animation speed, time offset, and pause state.
- Added renderer-independent `resolve_sprite_frame()` so future backends/tools share animation rules.
- Upgraded `.slscene` output to format v3 while retaining v1/v2 load compatibility.
- Added editor clip assignment and animation controls to the Sprite Inspector.
- Added facing-direction arrows and clip labels in the top-down Scene View.
- Added Sprite Clips metadata to Project / Assets.
- Expanded the reference world with an 8-direction × 2-frame procedural watcher clip.
- Bundled these related sprite/editor changes into one validation checkpoint instead of shipping several tiny builds.

## 0.2.3

- Added creation of new sprite actors from the Hierarchy and GameObject menu.
- Added sprite actor duplication (`Ctrl+D`) and deletion (`Delete`).
- Added Hierarchy context actions and Inspector duplicate/delete controls for sprite actors.
- Added unique default naming and scene-aware placement for newly authored actors.
- Routed create/duplicate/delete through undoable editor history operations.
- Added Frame Selected (`Shift+F`) and hierarchy double-click focusing.
- Added selected sprite labels in the top-down Scene View.

## 0.2.2

- Added direct sector-vertex dragging in the top-down Scene View.
- Added welded coincident-vertex propagation so shared portal corners stay connected during geometry edits.
- Added convex/counter-clockwise validation for interactive sector edits.
- Added direct sprite-actor and camera X/Z dragging.
- Added configurable grid snapping with Alt-to-bypass behavior.
- Added selected vertex handles and vertex selection in the Inspector.
- Added editor undo/redo history and edit transactions for Scene View drags and Inspector properties.
- Dirty-state tracking now follows history back to the last saved scene state.
- Added unsaved-change prompts before scene replacement or editor exit.
- Added verified temporary save + one-file `.bak` rollback behavior.

## 0.2.1

- Added the first `SectorlineEditor.exe`.
- Added a docked Dear ImGui tooling workspace with Hierarchy, Scene View, Inspector, Project / Assets, and Console panels.
- Added an interactive top-down X/Z Scene View with pan, zoom, frame-all, sector fills, portal edge highlighting, camera marker, sprite markers, and click selection.
- Added basic camera, sector, material-reference, and sprite-actor property editing through public scene/world APIs.
- Added editor `.slscene` open, save, save-as, and dirty-state handling.
- Added `SectorWorld::set_sector()` so editor sector changes update world revision without exposing renderer internals.
- Added `run-editor.ps1` and `run-game.ps1`; `run.ps1` now launches the editor against the source reference scene.
- Pinned Dear ImGui docking tooling dependency to commit `b48d1af` (`v1.92.9b-docking`).

## 0.2.0

- Added first-class serialized `SpriteActor` scene objects.
- Added upright camera-facing billboard rendering on the D3D12 backend.
- Added alpha-cutout sprite sampling and shared depth testing with sector geometry.
- Added per-sprite world size, bottom-center position, texture reference, and color tint.
- Upgraded `.slscene` output to format v2 with sprite records while retaining v1 load support.
- Added sprite load/save round-trip coverage and two reference sprite actors.
- Added a procedural 64x64 transparent sprite texture to validate the path before image importing exists.

## 0.1.4

- Added public `InputSystem` + named `InputMap` action foundation.
- Added keyboard, gamepad button, and gamepad axis action bindings with scaling.
- Added per-axis deadzones with smooth remapping outside the deadzone.
- Added physical and action pressed/released queries.
- Added SDL3 gamepad discovery, automatic open, hot-plug, and removal handling.
- Added normalized left/right stick and trigger state.
- Reference game now consumes named actions for movement/sprint and controller look.

## 0.1.3

- Added versioned `.slscene` text serialization (`sectorline_scene 1`).
- Added camera, material, sector, vertex, side-material, and portal-adjacency load/save support.
- Scene texture references resolve by texture name and remain independent of renderer IDs.
- Added line-numbered scene validation errors and transactional load behavior.
- Moved the connected reference world out of C++ into `assets/scenes/connected_sectors.slscene`.
- Reference game now exercises both load and save paths each launch.
- CMake copies reference-game assets beside the executable.

## 0.1.2

- Added renderer-independent RGBA8 `TextureData`.
- Added texture references and UV scale to world materials.
- Added UV/layer data to sector meshes.
- Added D3D12 texture-array upload and sampled texture path.
- Added five procedural reference textures.

## 0.1.1

- Added connected sectors with portal openings.
- Added differing floor/ceiling heights and cross-sector traversal.
- Fixed finite-segment wall collision around split portal boundaries.

## 0.1.0

- Added engine-owned Scene/Camera and sector-based 2.5D world foundation.
- Added first-person movement, mouse look, wall collision, and generated floor/wall/ceiling geometry.
