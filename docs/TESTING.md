# Vespera behavioral tests

Vespera has three complementary validation layers:

1. `tools/validate_source.py` checks source, format, and API contracts without launching the engine.
2. `test.ps1` builds and runs cheap C++ behavioral tests for logic that does not require a window, GPU, editor, or managed runtime.
3. `tools/run-qa.ps1` exercises the live editor/runtime/export path and remains the authoritative end-to-end regression suite.

The layers are intentionally different. A green source-contract check does not replace behavior tests, and unit tests do not replace Windows editor/runtime QA.

## Run the cheap behavioral suite

From PowerShell at the repository root:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\test.ps1
```

Use `-Configuration Release` or `-Configuration RelWithDebInfo` when needed. The script configures a separate `build-tests/` directory with `VESPERA_TESTS_ONLY=ON`, so SDL3, RmlUi, ImGui, D3D12, the editor, examples, and managed builds are not required for this loop.

The test-only configuration is self-contained/offline: it uses the repository's small doctest-compatible test header under `vendor-mini/` and builds `vespera_engine_logic_tests`, avoiding network fetches during the cheap loop.

## Current coverage

The first behavioral suite covers pure engine/project logic that previously relied on live QA or incidental use:

- key press/release frame edges;
- accumulated held-key repeat counts and per-frame reset;
- transient text and mouse-delta reset semantics;
- input-map binding counts/clear behavior;
- combined action values, clamping and pressed/down semantics;
- test action overrides;
- gamepad clamp/deadzone/disconnect behavior;
- project-authored input-map configuration and clean rejection of invalid bindings;
- project v10 save/load round-trip for shipping, VSync, input, startup UI, Lua entry and stable build-root data;
- unsupported project-version diagnostics;
- starter-template catalog behavior;
- template empty-name rejection before filesystem mutation;
- template folder-name sanitization/finalization;
- refusal to overwrite a non-empty project destination;
- RML/RCSS local `href` / `src` / `url(...)` scanning and project-relative resolution;
- RML/RCSS source-file rebasing with query/fragment preservation;
- controlled asset moves rewriting RML/RCSS dependents while preserving stable identity;
- legacy non-RML path-only dependents continuing to block unsafe renames;
- backend-neutral UI handle and legacy `UiSurface` semantic behavior;
- shared-player project discovery (explicit path, exactly-one packaged sibling, ambiguous siblings);
- explicit Project v8 startup-RML selection plus legacy first-build-root compatibility;
- Lua scripts as first-class assets/build roots plus stable Lua-entry move/delete repair;
- startup-UI build closure and stable fallback move/delete safety;
- runtime performance-counter smoothing and scene scale statistics;
- Emberlight Guild sample validation/build closure;
- fresh 3D starter regression coverage against hidden reference-game `floor_tiles.bmp` baggage;
- Performance Lab Project v10 validation and startup scene -> RML -> RCSS build closure;
- scene ID lookup-cache/hierarchy transform regression coverage, including the stable-ID reacquire case that fixed the 0.15.2 test UAF;
- project-package managed-stage preflight, shared-player branding ownership, Release-vs-Development PE subsystem policy, custom-runtime preservation, and portable hostfxr/runtime closure.

The current suite contains **52 behavioral cases**.

## Adding tests

Keep cheap tests deterministic and renderer-independent. Good candidates are path/reference logic, parsers/serializers, project/template logic, asset dependency scanning, validation, and other pure algorithms.

Do not force D3D12/editor/runtime behavior into this layer just to raise a unit-test count. UI interaction, renderer integration, Play Mode restoration, C# reload, MCP, export, and packaged-runtime behavior belong in the existing QA harness.

When a real bug is found, prefer the smallest regression test that fails before the fix and passes afterward. Then still run the nearest live QA scenario if the bug crosses an editor/runtime/shipping boundary.


## Combined release gate

For full Windows release validation, use the top-level runner:

```powershell
.\test-rc.ps1
```

It runs the deep automated RC gate once, prints plain PASS/FAIL/SKIP status, then launches the Release D3D12/Vulkan editor and reference-game visual smokes for human confirmation. The automated gate includes this behavioral suite, one Release native build, fresh Hub project creation, portable Release export/launch, sample exports, managed recovery, repeated Play/Stop and scene switching, asset move/save/reopen, runtime automation, MCP managed-build coverage, and bounded stress. `tools/run-release-gate.ps1` and `tools/run-rc-gate.ps1` remain lower-level diagnosis tools.
