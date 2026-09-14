# Editor extensions — 0.8.10 foundation

`editor/extension_api.hpp` establishes **Editor Extension API v1** as the host-side registration contract.

The initial rule is the same one used for MCP: extensions register semantic commands/capabilities and do not receive privileged access to Scene vectors, D3D12 objects, CLR internals, raw undo storage, or native pointers.

0.8.10 includes:

- versioned extension descriptors
- duplicate/compatibility validation
- semantic command descriptors
- mutation / undo / Play-mode capability metadata
- a live registry in `EditorState`
- `Tools > Extensions` status display
- automation capability discovery through `vespera_list_capabilities`

The Vespera core editor registers its current automation/command surface through this registry so the API is dogfooded immediately.

**Not yet implemented:** loading arbitrary third-party DLLs/shared libraries. Dynamic plugin loading is deliberately deferred until the typed command surface has been stress-tested during 0.9.x. This avoids creating an unsafe plugin ABI before the semantic transaction layer stabilizes.

## 0.9.0 note

The extension registry remains a host contract rather than an arbitrary-DLL loader. Shipping/export and MCP continue to use semantic editor commands; dynamic plugin loading will come after 0.9 command-layer stress testing so Vespera Builder and third-party tools do not gain a privileged mutation backdoor.

## 0.9.1 note

The host registry remains intentionally load-free while the 40-tool command/automation surface is stress-tested. UI/project/asset authoring additions continue to use semantic public operations; arbitrary native DLL loading is still deferred rather than granting extensions privileged mutation access prematurely.
