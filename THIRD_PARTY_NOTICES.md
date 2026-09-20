# Third-party notices

Vespera Engine is licensed under the MIT License. The dependencies below keep
their own licenses and are not relicensed by Vespera.

Most production dependencies are fetched by CMake at build time rather than
vendored into the Vespera source tree. Their complete license texts are
included in the downloaded upstream source packages.

| Dependency | Version / pin | License | Use |
| --- | --- | --- | --- |
| SDL | 3.4.14 | zlib License | platform, input, windows, audio foundation |
| Vulkan-Headers | 1.4.357 | Apache-2.0 OR MIT | Vulkan API declarations for the optional Vulkan renderer |
| Lua | 5.4.9 | MIT License | optional project-level Lua runtime |
| RmlUi | 6.2 | MIT License | production runtime UI |
| FreeType | 2.14.1 | FreeType License (with upstream alternative terms) | font rendering for RmlUi |
| Dear ImGui | v1.92.9b-docking pinned commit `b48d1afbe8ee8b238e2961dc363a949dd7304e23` | MIT License | editor tooling UI |

## Local test shim

`vendor-mini/doctest/doctest.h` is a small Vespera-owned, doctest-compatible
test shim used by the lightweight behavioral test target. It is not a vendored
copy of the upstream doctest distribution and is covered by Vespera's MIT
license.

When distributing third-party binaries or source obtained through Vespera's
build process, retain the license and notice files supplied by those upstream
projects as required by their respective licenses.
