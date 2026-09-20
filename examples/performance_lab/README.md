# Vespera Performance Lab

A normal Vespera project used for repeatable Windows/D3D12 performance testing. It goes through the same player, project-owned C#, RmlUi, and Build Game/export pipeline as a user project.

## Cinematic stress workload

The lab is deliberately visual now: a scripted camera flies through a long benchmark gallery while the current tier adds increasingly dense moving geometry and moving point lights. The workload is generated at runtime through Vespera's public C# Entity/component API; there is no benchmark-only native renderer path.

Approximate cumulative load at the top tier:

- about **13,000 active mesh entities** (intentionally not instanced; off-camera primitives may now be frustum-culled)
- more than **9,000 animated transform updates per frame** driven through the managed/native gameplay API
- **32 moving point lights**
- moving overhead architecture plus four visually distinct workload zones
- compact RmlUi telemetry overlay so the scene remains visible

Presets are cumulative: Baseline -> Gameplay -> Heavy -> Torture. Baseline is intentionally reasonable; Torture is intentionally excessive.

`Run Full Benchmark` automatically runs a camera fly-through for every tier with a 1.25 second warmup followed by a 7 second sample. It reports average FPS, approximate 1% low (99th-percentile frame time), and worst frame time both on screen and through the normal Vespera log.

The sample is a regression/comparison tool, not a hardware leaderboard. Compare the same Vespera build/configuration/window size before and after engine changes.

## First Windows baseline and culling pass

User-measured pre-culling Release/shared-player baseline on an RTX 4080 SUPER at the current window size:

- Baseline: 165 avg / 103 1% low / 10.7 ms worst
- Gameplay: 164 avg / 102 1% low / 11.3 ms worst
- Heavy: 164 avg / 106 1% low / 11.4 ms worst
- Torture: **52 avg / 47 1% low / 23.6 ms worst**

The first targeted optimization after that measurement adds renderer-independent perspective-frustum sphere testing, D3D12 primitive-mesh frustum culling, and point-light influence-volume culling. The authored workload is unchanged. Re-run the same Release benchmark before changing workload counts so the effect of culling can be compared directly against the 52/47 Torture baseline.
