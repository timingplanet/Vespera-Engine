# Editor Extensions

Vespera has a host-side extension registration API built around **semantic editor commands and capabilities**.

The important design rule is that extensions operate through supported editor operations instead of receiving raw pointers to scene vectors, renderer objects, CLR internals, or undo storage.

## What the extension surface describes

The host registry can describe:

- extension identity and compatibility;
- semantic commands;
- whether commands mutate project state;
- undo/Play-mode capability metadata;
- discoverable capabilities shown through the editor/automation surface.

The Vespera core editor itself registers its semantic command surface through this mechanism, which keeps the public transaction model exercised by normal tooling.

## Current boundary

The registry is **not** a general “drop any native DLL into a Plugins folder” loader. Arbitrary third-party native module loading is intentionally outside the current production workflow.

If you are building tooling today, prefer the semantic editor/automation interfaces documented under **Automation / MCP** instead of depending on engine-private structures.

## Why this matters

A semantic command boundary gives tools a chance to preserve:

- validation;
- undo/redo behavior;
- stable IDs;
- Play/Edit mode rules;
- error reporting;
- future compatibility.

Direct mutation of internal containers would bypass those guarantees.
