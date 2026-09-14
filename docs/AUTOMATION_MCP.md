# Vespera Editor Automation + MCP — 1.0.0 developer preview

Vespera automation is localhost-only and disabled by default. Editor requests are marshalled onto the editor main thread; MCP never receives Scene-vector pointers, C++ addresses, D3D12 handles, CLR objects or raw undo storage.

Start the editor with:

```powershell
.\run.ps1 -Automation
```

The editor VAP endpoint defaults to `127.0.0.1:46787`.

## Surface and stale-registration guard

1.0.0 retains **60 editor/VAP commands** and **71 MCP tools**. The extra MCP-only tools are long-operation polling, bridge fingerprinting, batched UI mutation, and eight standalone-runtime QA tools.

Always begin an automation QA session with:

```text
vespera_get_bridge_info
```

A current 1.0.0 bridge should report `bridge_version: 1.0.0`, `mcp_tool_count: 71`, and a connected editor whose public engine version is `1.0.0`. If those values disagree with the checkout, stop: the desktop MCP registration is pointing at a stale bridge or working directory.

`vespera_get_bridge_info` can also report whether an automation-enabled standalone runtime is reachable on `127.0.0.1:46788`.

## Long-running C# builds

`vespera_build_csharp` is asynchronous by default at the MCP boundary. It returns an `operation_id`; poll `vespera_get_operation` until `status` becomes `succeeded` or `failed`. The direct editor/VAP build remains synchronous/main-thread validated.

Guarded C# read/write accepts only relative `.cs` paths beneath the configured managed-project directory. Absolute paths, `..` escape, other extensions and files larger than 1 MiB are rejected. Writes use temporary/backup replacement. 0.9.6 retains the 0.9.5d behavior that permits these guarded writes/builds while Editor Play is active so the existing collectible game-assembly reload path can be exercised in Play Mode.

## Batched UI authoring

`vespera_apply_ui_batch` is MCP-local and accepts up to 256 UI operations in one tool call. Operations are `add`, `set`, `reparent` and `delete`. An `add` may provide an `alias`; later `node_id` or `parent_id` values can use that alias. The bridge still delegates each mutation through the normal VAP/editor command, so validation and public mutation paths remain authoritative while agent round-trip overhead drops dramatically.

`vespera_set_ui_node` in 0.9.6 also exposes the `.slui` v3 visual properties, including font family/weight/wrap/line spacing, surface opacity/radius/border/shadow, image fit, 9-slice insets and widget-state colors.

## Runtime QA tools in Editor Play

The editor command surface includes semantic runtime QA operations:

- `vespera_inject_input_action`
- `vespera_pointer_event`
- `vespera_ui_navigation`
- `vespera_text_input`
- `vespera_get_ui_runtime_state`
- `vespera_get_runtime_events`
- `vespera_clear_runtime_events`

Named input injection feeds the engine `InputSystem` action layer rather than synthesizing OS keystrokes. Pointer/navigation/text operations feed the native `UiRuntimeState`/`UiRenderCache` path. Runtime-event records contain sequence, frame, assembly generation, entity, component and callback. Teardown events are archived when Editor Play stops so QA can assert `OnDisable`/`OnDestroy` ordering after returning to Edit Mode.

## Standalone/exported-runtime automation

A reference/exported runtime launched with `--automation` starts a separate localhost-only VAP endpoint on `127.0.0.1:46788`. The MCP bridge routes these tools there:

- `vespera_runtime_get_state`
- `vespera_runtime_inject_input_action`
- `vespera_runtime_pointer_event`
- `vespera_runtime_ui_navigation`
- `vespera_runtime_text_input`
- `vespera_runtime_get_ui_state`
- `vespera_runtime_get_events`
- `vespera_runtime_clear_events`

`vespera_export_project` accepts `runtime_automation=true` only with `launch=true`, allowing QA to package and launch a runtime with the test endpoint enabled. Normal shipping launches do not enable this endpoint.

The runtime state reports scene/load/UI status, managed assembly generation and the reference sprite-clip availability used by the export regression. Semantic UI state includes node identity/type/enabled/text/progress plus hover/pressed/focused state. Screenshot capture is **not** implemented yet; this checkpoint validates behavior/state rather than pixels.

## Project/build authoring

`vespera_set_build_include` adds/removes explicit stable-ID build roots through the project authoring path. `vespera_get_project_settings` reports the current explicit build includes. Use this for runtime assets referenced dynamically or directly in game code that dependency scanning cannot infer.

## Built-in scenarios and runner

`vespera_run_qa_scenario` supports:

- `hierarchy`
- `play_cycle`
- `ui_layout`
- `managed_build_recovery`
- `asset_move_save_reopen`

The PowerShell runner can add bridge/runtime coverage:

```powershell
.\tools\run-qa.ps1 -ManagedRecovery -AssetMoveSave -McpManagedBuild -RuntimeTelemetry -RuntimeExportAutomation
```

`-RuntimeTelemetry` drives Editor Play UI/input and verifies structured lifecycle teardown. `-RuntimeExportAutomation` packages a Debug runtime, launches it with `--automation`, drives the same semantic UI/input path, verifies the direct sprite-clip dependency is present, and terminates the process it started.

The MCP schema remains Developer Preview before 1.0.


## RC gate

`tools/run-rc-gate.ps1` starts the Release editor on the reference project after the normal release/public-workflow gate, then exercises the same localhost VAP/MCP surfaces with managed-build recovery, 24 Play/Pause/Step/Stop cycles, repeated saved-scene switching, asset move/save/reopen repair, runtime telemetry, a Release exported-runtime automation pass, MCP async C# build polling, and bounded scale stress. It intentionally reuses the already-built Release binaries instead of triggering another native build.
