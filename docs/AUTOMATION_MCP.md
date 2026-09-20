# Automation / MCP

Vespera includes localhost-only developer-preview automation for editor QA, tooling, and MCP clients. It is disabled during an ordinary editor launch unless explicitly enabled.

## Start the editor with automation

```powershell
.\run.ps1 -Automation
```

The editor endpoint defaults to:

```text
127.0.0.1:46787
```

Requests are marshalled onto the editor's main-thread command path. Automation tools operate on semantic project/editor/runtime concepts rather than exposing native pointers or renderer/CLR objects.

## Start by checking the bridge

For MCP workflows, query bridge info and capabilities before destructive work. Confirm the connected editor matches the checkout you intend to automate.

Useful state/validation operations include project/editor state, project integrity, scene validation, console output, command history, asset queries, and dependency inspection.

## Authoring operations

The automation surface covers project settings, entities/hierarchy, components/properties, prefabs, assets, UI authoring, C# source/build operations, Play Mode controls, and export.

Use those semantic operations instead of directly rewriting `.slscene`, `.slprefab`, `.slui`, or metadata when you are testing the public workflow.

## Long-running managed builds

At the MCP boundary, C# build operations can return an operation ID. Poll the operation until it succeeds or fails instead of assuming the first response means compilation completed.

Guarded C# read/write operations are restricted to project managed-source paths and reject path traversal/unsupported files.

## Runtime QA

Play Mode automation can inject named input actions and query semantic UI/runtime events. A standalone runtime launched specifically with `--automation` can expose a separate localhost endpoint, normally on port `46788`.

A normal shipping runtime does **not** expose that endpoint.

## Use automation safely

Before destructive automation:

1. establish a clean project baseline;
2. inspect integrity and validation state;
3. mutate one subsystem at a time;
4. cross save/reload, Play/Stop, managed-reload, or export boundaries;
5. validate again;
6. restore or clearly report any destructive state.

Automation is powerful enough to reproduce real editor workflows, so treat failed operations as evidence to diagnose rather than something to silently work around.
