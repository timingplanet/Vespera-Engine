# Vespera Engine roadmap

**Current status: 1.1.0 — released.**

Vespera 1.0 established the Windows x64 + Direct3D 12 production workflow. Vespera 1.1 extends the renderer/platform foundation toward Vulkan, experimental Linux support, and prebuilt distribution without replacing that Windows baseline.

## 1.1.0 release baseline

Vespera 1.1.0 shipped from the feature-frozen release-candidate line after the Windows release/export/editor/runtime gate passed. The release baseline is:

1. **Windows + Direct3D 12 is the production baseline.** The normal project, editor, Play, Build Game, and standalone workflow is the supported release path.
2. **Windows + Vulkan remains opt-in.** It is available for parity testing without replacing D3D12 as the default renderer.
3. **Linux + Vulkan remains experimental.** CI and limited validation exist, but broad distro/GPU/driver certification is intentionally deferred.
4. **Release artifacts stay reproducible and clean.** Public source, portable packages, installer output, version labels, legal notices, and contributor-facing docs should remain synchronized with shipped behavior.

The 1.1.0 release did not require a scene, prefab, project, package, or managed ABI break. Compatibility-sensitive serialized `sectorline.*` identifiers remain intentional unless a separately designed migration replaces them.

## After 1.1

Post-1.1 work returns to game-building and authoring depth: stronger asset/editor workflows, improved 2D/world tooling, renderer features, MCP/editor automation maturity, and later visual scripting over the same semantic gameplay API.

Large refactors remain post-1.1 maintainability work. In particular, the remaining large editor/renderer translation units should only be split along clear subsystem boundaries with regression coverage.
