from pathlib import Path
import struct
import sys
import re

root = Path(__file__).resolve().parents[1]
required = [
    root / "CMakeLists.txt",
    root / "engine/CMakeLists.txt",
    root / "engine/include/vespera/assets/asset_catalog.hpp",
    root / "engine/include/vespera/assets/asset_authoring.hpp",
    root / "engine/include/vespera/assets/asset_reference.hpp",
    root / "engine/include/vespera/assets/rml_asset_references.hpp",
    root / "engine/include/vespera/assets/texture_importer.hpp",
    root / "engine/include/vespera/assets/sprite_clip_asset.hpp",
    root / "engine/include/vespera/assets/sprite_sheet_asset.hpp",
    root / "engine/include/vespera/assets/audio_clip_asset.hpp",
    root / "engine/include/vespera/assets/build_manifest.hpp",
    root / "engine/include/vespera/assets/font_asset.hpp",
    root / "engine/include/vespera/assets/material_asset.hpp",
    root / "engine/include/vespera/assets/project_package.hpp",
    root / "engine/include/vespera/render/material.hpp",
    root / "engine/include/vespera/assets/runtime_asset_monitor.hpp",
    root / "engine/include/vespera/project/project.hpp",
    root / "engine/include/vespera/project/project_templates.hpp",
    root / "engine/include/vespera/runtime/player_project.hpp",
    root / "engine/include/vespera/audio/audio.hpp",
    root / "engine/include/vespera/input/input.hpp",
    root / "engine/src/assets/asset_catalog.cpp",
    root / "engine/src/assets/asset_authoring.cpp",
    root / "engine/src/assets/rml_asset_references.cpp",
    root / "engine/src/assets/texture_importer.cpp",
    root / "engine/src/assets/sprite_clip_asset.cpp",
    root / "engine/src/assets/sprite_sheet_asset.cpp",
    root / "engine/src/assets/audio_clip_asset.cpp",
    root / "engine/src/assets/build_manifest.cpp",
    root / "engine/src/assets/font_asset.cpp",
    root / "engine/src/assets/material_asset.cpp",
    root / "engine/src/assets/project_package.cpp",
    root / "engine/src/assets/runtime_asset_monitor.cpp",
    root / "engine/src/project/project.cpp",
    root / "engine/src/project/project_templates.cpp",
    root / "engine/src/runtime/player_project.cpp",
    root / "engine/src/audio/audio.cpp",
    root / "engine/src/input/input.cpp",
    root / "engine/include/vespera/core/version.hpp.in",
    root / "engine/include/vespera/core/log.hpp",
    root / "engine/src/core/log.cpp",
    root / "engine/include/vespera/render/render_backend.hpp",
    root / "engine/include/vespera/render/view_frustum.hpp",
    root / "engine/include/vespera/render/d3d12/d3d12_native.hpp",
    root / "engine/include/vespera/scene/scene.hpp",
    root / "engine/include/vespera/scene/scene_io.hpp",
    root / "engine/include/vespera/scene/prefab.hpp",
    root / "engine/include/vespera/scene/scene_validation.hpp",
    root / "engine/include/vespera/scene/scene_collision.hpp",
    root / "engine/include/vespera/scene/scene_triggers.hpp",
    root / "engine/include/vespera/scene/scene_raycast.hpp",
    root / "engine/include/vespera/scene/component_access.hpp",
    root / "engine/include/vespera/scene/sprite_animation.hpp",
    root / "engine/include/vespera/scripting/managed_script_host.hpp",
    root / "engine/include/vespera/scripting/lua_script_host.hpp",
    root / "engine/include/vespera/ui/ui.hpp",
    root / "engine/include/vespera/ui/ui_io.hpp",
    root / "engine/include/vespera/ui/ui_render.hpp",
    root / "engine/include/vespera/ui/ui_surface.hpp",
    root / "engine/src/scene/scene_io.cpp",
    root / "engine/src/scene/prefab.cpp",
    root / "engine/src/scene/scene_validation.cpp",
    root / "engine/src/scene/scene_collision.cpp",
    root / "engine/src/scene/scene_triggers.cpp",
    root / "engine/src/scene/scene_raycast.cpp",
    root / "engine/src/scene/component_access.cpp",
    root / "engine/src/scene/sprite_animation.cpp",
    root / "engine/src/scripting/managed_script_host.cpp",
    root / "engine/src/scripting/lua_script_host.cpp",
    root / "engine/src/ui/ui.cpp",
    root / "engine/src/ui/ui_io.cpp",
    root / "engine/src/ui/ui_render.cpp",
    root / "engine/src/ui/ui_surface.cpp",
    root / "engine/src/render/view_frustum.cpp",
    root / "engine/src/render/d3d12/d3d12_renderer.cpp",
    root / "engine/src/world/sector_world.cpp",
    root / "engine/src/world/sector_mesh.cpp",
    root / "examples/reference_game/main.cpp",
    root / "examples/reference_game/managed/ReferenceGame.Scripts.csproj",
    root / "examples/reference_game/managed/ManagedSpinner.cs",
    root / "examples/reference_game/managed/GameplayApiDogfood.cs",
    root / "examples/reference_game/managed/TriggerReporter.cs",
    root / "examples/reference_game/managed/RuntimeSpawnReporter.cs",
    root / "examples/reference_game/managed/DestroyLifecycleProbe.cs",
    root / "examples/reference_game/assets/scripts/reference.lua",
    root / "examples/reference_game/assets/scripts/reference.lua.vmeta",
    root / "managed/Vespera.NET/Vespera.NET.csproj",
    root / "managed/Vespera.NET/Runtime.cs",
    root / "managed/Vespera.ScriptTool/Vespera.ScriptTool.csproj",
    root / "managed/Vespera.ScriptTool/Program.cs",
    root / "examples/reference_game/reference_textures.hpp",
    root / "examples/reference_game/VesperaReference.vesperaproject",
    root / "examples/reference_game/assets/textures/floor_tiles.bmp",
    root / "examples/reference_game/assets/textures/editor_badge.png",
    root / "examples/reference_game/assets/animations/watcher_walk.slspriteclip",
    root / "examples/reference_game/assets/animations/watcher_walk.slspritesheet",
    root / "examples/reference_game/assets/textures/watcher_sheet.bmp",
    root / "examples/reference_game/assets/audio/test_chime.slaudio",
    root / "examples/reference_game/assets/materials/concrete_lit.slmat",
    root / "examples/reference_game/assets/materials/concrete_lit.slmat.vmeta",
    root / "examples/reference_game/assets/materials/neon_unlit.slmat",
    root / "examples/reference_game/assets/materials/neon_unlit.slmat.vmeta",
    root / "examples/reference_game/assets/textures/floor_tiles.bmp.vmeta",
    root / "examples/reference_game/assets/scenes/connected_sectors.slscene",
    root / "examples/reference_game/assets/scenes/alternate_chamber.slscene",
    root / "examples/reference_game/assets/audio/test_chime.wav",
    root / "examples/reference_game/assets/prefabs/watcher.slprefab",
    root / "editor/main.cpp",
    root / "editor/editor_command.hpp",
    root / "editor/extension_api.hpp",
    root / "editor/play_runtime.hpp",
    root / "editor/play_runtime.cpp",
    root / "run-game.ps1",
    root / "run-project.ps1",
    root / "run.ps1",
    root / "export.ps1",
    root / "test.ps1",
    root / "tests/CMakeLists.txt",
    root / "tests/engine_logic_tests.cpp",
    root / "tools/packager/main.cpp",
    root / "tools/packager/CMakeLists.txt",
    root / "tools/build-managed-editor.ps1",
    root / "tools/run-release-gate.ps1",
    root / "runtime/player/CMakeLists.txt",
    root / "runtime/player/main.cpp",
    root / "templates/2d/managed/VesperaGame.Scripts.csproj",
    root / "templates/2d/managed/StarterGame.cs",
    root / "templates/3d/managed/VesperaGame.Scripts.csproj",
    root / "templates/3d/managed/StarterController.cs",
    root / "templates/empty/managed/VesperaGame.Scripts.csproj",
    root / "templates/empty/managed/Game.cs",
    root / "LICENSE",
    root / "THIRD_PARTY_NOTICES.md",
    root / "CONTRIBUTING.md",
    root / "docs/LIMITATIONS.md",
    root / "docs/index.html",
    root / "docs/getting-started.html",
    root / "docs/scripting.html",
    root / "docs/ui.html",
    root / "docs/shipping.html",
    root / "docs/reference.html",
    root / "docs/limitations.html",
    root / "docs/styles.css",
    root / "docs/site.js",
    root / "docs/ROADMAP.md",
    root / "docs/EDITOR.md",
    root / "docs/RUNTIME_UI.md",
    root / "docs/EXTENSIONS.md",
    root / "docs/EXPORTING.md",
    root / "docs/SCENE_FORMAT.md",
    root / "docs/MANAGED_SCRIPTING.md",
    root / "docs/LUA_SCRIPTING.md",
    root / "docs/RMLUI_EVALUATION.md",
    root / "docs/TESTING.md",
    root / "docs/RMLUI.md",
    root / "engine/include/vespera/ui/rmlui_surface.hpp",
    root / "engine/src/ui/rmlui_surface.cpp",
    root / "examples/rmlui_spike/CMakeLists.txt",
    root / "examples/rmlui_spike/main.cpp",
    root / "examples/rmlui_spike/ui/guild_showcase.rml",
    root / "examples/project_hub/CMakeLists.txt",
    root / "examples/project_hub/main.cpp",
    root / "examples/project_hub/ui/project_hub.rml",
    root / "templates/2d/Template.vesperaproject",
    root / "templates/2d/assets/scenes/main.slscene",
    root / "templates/2d/assets/ui/main.rml",
    root / "templates/2d/assets/ui/theme.rcss",
    root / "templates/3d/Template.vesperaproject",
    root / "templates/3d/assets/scenes/main.slscene",
    root / "templates/3d/assets/ui/main.rml",
    root / "templates/3d/assets/ui/theme.rcss",
    root / "templates/empty/Template.vesperaproject",
    root / "templates/empty/assets/scenes/main.slscene",
    root / "run-hub.ps1",
    root / "vespera.ps1",
    root / "run-rmlui-spike.ps1",
]
missing = [p for p in required if not p.exists()]
if missing:
    print("Missing required files:")
    for p in missing:
        print(" -", p.relative_to(root))
    sys.exit(1)

scene_h = (root / "engine/include/vespera/scene/scene.hpp").read_text(encoding="utf-8")
scene_io = (root / "engine/src/scene/scene_io.cpp").read_text(encoding="utf-8")
validation = (root / "engine/src/scene/scene_validation.cpp").read_text(encoding="utf-8")
collision_h = (root / "engine/include/vespera/scene/scene_collision.hpp").read_text(encoding="utf-8")
collision = (root / "engine/src/scene/scene_collision.cpp").read_text(encoding="utf-8")
triggers_h = (root / "engine/include/vespera/scene/scene_triggers.hpp").read_text(encoding="utf-8")
triggers = (root / "engine/src/scene/scene_triggers.cpp").read_text(encoding="utf-8")
raycast_h = (root / "engine/include/vespera/scene/scene_raycast.hpp").read_text(encoding="utf-8")
raycast = (root / "engine/src/scene/scene_raycast.cpp").read_text(encoding="utf-8")
component_access = (root / "engine/src/scene/component_access.cpp").read_text(encoding="utf-8")
prefab = (root / "engine/src/scene/prefab.cpp").read_text(encoding="utf-8")
asset_catalog = (root / "engine/src/assets/asset_catalog.cpp").read_text(encoding="utf-8")
asset_authoring = (root / "engine/src/assets/asset_authoring.cpp").read_text(encoding="utf-8")
rml_asset_references = (root / "engine/src/assets/rml_asset_references.cpp").read_text(encoding="utf-8")
texture_importer = (root / "engine/src/assets/texture_importer.cpp").read_text(encoding="utf-8")
sprite_clip_asset = (root / "engine/src/assets/sprite_clip_asset.cpp").read_text(encoding="utf-8")
sprite_sheet_asset = (root / "engine/src/assets/sprite_sheet_asset.cpp").read_text(encoding="utf-8")
audio_clip_asset = (root / "engine/src/assets/audio_clip_asset.cpp").read_text(encoding="utf-8")
build_manifest = (root / "engine/src/assets/build_manifest.cpp").read_text(encoding="utf-8")
font_asset = (root / "engine/src/assets/font_asset.cpp").read_text(encoding="utf-8")
material_asset = (root / "engine/src/assets/material_asset.cpp").read_text(encoding="utf-8")
material_h = (root / "engine/include/vespera/render/material.hpp").read_text(encoding="utf-8")
runtime_asset_monitor = (root / "engine/src/assets/runtime_asset_monitor.cpp").read_text(encoding="utf-8")
project_source = (root / "engine/src/project/project.cpp").read_text(encoding="utf-8")
project_asset = (root / "examples/reference_game/VesperaReference.vesperaproject").read_text(encoding="utf-8")
animation = (root / "engine/src/scene/sprite_animation.cpp").read_text(encoding="utf-8")
renderer = (root / "engine/src/render/d3d12/d3d12_renderer.cpp").read_text(encoding="utf-8")
view_frustum = (root / "engine/src/render/view_frustum.cpp").read_text(encoding="utf-8")
render_backend_h = (root / "engine/include/vespera/render/render_backend.hpp").read_text(encoding="utf-8")
d3d12_native_h = (root / "engine/include/vespera/render/d3d12/d3d12_native.hpp").read_text(encoding="utf-8")
editor_cmake = (root / "editor/CMakeLists.txt").read_text(encoding="utf-8")
world_h = (root / "engine/include/vespera/world/sector_world.hpp").read_text(encoding="utf-8")
editor = (root / "editor/main.cpp").read_text(encoding="utf-8")
asset = (root / "examples/reference_game/assets/scenes/connected_sectors.slscene").read_text(encoding="utf-8")
prefab_asset = (root / "examples/reference_game/assets/prefabs/watcher.slprefab").read_text(encoding="utf-8")
reference = (root / "examples/reference_game/main.cpp").read_text(encoding="utf-8")
reference_textures = (root / "examples/reference_game/reference_textures.hpp").read_text(encoding="utf-8")
build_script = (root / "build.ps1").read_text(encoding="utf-8")
run_game = (root / "run-game.ps1").read_text(encoding="utf-8")
engine_cmake = (root / "engine/CMakeLists.txt").read_text(encoding="utf-8")
root_cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
version_template = (root / "engine/include/vespera/core/version.hpp.in").read_text(encoding="utf-8")
managed_host = (root / "engine/src/scripting/managed_script_host.cpp").read_text(encoding="utf-8")
managed_host_h = (root / "engine/include/vespera/scripting/managed_script_host.hpp").read_text(encoding="utf-8")
lua_host = (root / "engine/src/scripting/lua_script_host.cpp").read_text(encoding="utf-8")
lua_host_h = (root / "engine/include/vespera/scripting/lua_script_host.hpp").read_text(encoding="utf-8")
application_source = (root / "engine/src/core/application.cpp").read_text(encoding="utf-8")
log_header = (root / "engine/include/vespera/core/log.hpp").read_text(encoding="utf-8")
log_source = (root / "engine/src/core/log.cpp").read_text(encoding="utf-8")
audio_source = (root / "engine/src/audio/audio.cpp").read_text(encoding="utf-8")
managed_api = (root / "managed/Vespera.NET/Runtime.cs").read_text(encoding="utf-8")
managed_spinner = (root / "examples/reference_game/managed/ManagedSpinner.cs").read_text(encoding="utf-8")
gameplay_dogfood = (root / "examples/reference_game/managed/GameplayApiDogfood.cs").read_text(encoding="utf-8")
trigger_reporter = (root / "examples/reference_game/managed/TriggerReporter.cs").read_text(encoding="utf-8")
runtime_spawn_reporter = (root / "examples/reference_game/managed/RuntimeSpawnReporter.cs").read_text(encoding="utf-8")
destroy_lifecycle_probe = (root / "examples/reference_game/managed/DestroyLifecycleProbe.cs").read_text(encoding="utf-8")
script_tool = (root / "managed/Vespera.ScriptTool/Program.cs").read_text(encoding="utf-8")
managed_build_helper = (root / "tools/build-managed-editor.ps1").read_text(encoding="utf-8")
release_gate = (root / "tools/run-release-gate.ps1").read_text(encoding="utf-8")
rc_gate = (root / "tools/run-rc-gate.ps1").read_text(encoding="utf-8")
input_h = (root / "engine/include/vespera/input/input.hpp").read_text(encoding="utf-8")
input_source = (root / "engine/src/input/input.cpp").read_text(encoding="utf-8")
play_runtime_h = (root / "editor/play_runtime.hpp").read_text(encoding="utf-8")
play_runtime = (root / "editor/play_runtime.cpp").read_text(encoding="utf-8")
automation_server = (root / "editor/automation_server.cpp").read_text(encoding="utf-8")
automation_protocol = (root / "editor/automation_protocol.cpp").read_text(encoding="utf-8")
automation_tools = (root / "editor/automation_tools.hpp").read_text(encoding="utf-8")
mcp_bridge = (root / "tools/vespera_mcp_server.py").read_text(encoding="utf-8")
ui_h = (root / "engine/include/vespera/ui/ui.hpp").read_text(encoding="utf-8")
ui_source = (root / "engine/src/ui/ui.cpp").read_text(encoding="utf-8")
ui_io = (root / "engine/src/ui/ui_io.cpp").read_text(encoding="utf-8")
ui_render = (root / "engine/src/ui/ui_render.cpp").read_text(encoding="utf-8")
ui_render_h = (root / "engine/include/vespera/ui/ui_render.hpp").read_text(encoding="utf-8")
ui_surface_h = (root / "engine/include/vespera/ui/ui_surface.hpp").read_text(encoding="utf-8")
ui_surface_source = (root / "engine/src/ui/ui_surface.cpp").read_text(encoding="utf-8")
extension_api = (root / "editor/extension_api.hpp").read_text(encoding="utf-8")
project_package = (root / "engine/src/assets/project_package.cpp").read_text(encoding="utf-8")
export_script = (root / "export.ps1").read_text(encoding="utf-8")
packager_cli = (root / "tools/packager/main.cpp").read_text(encoding="utf-8")
rmlui_h = (root / "engine/include/vespera/ui/rmlui_surface.hpp").read_text(encoding="utf-8")
rmlui_source = (root / "engine/src/ui/rmlui_surface.cpp").read_text(encoding="utf-8")
rmlui_sample = (root / "examples/rmlui_spike/main.cpp").read_text(encoding="utf-8")
rmlui_sample_cmake = (root / "examples/rmlui_spike/CMakeLists.txt").read_text(encoding="utf-8")
rmlui_document = (root / "examples/rmlui_spike/ui/guild_showcase.rml").read_text(encoding="utf-8")
rmlui_runner = (root / "run-rmlui-spike.ps1").read_text(encoding="utf-8")
rmlui_eval_doc = (root / "docs/RMLUI_EVALUATION.md").read_text(encoding="utf-8")
rmlui_doc = (root / "docs/RMLUI.md").read_text(encoding="utf-8")
project_templates_h = (root / "engine/include/vespera/project/project_templates.hpp").read_text(encoding="utf-8")
project_templates_source = (root / "engine/src/project/project_templates.cpp").read_text(encoding="utf-8")
player_project_h = (root / "engine/include/vespera/runtime/player_project.hpp").read_text(encoding="utf-8")
player_project_source = (root / "engine/src/runtime/player_project.cpp").read_text(encoding="utf-8")
player_main = (root / "runtime/player/main.cpp").read_text(encoding="utf-8")
player_cmake = (root / "runtime/player/CMakeLists.txt").read_text(encoding="utf-8")
run_project = (root / "run-project.ps1").read_text(encoding="utf-8")
project_hub = (root / "examples/project_hub/main.cpp").read_text(encoding="utf-8")
project_hub_rml = (root / "examples/project_hub/ui/project_hub.rml").read_text(encoding="utf-8")
project_hub_cmake = (root / "examples/project_hub/CMakeLists.txt").read_text(encoding="utf-8")
run_hub = (root / "run-hub.ps1").read_text(encoding="utf-8")
main_launcher = (root / "vespera.ps1").read_text(encoding="utf-8")

def _native_api_field_names_cpp(text: str):
    match = re.search(r"struct NativeApi \{(.*?)\n\};", text, re.S)
    if not match:
        return []
    fields = []
    for raw in match.group(1).splitlines():
        line = raw.strip()
        field = re.match(r"(?:[\w:<>*]+)\s+([A-Za-z_]\w*)\s*(?:=[^;]*)?;", line)
        if field:
            fields.append(field.group(1))
    return fields

def _native_api_field_names_cs(text: str):
    match = re.search(r"internal unsafe struct NativeApi\s*\{(.*?)\n\s*\}", text, re.S)
    if not match:
        return []
    fields = []
    for raw in match.group(1).splitlines():
        field = re.search(r"\s([A-Za-z_]\w*)\s*;", raw.strip())
        if field:
            fields.append(field.group(1))
    return fields

def _field_key(name: str):
    return re.sub(r"[^a-z0-9]", "", name.lower())

_native_cpp_fields = _native_api_field_names_cpp(managed_host)
_native_cs_fields = _native_api_field_names_cs(managed_api)
_native_api_parity = (
    len(_native_cpp_fields) == 88
    and len(_native_cs_fields) == 88
    and [_field_key(x) for x in _native_cpp_fields] == [_field_key(x) for x in _native_cs_fields]
)

checks = {
    "project version": "project(VesperaEngine VERSION 1.0.0" in root_cmake and 'set(VESPERA_VERSION_LABEL "1.0.0")' in root_cmake,
    "configured engine version header": "configure_file(" in root_cmake and "version.hpp.in" in root_cmake and "kEngineVersion" in version_template,
    "1.0 MIT license": "MIT License" in (root / "LICENSE").read_text(encoding="utf-8") and "Timingplanet" in (root / "LICENSE").read_text(encoding="utf-8") and not (root / "LICENSE-NOTE.txt").exists(),
    "1.0 third-party notices": "SDL" in (root / "THIRD_PARTY_NOTICES.md").read_text(encoding="utf-8") and "Lua" in (root / "THIRD_PARTY_NOTICES.md").read_text(encoding="utf-8") and "RmlUi" in (root / "THIRD_PARTY_NOTICES.md").read_text(encoding="utf-8") and "FreeType" in (root / "THIRD_PARTY_NOTICES.md").read_text(encoding="utf-8") and "Dear ImGui" in (root / "THIRD_PARTY_NOTICES.md").read_text(encoding="utf-8"),
    "1.0 public limitations": "Windows x64" in (root / "docs/LIMITATIONS.md").read_text(encoding="utf-8") and "2D / UI Foundation (Experimental)" in (root / "docs/LIMITATIONS.md").read_text(encoding="utf-8") and "One active RmlUi surface" in (root / "docs/LIMITATIONS.md").read_text(encoding="utf-8"),
    "1.0 documentation website": "Vespera Engine 1.0 documentation" in (root / "docs/index.html").read_text(encoding="utf-8") and "Build Game" in (root / "docs/shipping.html").read_text(encoding="utf-8") and "2D / UI Foundation" in (root / "docs/getting-started.html").read_text(encoding="utf-8"),
    "entity type": "struct Entity" in scene_h and "std::vector<Entity> entities" in scene_h,
    "entity identity metadata": 'std::string tag = "Untagged"' in scene_h and 'std::string layer = "Default"' in scene_h,
    "transform component": "struct TransformComponent" in scene_h and "Vec3 rotation" in scene_h and "Vec3 scale" in scene_h,
    "sprite renderer component": "struct SpriteRendererComponent" in scene_h and "std::optional<SpriteRendererComponent> sprite_renderer" in scene_h,
    "cylinder collider component": "struct CylinderColliderComponent" in scene_h and "std::optional<CylinderColliderComponent> cylinder_collider" in scene_h,
    "point light component": "struct PointLightComponent" in scene_h and "std::optional<PointLightComponent> point_light" in scene_h,
    "managed script attachments": "struct ManagedScriptComponent" in scene_h and "std::vector<ManagedScriptComponent> managed_scripts" in scene_h and "ManagedScriptFieldValue" in scene_h,
    "stable component keys": '"sectorline.transform"' in scene_h and '"sectorline.sprite_renderer"' in scene_h and '"sectorline.cylinder_collider"' in scene_h and '"sectorline.point_light"' in scene_h ,
    "component property metadata": "BuiltinPropertyInfo" in scene_h and "builtin_component_properties" in scene_h,
    "component key api": "has_component(std::string_view key)" in scene_h and "component_keys()" in scene_h,
    "generic component property access": "get_builtin_component_property" in component_access and "set_builtin_component_property" in component_access,
    "entity clone api": "clone_entity(SceneObjectId source_id" in scene_h,
    "scene tag/layer queries": "find_entity_with_tag" in scene_h and "find_entities_with_tag" in scene_h and "find_entities_on_layer" in scene_h,
    "scene component queries": "find_entities_with_component(std::string_view key)" in scene_h,
    "world point/name queries": "find_sector_index" in world_h and "find_sector_by_name" in world_h,
    "scene collision source linked": "src/scene/scene_collision.cpp" in engine_cmake,
    "trigger source linked": "src/scene/scene_triggers.cpp" in engine_cmake,
    "raycast source linked": "src/scene/scene_raycast.cpp" in engine_cmake,
    "generic component property access linked": "src/scene/component_access.cpp" in engine_cmake,
    "prefab source linked": "src/scene/prefab.cpp" in engine_cmake,
    "asset catalog source linked": "src/assets/asset_catalog.cpp" in engine_cmake and "src/assets/asset_authoring.cpp" in engine_cmake,
    "0.7.5 authored asset sources linked": "src/assets/sprite_sheet_asset.cpp" in engine_cmake and "src/assets/audio_clip_asset.cpp" in engine_cmake and "src/assets/build_manifest.cpp" in engine_cmake,
    "entity prefab source metadata": "AssetReference prefab_source" in scene_h,
    "prefab api": "load_entity_prefab" in prefab and "save_entity_prefab" in prefab and "instantiate_entity_prefab" in prefab,
    "prefab v5 writer": "kPrefabFormatVersion = 6" in prefab and 'command == "managed_field"' in prefab and 'output << "managed_script "' in prefab,
    "asset catalog api": "AssetCatalog::refresh" in asset_catalog and "AssetKind::EntityPrefab" in asset_catalog,
    "0.7 project workspace api": "load_vespera_project" in project_source and "save_vespera_project" in project_source and "validate_vespera_project" in project_source and 'vespera_project 10' in project_asset,
    "0.7 stable asset metadata": "vespera_meta" in asset_catalog and "find_by_id" in asset_catalog and "MetadataUpdated" in asset_catalog and "write_metadata" in asset_catalog,
    "0.7 expanded asset kinds": "AssetKind::Texture" in asset_catalog and "AssetKind::Audio" in asset_catalog and "AssetKind::Font" in asset_catalog,
    "0.7 texture importers": "import_bmp_texture" in texture_importer and "import_tga_texture" in texture_importer and "bits_per_pixel != 24u" in texture_importer and "texture.rgba8" in texture_importer,
    "scene collision api": "scene_circle_overlaps_solid_collider" in collision and "resolve_circle_motion_against_scene_colliders" in collision,
    "trigger overlap api": "scene_circle_overlapping_triggers" in collision_h and "scene_circle_overlapping_triggers" in collision,
    "trigger event tracker": "class TriggerTracker" in triggers_h and "TriggerEventType::Enter" in triggers,
    "2.5D scene raycast": "raycast_scene_2d" in raycast_h and "SceneRaycastHitType::EntityCollider" in raycast and "portal_open_at_height" in raycast,
    "scene v14 writer": "kSceneFormatVersion = 15" in scene_io and 'output << "  prefab_source_asset "' in scene_io and 'output << "  parent "' in scene_io,
    "scene point light records": 'command == "point_light"' in scene_io and 'output << "  point_light "' in scene_io,
    "scene managed script records": 'command == "managed_script"' in scene_io and 'output << "  managed_script "' in scene_io,
    "scene identity blocks retained": 'command == "identity"' in scene_io and 'output << "  identity "' in scene_io,
    "scene v8 prefab source": 'command == "prefab_source"' in scene_io and 'output << "  prefab_source "' in scene_io,
    "legacy migration retained": "Legacy v2-v4 sprite records migrate" in scene_io and "loaded_scene_version < 4" in scene_io,
    "reference scene v14 stable prefab source": "sectorline_scene 14" in asset and 'identity "enemy" "Actors"' in asset and 'prefab_source_asset "3ea2605121c0ad8e0875f49b7b5c334c" "prefabs/watcher.slprefab"' in asset,
    "reference managed script": 'managed_script "ReferenceGame.Scripts.ManagedSpinner" 1' in asset,
    "reference point lights": 'point_light 1.00 0.48 0.18' in asset and 'point_light 0.16 0.72 1.00' in asset,
    "reference trigger entity": 'entity 1004 1 "Chamber Threshold"' in asset and 'identity "checkpoint" "Triggers"' in asset and "cylinder_collider 1.15 2.40" in asset,
    "identity validation": "has an empty tag" in validation and "has an empty layer" in validation,
    "collider validation": "Cylinder Collider has a non-positive radius" in validation and "Cylinder Collider has a non-positive height" in validation,
    "point light validation": "Point Light has a negative intensity" in validation and "Point Light has a non-positive radius" in validation,
    "managed script validation": "C# Script #" in validation and "duplicate exposed field override" in validation,
    "renderer entity components retained": "for (const Entity& entity : scene.entities)" in renderer and "entity.sprite_renderer" in renderer,
    "renderer point lights": "fill_lighting_constants" in renderer and "evaluate_point_light" in renderer and "Num32BitValues = 48" in renderer and "kMaxActivePointLights = 32" in renderer and "LightingData : register(b1)" in renderer,
    "0.15 performance frustum culling": "make_view_frustum" in renderer and "frustum.intersects_sphere" in renderer and "ViewFrustum::intersects_sphere" in view_frustum and "src/render/view_frustum.cpp" in engine_cmake,
    "sprite animation uses world transform": "entity_world_transform" in animation and "world_transform.rotation.y" in animation,
    "editor entity filter": "ImGuiTextFilter entity_filter" in editor and 'Draw("Filter entities"' in editor,
    "editor tag layer inspector": 'InputText("Tag"' in editor and 'InputText("Layer"' in editor,
    "editor trigger creation": "command_create_trigger_entity" in editor and 'MenuItem("Trigger Volume")' in editor,
    "editor asset catalog": "AssetCatalog asset_catalog" in editor and 'Button("Reimport All",' in editor and 'Button("Refresh")' in editor,
    "editor prefab workflow": "command_create_prefab_from_selected" in editor and "command_instantiate_prefab" in editor and 'Button("Apply to Prefab"' in editor and 'Button("Revert"' in editor and 'Button("Unpack"' in editor,
    "editor transform inspector": 'Local Transform' in editor and 'DragFloat3("Rotation"' in editor and 'DragFloat3("Scale"' in editor,
    "editor generic add component": 'Button("Add Component...")' in editor and "kBuiltinComponentTypes" in editor,
    "editor collider inspector": 'TreeNodeEx("Cylinder Collider"' in editor and 'Checkbox("Is Trigger"' in editor,
    "editor point light authoring": "command_create_point_light_entity" in editor and 'TreeNodeEx("Point Light"' in editor and '[Light]' in editor,
    "editor C# script authoring": "entity.managed_scripts" in editor and "Exposed Fields" in editor and "load_managed_metadata" in editor,
    "editor undo path retained": "execute_editor_command" in editor and "undo_stack" in editor and "redo_stack" in editor,
    "scene validation command": 'MenuItem("Validate Scene")' in editor and "validate_scene" in editor,
    "sector authoring retained": "command_create_sector" in editor and "command_set_sector_portal_target" in editor,
    "material authoring retained": "command_create_material" in editor and '"UV Scale"' in editor,
    "sprite clip authoring retained": "command_create_clip" in editor and "resize_clip_frames" in editor,
    "reference trigger tracker": "TriggerTracker trigger_tracker_" in reference and "TriggerEventType::Enter" in reference,
    "reference raycast probe": "raycast_scene_2d" in reference and 'kProbe = "probe"' in reference,
    "reference query dogfood": 'find_entities_with_tag("enemy")' in reference and 'find_entities_on_layer("Triggers")' in reference and "BuiltinComponentType::PointLight" in reference,
    "reference prefab asset": "sectorline_prefab 1" in prefab_asset and 'name "Watcher"' in prefab_asset,
    "reference prefab dogfood": "load_entity_prefab" in reference and "instantiate_entity_prefab" in reference and "AssetCatalog asset_catalog_" in reference,
    "automatic asset sync": "$SourceAssets" in run_game and "$RuntimeAssets" in run_game and "Copy-Item $SourceAssets $RuntimeAssets -Recurse -Force" in run_game,
    "D3D12 renderer retained": "D3D12CreateDevice" in renderer and "CreateSwapChainForHwnd" in renderer,
    "hardware RT query retained": "D3D12_FEATURE_D3D12_OPTIONS5" in renderer,
    "staged renderer frame api": "begin_frame" in render_backend_h and "render_scene" in render_backend_h and "end_frame" in render_backend_h and "RenderViewport" in render_backend_h,
    "runtime render convenience retained": "virtual void render(const Scene& scene, double total_seconds)" in render_backend_h,
    "D3D12 native tooling bridge": "class D3D12NativeAccess" in d3d12_native_h and "d3d12_command_queue" in d3d12_native_h and "d3d12_command_list" in d3d12_native_h,
    "D3D12 staged frame implementation": "bool begin_frame(const RenderFrameConfig&" in renderer and "void render_scene(" in renderer and "bool end_frame() override" in renderer,
    "D3D12 subregion scene viewport": "requested_viewport" in renderer and "ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 1, &scissor)" in renderer,
    "editor D3D12 imgui backend": "imgui_impl_dx12.cpp" in editor_cmake and "ImGui_ImplDX12_Init" in editor and "ImGui_ImplDX12_RenderDrawData" in editor,
    "compact ImGui editor baseline": "VesperaWorkspaceToolbar" in editor and "AssetSourceRail" in editor and "ASSET INSPECTOR" in editor and "HIERARCHY" in editor,
    "0.7.4 sprite clip assets": "AssetKind::SpriteClip" in asset_catalog and "vespera_sprite_clip" in sprite_clip_asset and "load_sprite_clip_asset" in sprite_clip_asset,
    "0.7.4 dependency graph": "scan_dependencies" in asset_catalog and "dependencies_of" in asset_catalog and "broken_dependencies" in asset_catalog,
    "0.7.4 Windows image decode": "IWICImagingFactory" in texture_importer and "GUID_WICPixelFormat32bppRGBA" in texture_importer,
    "0.7.4 folder browsing": "asset_browser_folder" in editor and "PROJECT" in editor,
    "0.7.5 sprite sheet asset": "vespera_sprite_sheet" in sprite_sheet_asset and "slice_rgba" in sprite_sheet_asset and "AssetKind::SpriteSheet" in asset_catalog,
    "0.7.5 audio clip asset": "vespera_audio_clip" in audio_clip_asset and "AssetKind::AudioClip" in asset_catalog,
    "0.7.5 build manifest": "build_project_asset_manifest" in build_manifest and "build_include" in project_source and "build_include" in project_asset,
    "0.7.5 editor asset workflow": "Runtime-ready external asset" in editor and "Include in Standalone Build" in editor and "Build manifest:" in editor,
    "0.7.6 stable descriptor references": "source_asset" in sprite_sheet_asset and "source_asset" in audio_clip_asset and "resolve_reference" in asset_catalog,
    "0.7.6 project stable roots": "startup_scene_asset" in project_source and "build_include_asset" in project_source and "startup_scene_asset" in project_asset,
    "0.7.6 move-aware catalog": "AssetCatalogChangeKind::Moved" in asset_catalog and "assets_moved" in asset_catalog and "stale_fallback_paths" in asset_catalog,
    "0.7.6 editor stable-ref UX": "Stable asset ID:" in editor and "stable refs" in editor and "Fallback path is stale" in editor,
    "0.7.7 safe asset authoring": "move_project_asset" in asset_authoring and "repair_stable_asset_fallbacks" in asset_authoring and "path-only dependent" in asset_authoring and "src/assets/asset_authoring.cpp" in engine_cmake,
    "0.7.7 editor move repair workflow": "Move / Rename Asset" in editor and "Repair Stable Fallback Paths" in editor and "repair_asset_fallback_paths" in editor,
    "0.7.7 managed stable asset api": "public readonly struct AssetReference" in managed_api and "public static class Assets" in managed_api and "ResolveAssetPath" in managed_api and "FindAssetId" in managed_api,
    "0.7.7 managed asset dogfood": "WatcherPrefab" in gameplay_dogfood and "ChimeAudio" in gameplay_dogfood and "stable asset API prefab-id" in gameplay_dogfood,
    "0.7.8 scene v12 stable prefab reference": 'constexpr int kSceneFormatVersion = 15' in scene_io and 'command == "prefab_source_asset"' in scene_io and 'prefab_source.asset_id' in scene_io,
    "0.7.8 stable scene dependency scan": 'command == "prefab_source_asset"' in asset_catalog and 'Prefab source' in asset_catalog,
    "editor live 3D view": "struct SceneView3DState" in editor and 'ImGui::Begin("Scene")' in editor and "draw_scene_view_3d(state)" in editor,
    "editor 3D navigation": "RMB + WASD/QE" in editor and "ImGuiKey_W" in editor and "ImGuiKey_Q" in editor and "ImGuiKey_E" in editor,
    "editor 3D viewport DPI conversion": "scene_view_3d_pixel_viewport" in editor and "SDL_GetWindowSizeInPixels" in editor,
    "editor D3D descriptor allocator": "EditorD3D12DescriptorAllocator" in editor and "kImGuiDescriptorCount = 64" in editor,
    "0.8.0 familiar workspace": 'DockBuilderDockWindow("Hierarchy", left)' in editor and 'DockBuilderDockWindow("Project", bottom)' in editor and 'DockBuilderDockWindow("Console", bottom)' in editor and 'DockBuilderDockWindow("Inspector", right)' in editor and 'DockBuilderDockWindow("Scene", center)' in editor and 'DockBuilderDockWindow("Sector", center)' in editor,
    "0.8.0 project breadcrumbs": "breadcrumb_root" in editor and "asset_kind_filter" in editor,
    "0.8.0 console filters": "console_show_info" in editor and "console_show_warnings" in editor and "console_show_errors" in editor,
    "0.8.0 layout reset": "request_reset_layout" in editor and 'MenuItem("Reset Editor Layout")' in editor,
    "0.8.0 typed command taxonomy": "editor_command.hpp" in editor and "EditorCommandKind" in editor and "command_log" in editor,
    "0.8.1 scene tools": "SceneTool::Move" in editor and "SceneTool::Rotate" in editor and "SceneTool::Scale" in editor,
    "0.8.1 3d entity picking": "pick_entity_in_3d" in editor and "project_scene_point" in editor,
    "0.8.1 transform gizmos": "gizmo_world_axis" in editor and "commit_3d_gizmo_drag" in editor and "Move entity gizmo" in editor,
    "0.8.1 transform command audit": "entity.transform.move" in (root / "editor" / "editor_command.hpp").read_text() and "entity.transform.rotate" in (root / "editor" / "editor_command.hpp").read_text(),
    "authored reference texture imports": "reference_content::register_textures" in reference and "reference_content::register_textures" not in editor and "import_texture" in reference_textures and "watcher_d{}_f{}.bmp" in reference_textures,
    "reference game scene load": "load_scene_text" in reference and "connected_sectors.slscene" in reference,
    "managed host linked": "src/scripting/managed_script_host.cpp" in engine_cmake and "hostfxr_initialize_for_runtime_config" in managed_host,
    "managed ABI v12": "kManagedAbiVersion = 12" in managed_host and "api->AbiVersion != 12" in managed_api and "struct_size" in managed_host and "StructSize" in managed_api,
    "managed NativeApi 88-field parity": _native_api_parity,
    "managed camera API": "GetCameraCallback" in managed_host and "SetCameraCallback" in managed_host and "public static class Camera" in managed_api and "camera_was_written_last_update" in managed_host_h,
    "0.9.9c managed UI surface routing": "UiSurface* ui_surface" in managed_host_h and "UiHandleTable ui_handles" in managed_host and "UiDocument* ui" not in managed_host and "UiGetValueCallback" in managed_host and "UiSetPropertyCallback" in managed_host and "public string ValueText" in managed_api and "SetClass(string className" in managed_api,
    "0.9.9c managed UI callback signatures": "using UiGetValueCallback = const char*(*)(void*, std::uint64_t);" in managed_host and "using UiSetValueCallback = int(*)(void*, std::uint64_t, const char*);" in managed_host and "using UiSetPropertyCallback = int(*)(void*, std::uint64_t, const char*, const char*);" in managed_host and "using UiSetClassCallback = int(*)(void*, std::uint64_t, const char*, int);" in managed_host and "delegate* unmanaged[Cdecl]<nint, ulong, byte*> UiGetValue;" in managed_api and "delegate* unmanaged[Cdecl]<nint, ulong, byte*, int> UiSetValue;" in managed_api and "delegate* unmanaged[Cdecl]<nint, ulong, byte*, byte*, int> UiSetProperty;" in managed_api and "delegate* unmanaged[Cdecl]<nint, ulong, byte*, int, int> UiSetClass;" in managed_api,
    "Vespera.NET lifecycle": "abstract class Component" in managed_api and "virtual void Start()" in managed_api and "virtual void Update(float deltaTime)" in managed_api and "virtual void OnDestroy()" in managed_api,
    "managed Entity reference handle": "public sealed class Entity" in managed_api and "public Entity Entity { get; private set; } = null!;" in managed_api and "public Transform Transform { get; }" in managed_api,
    "managed dispatcher": "UnmanagedCallersOnly" in managed_api and "Dispatch(int command" in managed_api,
    "managed expose reflection": "class ExposeAttribute" in managed_api and "ApplyFieldCommand" in managed_api and "TryParseExposedValue" in managed_api,
    "managed metadata tool": "vespera_script_metadata 4" in script_tool and "GetCustomAttribute<ExposeAttribute>" in script_tool and "TooltipAttribute" in script_tool and "RangeAttribute" in script_tool and "field.FieldType.IsEnum" in script_tool and 'return "color"' in script_tool,
    "reference C# dogfood": "class ManagedSpinner" in managed_spinner and "[Expose" in managed_spinner and "Entity.Transform.Rotation" in managed_spinner and "SpinDirection" in managed_spinner and "ManagedScriptHost managed_host_" in reference,
    "managed build integration": "build-managed-editor.ps1" in build_script and "last-good" in build_script and "$ManagedBuild" in run_game,
    "managed last-good staging": "staging.$PID" in managed_build_helper and "Last-good protection" in managed_build_helper and "Vespera.ManagedBuildDiagnostics.txt" in managed_build_helper,
    "editor managed build UX": 'Button("Build C#")' in editor and "build_managed_scripts" in editor and "C# build failed. The previous good managed assembly" in editor,
    "managed metadata v4 editor": "version > 4" in editor and "has_range" in editor and "tooltip" in editor and "enum_values" in editor and "aliases" in editor,
    "precise ranged inspector entry": '##range_precise' in editor and 'slider + precise entry' in editor,
    "managed script ordering controls": 'Button("Move Up")' in editor and 'Button("Move Down")' in editor and 'Duplicate C# Script' in editor,
    "managed exposed field types": "type == typeof(Vector2)" in managed_api and "type == typeof(Color)" in managed_api and "type.IsEnum" in managed_api,
    "managed reload groundwork": "isCollectible: true" in managed_api and "LoadFromStream" in managed_api and "bool ManagedScriptHost::reload()" in managed_host and "reloaded in-place" in managed_host,
    "managed unload cleanup": "OnDestroy" in managed_api and "VerifyCollectibleUnload" in managed_api and "AssemblyLoadContext is still alive" in managed_api,
    "reference live reload dogfood": "context.input.pressed(Key::Space)" in reference and "managed_host_.reload()" in reference and "Space manual script reload" in reference,
    "runtime managed mirror refresh": "RuntimeManagedDir" in managed_build_helper and "Vespera.NET.dll" in managed_build_helper and "collectible" in managed_build_helper,
    "managed fault containment": "instance.Faulted = true" in managed_api and "FormatScriptException" in managed_api and "entity {instance.EntityId}" in managed_api,
    "managed automatic reload": "auto_reload" in managed_host and "Detected a stable managed game build" in managed_host and "auto_reload = true" in reference,
    "managed two-phase replacement": "ValidateCandidateScript" in managed_host and "CommitCandidateAssembly" in managed_host and "DiscardCandidateAssembly" in managed_host and "Prepared managed game assembly candidate" in managed_api and "CommitCandidateAssemblyCommand" in managed_api,
    "managed candidate command id parity": "ValidateCandidateScript = 8" in managed_host and "CommitCandidateAssembly = 9" in managed_host and "DiscardCandidateAssembly = 10" in managed_host and "ValidateCandidateScriptCommand = 8" in managed_api and "CommitCandidateAssemblyCommand = 9" in managed_api and "DiscardCandidateAssemblyCommand = 10" in managed_api,
    "managed reload scene preflight": "replacement is missing/invalid for one or more enabled scene attachments" in managed_host and "active scripts were preserved" in managed_host,
    "managed expose contract validation": "VESPERA1101" in script_tool and "VESPERA1102" in script_tool and "VESPERA1103" in script_tool and "VESPERA1104" in script_tool and "parameterless constructor" in script_tool,
    "managed field rename aliases": "FormerlySerializedAsAttribute" in managed_api and "alias" in script_tool and "Migrate Name" in editor,
    "managed atomic runtime mirror": "Copy-AtomicFile" in managed_build_helper and "ReferenceGame.Scripts.dll" in managed_build_helper and "committed" in managed_build_helper,
    "0.6 managed input api": "public static class Input" in managed_api and "ActionPressed" in managed_api and "action_pressed" in managed_host,
    "0.6 managed time/timer api": "public static class Time" in managed_api and "public sealed class GameTimer" in managed_api,
    "0.6 managed scene/entity api": "public static class Scene" in managed_api and "CreateEntity" in managed_api and "CloneEntity" in managed_api and "DestroyEntity" in managed_api and "GetEntityTag" in managed_api and "set_entity_layer" in managed_host and "AddComponent" in managed_api and "managed_add_component" in managed_host,
    "0.6 managed prefab api": "public static class Prefab" in managed_api and "InstantiatePrefab" in managed_api and "managed_instantiate_prefab" in managed_host,
    "0.6 managed raycast api": "public static class Physics" in managed_api and "Raycast2D" in managed_api and "managed_raycast_2d" in managed_host,
    "0.6 managed save data": "public static class SaveData" in managed_api and "SaveDirectory" in managed_api and "runtime" in managed_api and "saves" in managed_api,
    "0.6 managed trigger callbacks": "OnTriggerEnter" in managed_api and "OnTriggerExit" in managed_api and "TriggerEnter = 11" in managed_host and "trigger_enter" in reference,
    "0.6 reference gameplay dogfood": "class GameplayApiDogfood" in gameplay_dogfood and "Prefab.Instantiate" in gameplay_dogfood and "Physics.Raycast2D" in gameplay_dogfood and "SaveData" in gameplay_dogfood,
    "0.6 reference trigger dogfood": "class TriggerReporter" in trigger_reporter and "OnTriggerEnter" in trigger_reporter and "ReferenceGame.Scripts.TriggerReporter" in asset,
    "0.6.1 audio source linked": "src/audio/audio.cpp" in engine_cmake and "SDL_OpenAudioDeviceStream" in audio_source and "SDL_LoadWAV" in audio_source,
    "0.6.1 managed audio api": "public static class Audio" in managed_api and "AudioPlayOneShot" in managed_api and "managed_audio_play_one_shot" in managed_host,
    "0.6.1 deferred scene switching": "RequestSceneLoad" in managed_api and "take_scene_load_request" in managed_host and "load_runtime_scene" in reference and "alternate_chamber.slscene" in gameplay_dogfood,
    "0.6.1 dynamic managed scripts": "AddScript<T>" in managed_api and "Runtime-attached" in managed_host and "StartScript = 13" in managed_host and "DestroyScript = 14" in managed_host and "RuntimeSpawnReporter" in runtime_spawn_reporter,
    "0.6.1 semantic managed property bridge": "GetPropertyFloat" in managed_api and "SetPropertyColor" in managed_api and "get_builtin_component_property" in managed_host and "typed SpriteRenderer animation/tint control active" in gameplay_dogfood,
    "0.6.1 reference audio dogfood": "Audio.PlayOneShot" in gameplay_dogfood and "audio_test" in gameplay_dogfood and "test_chime.wav" in gameplay_dogfood,
    "0.6.1+ scene/prefab compatibility baseline": "kSceneFormatVersion = 15" in scene_io and "kPrefabFormatVersion = 6" in prefab,
    "0.6.2 handle audio voices": "AudioVoiceHandle" in audio_source and "set_voice_position" in audio_source and "SDL_PauseAudioStreamDevice" in audio_source and "loop" in audio_source,
    "0.6.2 managed audio source/listener": "public sealed class AudioSource" in managed_api and "PlaySpatial" in managed_api and "public static class AudioListener" in managed_api and "AudioPlayVoice" in managed_api and "managed_audio_play_voice" in managed_host,
    "0.6.2 camera listener default": "set_camera_listener_position(scene_.camera.position)" in application_source,
    "0.6.2 overlap collision queries": "scene_circle_overlapping_colliders" in collision and "OverlapCircle2D" in managed_api and "managed_overlap_circle_2d" in managed_host,
    "0.6.2 resolve motion managed query": "ResolveCircleMotion2D" in managed_api and "managed_resolve_circle_motion_2d" in managed_host,
    "0.6.2 enable disable lifecycle": "virtual void OnEnable()" in managed_api and "virtual void OnDisable()" in managed_api and "RuntimeSpawnReporter.OnEnable" in runtime_spawn_reporter and "RuntimeSpawnReporter.OnDisable" in runtime_spawn_reporter,
    "0.6.2 typed sprite animator": "public sealed class SpriteRenderer" in managed_api and "sprite.Play" in gameplay_dogfood and "typed SpriteRenderer animation/tint control active" in gameplay_dogfood,
    "0.6.2 stable managed time epoch": "Keep Time.ElapsedTime on the" in managed_api and "Time.Reset();" not in managed_api[managed_api.find("private static int Initialize"):managed_api.find("private static Assembly LoadAssemblyUnlocked")],
    "0.6.2 reference spatial audio dogfood": "Audio.PlaySpatial" in gameplay_dogfood and "audio_spatial" in gameplay_dogfood and "lifecycle_test" in gameplay_dogfood,
    "0.6.3 scene collection queries": "QueryEntities" in managed_api and "managed_query_entities" in managed_host and "FindAllWithTag" in managed_api and "FindAllOnLayer" in managed_api and "FindAllWithComponent" in managed_api and "collection queries" in gameplay_dogfood,
    "0.6.3 deferred entity destroy": "pending_destroy_ids" in managed_host and "Deferred Entity.Destroy committed" in managed_host and "DestroyLifecycleProbe" in destroy_lifecycle_probe and "ExistsBeforeNativeRemove" in destroy_lifecycle_probe,
    "0.6.3 typed collider light wrappers": "public sealed class CylinderCollider" in managed_api and "public sealed class PointLight" in managed_api and "EnsureCylinderCollider" in managed_api and "EnsurePointLight" in managed_api and "typed Collider + PointLight active" in gameplay_dogfood,
    "0.6.3 trigger stay lifecycle": "virtual void OnTriggerStay" in managed_api and "TriggerStay = 15" in managed_host and "trigger_stay" in reference and "OnTriggerStay" in trigger_reporter,
    "0.6.3 managed audio ownership": "FollowTarget" in managed_api and "UpdateManagedSources" in managed_api and "StopAllManagedSources" in managed_api and "AudioActiveVoiceCount" in managed_api and "managed_audio_stop_all" in managed_host,
    "0.6.3 save timer polish": "SetVector3" in managed_api and "public static string[] Keys" in managed_api and "public float Remaining" in managed_api and "public float Progress" in managed_api,
    "0.6.3+ authored formats compatible": "kSceneFormatVersion = 15" in scene_io and "kPrefabFormatVersion = 6" in prefab,
    "0.7 project-aware reference runtime": "load_vespera_project" in reference and "Project validation: passed" in reference and "project_.managed_assembly" in reference,
    "0.7 project-aware editor": "open_project" in editor and "Project Settings" in editor and "Refresh / Reimport" in editor and "Validate to Console" in editor,
    "0.7.1 asset inspector": "SelectionKind::Asset" in editor and "Copy Asset ID" in editor and "AssetBrowser" in editor,
    "0.7.1 project runtime settings": "window_width" in project_source and "window_title" in project_source and "startup_project.window_width" in reference,
    "0.7.1 metadata fast path": "source_mtime" in asset_catalog and "fast_path_hits" in asset_catalog and "orphaned_metadata" in asset_catalog,
    "0.7 project-aware managed build": "GameAssemblyName" in managed_build_helper and "managed_assembly" in build_script,
    "VS bundled CMake discovery": "vswhere.exe" in build_script and "CommonExtensions\\Microsoft\\CMake" in build_script,
}

checks.update({
    "0.7.9 vmeta v3 texture settings": "vespera_meta 3" in asset_catalog and "texture_usage" in asset_catalog and "save_texture_import_settings" in asset_catalog,
    "0.7.9 texture inspector settings": "Apply Import Settings" in editor and "const char* usage_items[] = {\"World\", \"Sprite\", \"UI\", \"Data\"}" in editor,
    "0.7.9 font asset validation": "inspect_font_asset" in font_asset and "OpenType/CFF" in font_asset and "font_asset.cpp" in engine_cmake,
    "0.7.9 runtime asset invalidation": "RuntimeAssetMonitor" in runtime_asset_monitor and "Runtime asset invalidation" in reference and "runtime_asset_monitor.cpp" in engine_cmake,
    "0.7.9 safe delete preflight": "preflight_delete_project_asset" in asset_authoring and "Check Delete Safety" in editor,
    "0.7.9 deterministic build asset index": "vespera_build_asset_index 1" in build_manifest and "Build asset index: wrote" in reference,
})

failed = [name for name, ok in checks.items() if not ok]
if failed:
    print("Source checks failed:")
    for name in failed:
        print(" -", name)
    sys.exit(1)

legacy_leaks = []
for path in [root / "engine", root / "editor", root / "examples"]:
    for file in list(path.rglob("*.cpp")) + list(path.rglob("*.hpp")):
        text = file.read_text(encoding="utf-8")
        if "scene.sprites" in text or "SpriteActor" in text:
            legacy_leaks.append(str(file.relative_to(root)))
if legacy_leaks:
    print("Legacy sprite-object API leaked into 0.5.x sources:")
    for item in sorted(set(legacy_leaks)):
        print(" -", item)
    sys.exit(1)

# The reference sprite sheet must preserve the alpha carried by its source
# frames. A 24-bit export silently turns transparent sprite padding opaque and
# produces black quads in the D3D12 runtime, so guard the dogfood asset here.
sheet_path = root / "examples/reference_game/assets/textures/watcher_sheet.bmp"
sheet_bytes = sheet_path.read_bytes()
if sheet_bytes[:2] != b"BM" or len(sheet_bytes) < 54:
    print("Reference watcher sheet is not a valid BMP")
    sys.exit(1)
pixel_offset = struct.unpack_from("<I", sheet_bytes, 10)[0]
bits_per_pixel = struct.unpack_from("<H", sheet_bytes, 28)[0]
if bits_per_pixel != 32:
    print(f"Reference watcher sheet must be 32-bit BGRA to preserve transparency (got {bits_per_pixel}-bit)")
    sys.exit(1)
alpha = sheet_bytes[pixel_offset + 3::4]
if not alpha or 0 not in alpha or not any(value >= 128 for value in alpha):
    print("Reference watcher sheet must contain both transparent and opaque pixels")
    sys.exit(1)

checks.update({
    "0.8.4 mesh renderer component": "sectorline.mesh_renderer" in scene_h and "PrimitiveMeshType" in scene_h and "mesh_renderer" in scene_io,
    "0.8.4 primitive editor create": "command_create_primitive_entity" in editor and 'BeginMenu("3D Object")' in editor,
    "0.8.4 primitive renderer": "create_primitive_geometry" in renderer and "primitive_index_counts_" in renderer,
    "0.8.5 scene v14": "kSceneFormatVersion = 15" in scene_io and 'command == "parent"' in scene_io and 'command == "mesh_renderer"' in scene_io,
    "0.8.4 prefab v5": "kPrefabFormatVersion = 6" in prefab and 'command == "mesh_renderer"' in prefab,
    "0.8.4 entity multi-selection": "selected_entity_ids" in editor and "toggle_entity_selection" in editor and "select_entity_range" in editor,
    "0.8.4 group transform gizmo": "drag_start_transforms" in editor and "selected_entity_gizmo_pivot" in editor and "rotate_vector_around_axis" in editor,
    "0.8.4b unique vec3 helpers": editor.count("float vec3_dot(") == 1 and editor.count("vespera::Vec3 vec3_cross(") == 1,
    "0.8.4 center pivot control": 'view.center_pivot ? "Center" : "Pivot"' in editor,
    "0.8.5 hierarchy parent/reorder": "VESPERA_ENTITY_HIERARCHY" in editor and "command_reparent_entity" in editor and "command_reorder_entity" in editor and 'return "entity.reparent"' in (root / "editor/editor_command.hpp").read_text(),
    "0.8.4 project grid": "AssetGrid" in editor and "asset_grid_view" in editor and "asset_tile_size" in editor,
    "0.8.5 hierarchy module": (root / "engine/include/vespera/scene/scene_hierarchy.hpp").exists() and (root / "engine/src/scene/scene_hierarchy.cpp").exists(),
    "0.8.5 project texture previews": "texture_asset_thumbnail" in editor and "draw_cpu_thumbnail" in editor,
    "0.8.5 approved editor branding": (root / "editor/branding/vespera_mark.png").exists() and "SDL_SetWindowIcon" in editor,
    "0.8.6 game view play mode": 'ImGui::Begin("Game")' in editor and "EditorPlayState" in editor and "start_play_mode" in editor and "step_play_mode" in editor and 'DockBuilderDockWindow("Game"' in editor,
    "0.8.6 isolated play scene": "vespera::Scene play_scene" in editor and "state.play_scene = state.scene" in editor and "edit scene restored unchanged" in editor,
    "0.8.6 asset drag drop": "VESPERA_PROJECT_ASSET" in editor and "begin_project_asset_drag" in editor and "accept_texture_asset_drop" in editor and "DropPrefabIntoScene" in editor,
    "0.8.6 prefab workflow polish": 'Button("Select Source"' in editor and 'MenuItem("Apply to Prefab")' in editor and 'MenuItem("Revert from Prefab")' in editor,
    "0.8.6 typed command transaction metadata": "before_state_id" in (root / "editor/editor_command.hpp").read_text() and "target_asset_id" in (root / "editor/editor_command.hpp").read_text() and 'return "play.enter"' in (root / "editor/editor_command.hpp").read_text(),
    "0.8.6 many light buffer": "kMaxActivePointLights = 32" in renderer and "lighting_buffers_" in renderer and "SetGraphicsRootConstantBufferView" in renderer and "Point lighting: up to" in renderer,
    "0.8.6 multi-view light upload safety": "kMaxLightingViewsPerFrame" in renderer and "lighting_view_cursor_" in renderer and "kLightingConstantStride" in renderer,
    "0.8.7 interactive play runtime": "class PlayRuntime" in play_runtime_h and "ManagedScriptHost managed_" in play_runtime_h and "update_reference_camera_controller" in play_runtime and "update_triggers" in play_runtime and "load_requested_scene" in play_runtime,
    "0.8.7 game input capture": "SDL_SetWindowRelativeMouseMode" in editor and "SDL_GetRelativeMouseState" in editor and "feed_play_runtime_input" in editor and "INPUT CAPTURED" in editor,
    "0.8.7 host input feed": "host_begin_frame" in input_h and "host_set_key" in input_h and "host_add_mouse_delta" in input_h and "void InputSystem::host_begin_frame" in input_source,
    "0.8.7 project input bindings retained": 'output << "vespera_project 10\\n"' in project_source and 'command == "input_bind"' in project_source and "configure_project_input_map" in project_source and 'input_bind "move_forward" "key" "W"' in project_asset,
    "0.8.7 shared reference input map": "configure_project_input_map(project_, input_map" in reference and "map.bind_key(std::string(kMoveForward)" not in reference,
    "0.8.7 play audio pause": "set_all_paused" in audio_source and "set_paused(bool paused)" in play_runtime_h and "audio_.set_all_paused(paused)" in play_runtime,
    "0.8.7 live editor managed mirror": "Editor Play Mode now hosts Vespera.NET" in managed_build_helper and "EditorReloadable" in managed_build_helper and "Editor Play mirror committed" in managed_build_helper,
    "0.8.7 editor project input settings": 'SeparatorText("Input Actions")' in editor and "+ Gamepad Axis" in editor,
    "0.8.8 first-class material asset": "vespera_material 1" in material_asset and "base_texture_asset" in material_asset and "BuiltinMaterialShader" in material_h,
    "0.8.8 asset catalog material kind": "AssetKind::Material" in asset_catalog and 'extension == ".slmat"' in asset_catalog and 'command == "base_texture_asset"' in asset_catalog,
    "0.8.8 scene material stable refs": 'command == "mesh_material_asset"' in scene_io and 'output << "  mesh_material_asset "' in scene_io,
    "0.8.8 prefab material stable refs": 'command == "mesh_material_asset"' in prefab and 'output << "mesh_material_asset "' in prefab,
    "0.8.8 material inspector workflow": 'Create Material Asset' in editor and 'Apply Material' in editor and 'Assign Material from Project' in editor,
    "0.8.8 managed material wrapper": "public AssetReference Material" in managed_api and "SetMaterial(string projectRelativePath)" in managed_api,
    "0.8.8 lit unlit shader path": "g_material_params" in renderer and "g_emission" in renderer and "BuiltinMaterialShader::Unlit" in renderer and "Num32BitValues = 48" in renderer,
    "0.8.8 material samples": (root / "examples/reference_game/assets/materials/concrete_lit.slmat").exists() and (root / "examples/reference_game/assets/materials/neon_unlit.slmat").exists() and 'materials/concrete_lit.slmat' in project_asset,
    "0.8.9 localhost automation server": "INADDR_LOOPBACK" in automation_server and "poll()" in automation_server and "AutomationIncomingRequest" in (root / "editor/automation_server.hpp").read_text(),
    "0.8.9 VAP protocol": "parse_automation_request" in automation_protocol and "automation_percent_encode" in automation_protocol and "encode_automation_response" in automation_protocol,
    "0.8.9 typed automation tools": "vespera_create_primitive" in automation_tools and "vespera_set_transform" in automation_tools and "vespera_save_scene" in automation_tools and "kAutomationTools" in automation_tools,
    "0.8.9 MCP stdio bridge": 'method == "initialize"' in mcp_bridge and 'method == "tools/list"' in mcp_bridge and 'method == "tools/call"' in mcp_bridge and "socket.create_connection" in mcp_bridge,
    "0.8.9 main thread automation dispatch": "poll_automation_server(state)" in editor and "dispatch_automation_request" in editor and "Automation / MCP" in editor,
    "0.8.9 automation launch helpers": "-Automation" in (root / "run.ps1").read_text() and (root / "tools/run-mcp.ps1").exists(),
    "0.8.9 automation docs": (root / "docs/AUTOMATION_MCP.md").exists() and "main-thread" in (root / "docs/AUTOMATION_MCP.md").read_text(),
    "0.9.1 selectable console without hover nag": "ConsoleSelectableText" in editor and "ImGuiInputTextFlags_ReadOnly" in editor and "Copy Visible" in editor and "Copy All" in editor and "Read-only Console text. Drag to select any portion" not in editor,
    "0.8.10 extension API foundation": "kEditorExtensionApiVersion = 1" in extension_api and "EditorExtensionRegistry" in extension_api and "register_core_extension_capabilities" in editor and 'BeginMenu("Extensions")' in editor,
    "0.8.10 native UI foundation": "class UiDocument" in ui_h and "UiNodeType::Canvas" in ui_source and "resolve_ui_layout" in ui_source and "ui_hit_test" in ui_source and "src/ui/ui.cpp" in engine_cmake,
    "0.9.3 expanded automation tools": "vespera_add_component" in automation_tools and "vespera_assign_material" in automation_tools and "vespera_build_csharp" in automation_tools and "vespera_export_project" in automation_tools and "vespera_create_ui_document" in automation_tools and "vespera_get_ui_layout" in automation_tools and "std::array<AutomationToolDescriptor, 60>" in automation_tools,
    "0.8.10 component automation dispatch": 'request.command == "vespera_add_component"' in editor and 'request.command == "vespera_set_component_property"' in editor and "get_builtin_component_property" in editor,
    "0.8.10 asset prefab material automation": 'request.command == "vespera_list_assets"' in editor and 'request.command == "vespera_instantiate_prefab"' in editor and 'request.command == "vespera_create_material"' in editor,
    "0.9.8 MCP runtime-QA schemas": 'SERVER_VERSION = "1.0.0"' in mcp_bridge and '"name":"vespera_get_operation"' in mcp_bridge and '"name":"vespera_get_entity"' in mcp_bridge and '"name":"vespera_open_scene"' in mcp_bridge and '"name":"vespera_create_ui_document"' in mcp_bridge and '"name":"vespera_get_ui_layout"' in mcp_bridge and '"name":"vespera_export_project"' in mcp_bridge,
    "0.9.8 MCP exact tool count": mcp_bridge.count('{"name":"vespera_') == 71,
    "0.8.10 UI and extension docs": (root / "docs/RUNTIME_UI.md").exists() and (root / "docs/EXTENSIONS.md").exists(),
    "0.9.0 deterministic package core": "export_project_package" in project_package and "Vespera.PackageManifest.txt" in project_package and "Vespera.BuildAssetIndex.txt" in project_package,
    "0.9.0 packager CLI": "--project" in packager_cli and "--output" in packager_cli and "export_project_package" in packager_cli,
    "0.9.8 export script": "Vespera Engine 1.0.0 export" in export_script and 'ValidateSet("Debug", "Development", "Release")' in export_script and 'ValidateSet("Project","FrameworkDependent","Portable")' in export_script.replace(" ", "") and "vespera_packager.exe" in export_script and "[switch]$Launch" in export_script,
    "0.9.0 editor export": "project.export" in (root / "editor/editor_command.hpp").read_text(encoding="utf-8") and "export_current_project_from_editor" in editor,
    "0.9.0 exporting docs": (root / "docs/EXPORTING.md").exists(),
    "0.9.1 shipping metadata compatibility": 'command == "managed_deployment"' in project_source and "candidate.format_version < 6" in project_source and "ManagedDeploymentMode" in project_source and 'company_name "Timingplanet"' in project_asset and 'managed_deployment "framework-dependent"' in project_asset,
    "0.9.1 portable managed package": "ManagedDeploymentMode::Portable" in project_package and "host/fxr" in project_package and "Microsoft.NETCore.App" in project_package and "dotnet_runtime_version" in project_package and "vespera_package_manifest 3" in project_package,
    "0.9.1 local bundled hostfxr preference": '"dotnet" / "host" / "fxr"' in managed_host or '"dotnet/host/fxr"' in managed_host,
    "0.9.4 project v7 build settings": 'output << "vespera_project 10\\n"' in project_source and 'command == "executable_name"' in project_source and 'command == "build_output_directory"' in project_source and 'command == "game_icon_asset"' in project_source and 'command == "development_diagnostics"' in project_source and 'executable_name "VesperaReferenceGame"' in project_asset and 'development_diagnostics 1' in project_asset,
    "0.9.4 configuration mapping": 'configuration == "Development"' in project_package and 'return "RelWithDebInfo"' in project_package and 'configuration == "Debug"' in project_package and 'configuration == "Release"' in project_package,
    "0.9.4 package report and manifest v3": "Vespera.PackageReport.txt" in project_package and "vespera_package_manifest 3" in project_package and "asset_bytes" in project_package and "payload_bytes" in project_package and "packaged_runtime_filename" in project_package,
    "0.9.4 game icon build safety": "project.game_icon_reference()" in build_manifest and "asset is the project game icon" in asset_authoring and "project.game_icon = game_icon.record->relative_path" in asset_authoring,
    "0.9.4 editor Build Settings": 'InputText("Executable Name"' in editor and 'InputText("Build Output Directory"' in editor and 'Use as Project Game Icon' in editor and 'Development Diagnostics / Symbols' in editor and 'Build Game...' in editor and 'Build & Run' in editor,
    "0.9.5d automation descriptor count": "std::array<AutomationToolDescriptor, 60>" in (root / "editor/automation_tools.hpp").read_text(),
    "0.9.5 managed source guard": "automation_managed_source_path" in editor and "managed source path must end in .cs" in editor and "automation_guarded_replace_text" in editor,
    "0.9.5 entity QA surface": "vespera_set_entity_metadata" in editor and "vespera_reorder_entity" in editor and "vespera_attach_csharp_script" in editor,
    "0.9.5 integrity and scenarios": "vespera_check_project_integrity" in editor and "scenario == \"hierarchy\"" in editor and "scenario == \"play_cycle\"" in editor and "scenario == \"ui_layout\"" in editor,
    "0.9.5 QA runner": (root / "tools/vespera_qa_runner.py").exists() and (root / "tools/run-qa.ps1").exists(),
    "0.9.5a Windows fallback repair handle release": asset_authoring.count("input.close(); // Windows: release the descriptor before atomic rename/replace.") == 2,
    "0.9.5a fresh managed diagnostics": "Vespera.ManagedBuildDiagnostics.{}.txt" in editor and "managed_build_sequence" in editor and "std::filesystem::remove(diagnostics" in editor,
    "0.9.5a MCP command response timeouts": '"vespera_build_csharp": 180.0' in mcp_bridge and "sock.settimeout(response_timeout)" in mcp_bridge and "timed out after" in mcp_bridge,
    "0.9.5b loaded-scene fallback synchronization": "hydrate_scene_asset_references(state)" in editor and "before scene save" in editor,
    "0.9.5b asset move-save regression": "scenario == \"asset_move_save_reopen\"" in editor and "AssetMoveSave" in (root / "tools/run-qa.ps1").read_text() and "--asset-move-save" in (root / "tools/vespera_qa_runner.py").read_text(),
    "0.9.5b async MCP managed builds": "class OperationRegistry" in mcp_bridge and "vespera_get_operation" in mcp_bridge and "wait_for_completion" in mcp_bridge and "poll_with" in mcp_bridge,
    "0.9.5c MCP managed-build regression runner": "McpManagedBuild" in (root / "tools/run-qa.ps1").read_text() and "run_mcp_managed_build" in (root / "tools/vespera_qa_runner.py").read_text() and "--mcp-managed-build" in (root / "tools/vespera_qa_runner.py").read_text(),
    "0.9.5c direct QA response timeouts": '"vespera_run_qa_scenario": 300.0' in (root / "tools/vespera_qa_runner.py").read_text() and "sock.settimeout(response_timeout)" in (root / "tools/vespera_qa_runner.py").read_text() and 'steps.append(("integrity-after"' in (root / "tools/vespera_qa_runner.py").read_text(),
    "0.9.5d semantic input injection": "host_set_action_override" in (root / "engine/src/input/input.cpp").read_text() and "previous_action_overrides_" in (root / "engine/include/vespera/input/input.hpp").read_text(),
    "0.9.5d runtime TextInput path": "UiTextInputState" in ui_render and "SDL_EVENT_TEXT_INPUT" in (root / "engine/src/core/application.cpp").read_text() and 'node 7 2 "text_input" "QA Text Input"' in (root / "examples/reference_game/assets/ui/reference_hud.slui").read_text(),
    "0.9.5d structured managed runtime events": "ManagedRuntimeEvent" in managed_host and "trace_runtime_event" in managed_host and "TraceRuntimeEvent" in managed_api and "runtime_events_since" in managed_host,
    "0.9.5d lifecycle telemetry survives reload": "Runtime QA telemetry intentionally survives managed shutdown/reinitialize" in managed_host,
    "0.9.5d same-scene reload guard": "use Scene.Reload() for an intentional reload" in reference and "rapid same-scene reload loop" in reference and '@vespera/reload-current' in managed_api,
    "0.9.5d Editor Play C# hot reload": 'request.command == "vespera_build_csharp"' in editor and "Build C# is disabled during Play Mode" not in editor and "managed_config_.auto_reload = true" in (root / "editor/play_runtime.cpp").read_text(),
    "0.9.5d editor runtime QA surface": all(name in editor for name in ["vespera_inject_input_action","vespera_pointer_event","vespera_ui_navigation","vespera_text_input","vespera_get_ui_runtime_state","vespera_get_runtime_events","vespera_clear_runtime_events"]),
    "0.9.5d standalone runtime automation": "Runtime automation server listening on 127.0.0.1" in reference and "vespera_runtime_get_state" in reference and "kRuntimeAutomationPort = 46788" in reference and "../../editor/automation_server.cpp" in (root / "examples/reference_game/CMakeLists.txt").read_text(),
    "0.9.5d bridge stale-registration guard": '"name":"vespera_get_bridge_info"' in mcp_bridge and 'DEFAULT_RUNTIME_PORT = 46788' in mcp_bridge and 'name.startswith("vespera_runtime_")' in mcp_bridge,
    "0.9.5d exported runtime QA runner": "RuntimeTelemetry" in (root / "tools/run-qa.ps1").read_text() and "RuntimeExportAutomation" in (root / "tools/run-qa.ps1").read_text() and "run_exported_runtime_automation" in (root / "tools/vespera_qa_runner.py").read_text(),
    "0.9.5d explicit sprite clip export root": 'build_include_asset "07400000000000000000000000000001" "animations/watcher_walk.slspriteclip"' in project_asset,
    "0.9.5d build-root MCP authoring": "vespera_set_build_include" in editor and '"name":"vespera_set_build_include"' in mcp_bridge,
    "0.9.8 public version": 'kEngineVersion = "@VESPERA_VERSION_LABEL@"' in version_template and 'product_version "1.0.0"' in project_asset,
    "0.9.5a asset repair warning messages": '"warning_messages"' in editor,
    "1.0 public contributor guidance": (root / "CONTRIBUTING.md").exists() and (root / "docs/TESTING.md").exists() and (root / "docs/RELEASE_VALIDATION.md").exists(),
    "0.9.5a managed recovery regression": "scenario == \"managed_build_recovery\"" in editor and "--managed-recovery" in (root / "tools/vespera_qa_runner.py").read_text() and "ManagedRecovery" in (root / "tools/run-qa.ps1").read_text(),
    "0.9.5 MCP shipping + torture surface": 'configuration must be Debug, Development, or Release' in editor and 'packaged.package_report' in editor and '"enum":["Debug","Development","Release"]' in mcp_bridge and '"launch":{"type":"boolean"' in mcp_bridge and 'vespera_check_project_integrity' in mcp_bridge and 'vespera_run_qa_scenario' in mcp_bridge,
    "0.9.6 UI document serialization": "kUiFormatVersion = 3" in ui_io and "save_ui_document" in ui_io and "load_ui_document" in ui_io and "version < 1" in ui_io and "version > kUiFormatVersion" in ui_io and "src/ui/ui_io.cpp" in engine_cmake,
    "0.9.1 UI asset catalog": "AssetKind::UiDocument" in asset_catalog and 'extension == ".slui"' in asset_catalog and 'command == "image_asset"' in asset_catalog and 'command == "font_asset"' in asset_catalog,
    "0.9.1 reference UI dogfood": (root / "examples/reference_game/assets/ui/reference_hud.slui").exists() and (root / "examples/reference_game/assets/ui/reference_hud.slui.vmeta").exists() and 'ui/reference_hud.slui' in project_asset and "Runtime UI document:" in reference,
    "0.9.1 editor UI asset inspection": "AssetKind::UiDocument" in editor and "UI Document" in editor and "resolve_ui_layout" in editor,
    "0.9.1 automation asset authoring": 'request.command == "vespera_move_asset"' in editor and 'request.command == "vespera_check_asset_delete"' in editor and 'request.command == "vespera_repair_asset_fallbacks"' in editor,
    "0.9.1 automation UI authoring": 'request.command == "vespera_create_ui_document"' in editor and 'request.command == "vespera_get_ui_document"' in editor and 'request.command == "vespera_add_ui_node"' in editor and 'request.command == "vespera_set_ui_node"' in editor,
    "0.9.1 project settings automation": 'request.command == "vespera_get_project_settings"' in editor and 'request.command == "vespera_set_project_setting"' in editor,
    "0.9.2 runtime UI packet": "struct UiRenderPacket" in ui_render_h and "UiRenderCache::build_packet" in ui_render and "UiDrawVertex" in ui_render_h and "src/ui/ui_render.cpp" in engine_cmake,
    "0.9.2 D3D12 native UI renderer": "void render_ui(" in renderer and "create_ui_pipeline" in renderer and "Runtime UI atlas uploaded" in renderer and "g_ui_atlas" in renderer,
    "0.9.2 standalone runtime UI": "runtime_ui_cache_.build_packet" in reference and "renderer.render_ui(packet" in reference and "Runtime UI document:" in reference,
    "0.9.2 editor Play UI": "play_ui_cache.build_packet" in editor and "render_ui(ui_packet" in editor and "Play Mode UI: loaded" in editor,
    "0.9.2 managed UI ABI": "UiFindNodeCallback" in managed_host and "ui_find_node = managed_ui_find_node" in managed_host and "public static class UI" in managed_api and "public sealed class UiElement" in managed_api,
    "0.9.8 pinned RmlUi production dependencies": "VESPERA_RMLUI_UI" in root_cmake and "VESPERA_RMLUI_EVALUATION" in root_cmake and "RMLUI_FONT_ENGINE" in root_cmake and "VER-2-14-1.tar.gz" in root_cmake and "RmlUi/archive/refs/tags/6.2.tar.gz" in root_cmake and "RMLUI_SAMPLES OFF" in root_cmake,
    "0.9.8 RmlUi engine adapter linked": "src/ui/rmlui_surface.cpp" in engine_cmake and "RmlUi::RmlUi" in engine_cmake and "VESPERA_HAS_RMLUI" in engine_cmake,
    "0.9.8 RmlUi Vespera-owned public surface": "class RmlUiSurface" in rmlui_h and "UiRenderPacket build_packet" in rmlui_h and "process_input" in rmlui_h and "set_text" in rmlui_h and "set_property" in rmlui_h and "set_value" in rmlui_h and "set_class" in rmlui_h and "set_disabled" in rmlui_h and "reload" in rmlui_h and "consume_clicks" in rmlui_h,
    "0.9.8 RmlUi compatibility packet renderer": "Rml::RenderInterfaceCompatibility" in rmlui_source and "GetAdaptedInterface" in rmlui_source and "rebuild_atlas_if_needed" in rmlui_source and "clip_triangle" in rmlui_source and "import_texture" in rmlui_source and "kMaxAtlasDimension = 8192" in rmlui_source,
    "0.9.8 RmlUi input and typography integration": "Rml::LoadFontFace" in rmlui_source and "ProcessMouseMove" in rmlui_source and "ProcessMouseButtonDown" in rmlui_source and "ProcessTextInput" in rmlui_source and "ProcessKeyDown" in rmlui_source,
    "0.9.8 isolated RmlUi showcase": "vespera_rmlui_spike" in rmlui_sample_cmake and "guild_showcase.rml" in rmlui_sample and "start-mission" in rmlui_sample and "guild-name" in rmlui_document and "display: flex" in rmlui_document and ":hover" in rmlui_document and ":focus" in rmlui_document,
    "0.9.8 RmlUi launch helper": "vespera_rmlui_spike.exe" in rmlui_runner and "Launching Vespera RmlUi showcase" in rmlui_runner,
    "0.9.8 RmlUi production documentation": "production UI foundation" in rmlui_doc and ".slui" in rmlui_doc and "RmlUi does not own" in rmlui_doc and "Current limitations" in rmlui_doc,
    "0.9.6 UI v3 surface model": "struct UiSurfaceProperties" in ui_h and "UiImageFit" in ui_h and "nine_slice" in ui_h and "font_family" in ui_h and "font_weight" in ui_h and 'out << "surface "' in ui_io and 'out << "text_style "' in ui_io,
    "0.9.6 modern native UI rendering": "append_rounded_rect" in ui_render and "append_shadow" in ui_render and "UiImageFit::Contain" in ui_render and "UiImageFit::Cover" in ui_render and "nine_slice" in ui_render and "decode_utf8" in ui_render,
    "0.9.6 Windows font rasterization": "AddFontResourceExW" in ui_render and "CreateFontW" in ui_render and "UiFontResolver" in ui_render_h and "gdi32" in engine_cmake,
    "0.9.6a Win32 GDI metric type disambiguation": "std::max<int>(8, static_cast<int>(size.cx)" in ui_render and "std::max<int>(8, static_cast<int>(tm.tmHeight)" in ui_render,
    "0.9.6 antialiased UI sampler": "D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT" in renderer,
    "0.9.6 UI authoring style controls": 'DragFloat("Corner Radius"' in editor and 'InputText("Font Family"' in editor and 'Combo("Image Fit"' in editor and 'InputFloat4("9-Slice LTRB"' in editor,
    "0.9.6 UI MCP styling": '"font_family"' in mcp_bridge and '"corner_radius"' in mcp_bridge and '"nine_slice"' in mcp_bridge and 'request.command == "vespera_set_ui_node"' in editor,
    "0.9.6 MCP UI batch": '"name":"vespera_apply_ui_batch"' in mcp_bridge and "operations_completed" in mcp_bridge and "aliases" in mcp_bridge,
    "0.9.6 reference UI v3": 'vespera_ui 3' in (root / "examples/reference_game/assets/ui/reference_hud.slui").read_text() and 'surface ' in (root / "examples/reference_game/assets/ui/reference_hud.slui").read_text() and 'text_style ' in (root / "examples/reference_game/assets/ui/reference_hud.slui").read_text(),
    "0.9.3 richer UI model": "UiNodeType::ProgressBar" in ui_source and "UiNodeType::ScrollView" in ui_source and "UiNodeType::TextInput" in ui_source and "UiLayoutMode::Grid" in ui_source and "clip_rect" in ui_h,
    "0.9.3 UI runtime interaction": "struct UiRuntimeState" in ui_h and "primary_released" in ui_render_h and "focus_next" in ui_render_h and "consume_click" in ui_source,
    "0.9.3 UI authoring window": "struct UiAuthoringState" in editor and "Open UI Authoring" in editor and "draw_ui_authoring_preview" in editor and 'Button("Top Left")' in editor and '"1920 x 1080"' in editor,
    "0.9.3 managed UI ABI v8 additions": "UiGetNodeBoolCallback" in managed_host and "ui_set_node_float = managed_ui_set_node_float" in managed_host and "UiConsumeClick" in managed_api and "public float Value" in managed_api and "public bool Clicked" in managed_api and "Gameplay API: runtime UI control active" in gameplay_dogfood,
    "0.9.3a safe managed UI float bridge": "float native = 0.0f;" in managed_api and "UiGetNodeFloat(_api.UserData, id, property, &native)" in managed_api and "Managed compiler diagnostics:" in managed_build_helper,
    "0.9.3b Game-view UI-first pointer routing": "ui_interactable_under_pointer" in editor and "Runtime UI is clickable | click empty Game space to capture FPS input" in editor and "state.game_view.hovered && !state.game_view.input_captured" in editor and "state.game_view.focused && !state.game_view.input_captured" in editor,
    "0.9.3 UI QA automation": 'request.command == "vespera_delete_ui_node"' in editor and 'request.command == "vespera_validate_ui_document"' in editor and 'request.command == "vespera_get_ui_layout"' in editor,
    "0.9.8 RML asset catalog": "AssetKind::RmlDocument" in asset_catalog and "AssetKind::RmlStyleSheet" in asset_catalog and 'extension == ".rml"' in asset_catalog and 'extension == ".rcss"' in asset_catalog and "scan_rml_asset_path_references" in asset_catalog,
    "0.9.8 project template API": "ProjectTemplateKind" in project_templates_h and "create_project_from_template" in project_templates_h and "template root cannot be empty" in project_templates_source and "Template.vesperaproject" in project_templates_source,
    "0.9.8 project hub": "ProjectHubGame" in project_hub and "template-2d" in project_hub_rml and "template-3d" in project_hub_rml and "template-empty" in project_hub_rml and "vespera_project_hub" in project_hub_cmake,
    "0.9.8 project launch flow": "run-hub.ps1" in main_launcher and "vespera_hub_result.txt" in run_hub and "run-editor.ps1" in run_hub and "[string]$Project" in (root / "run.ps1").read_text(),
    "0.9.8 clean templates": all((root / path).exists() for path in ["templates/2d/Template.vesperaproject","templates/3d/Template.vesperaproject","templates/empty/Template.vesperaproject"]) and "startup_ui_asset" in (root / "templates/2d/Template.vesperaproject").read_text() and "startup_ui_asset" in (root / "templates/3d/Template.vesperaproject").read_text(),
})

# Several historical milestone checks are appended after the legacy failure gate
# and some intentionally match older source wording. Enforce every check for the
# *current* checkpoint here without turning stale historical text probes into new
# blockers for this package.
checks.update({
    "0.9.8a Windows known-folder Project Hub path": "SHGetKnownFolderPath" in project_hub and "FOLDERID_Documents" in project_hub and "KF_FLAG_CREATE" in project_hub,
    "0.9.8a key repeat input bridge": "repeat_count(Key key)" in input_h and "key_repeat_counts_" in input_h and "event.key.repeat" in (root / "engine/src/core/application.cpp").read_text() and "input.repeat_count(key)" in rmlui_source,
    "0.9.8a RmlUi border-box baseline": "box-sizing: border-box" in project_hub_rml and "box-sizing: border-box" in rmlui_document,
    "0.9.8a compact Project Hub actions": "align-items: flex-start" in project_hub_rml and ".primary" in project_hub_rml and ".secondary" in project_hub_rml,
    "0.9.8b behavioral test foundation": "VESPERA_TESTS_ONLY" in root_cmake and "add_subdirectory(tests)" in root_cmake and "doctest::doctest" in (root / "tests/CMakeLists.txt").read_text() and (root / "vendor-mini/doctest/doctest.h").is_file() and "vespera_engine_logic_tests" in (root / "tests/CMakeLists.txt").read_text() and "repeat_count(Key::Backspace)" in (root / "tests/engine_logic_tests.cpp").read_text() and "Project format round-trips" in (root / "tests/engine_logic_tests.cpp").read_text() and "VESPERA_TESTS_ONLY=ON" in (root / "test.ps1").read_text(),
    "0.9.9a RML reference module": "src/assets/rml_asset_references.cpp" in engine_cmake and "scan_rml_asset_path_references" in rml_asset_references and "rewrite_rml_asset_path_references" in rml_asset_references,
    "0.9.9a controlled RML move repair": "expected_rml_rewrites" in asset_authoring and "rml_references_rewritten" in asset_authoring and "RML/RCSS path reference(s)" in asset_authoring and "rml_references_rewritten" in editor,
    "0.9.9a RML move behavior tests": "Controlled asset move rewrites RML and RCSS path dependents" in (root / "tests/engine_logic_tests.cpp").read_text() and "Controlled RML source move rebases" in (root / "tests/engine_logic_tests.cpp").read_text() and "data-src" in (root / "tests/engine_logic_tests.cpp").read_text(),
    "0.9.9b engine-owned UI surface": "class UiSurface" in ui_surface_h and "class LegacyUiSurface final : public UiSurface" in ui_surface_h and "src/ui/ui_surface.cpp" in engine_cmake and "LegacyUiSurface::set_visible" in ui_surface_source and "class RmlUiSurface final : public UiSurface" in rmlui_h,
    "0.9.9b UI surface behavior tests": "Legacy UI surface exposes named semantic element operations" in (root / "tests/engine_logic_tests.cpp").read_text() and "Legacy UI surface visibility clears stale interaction state" in (root / "tests/engine_logic_tests.cpp").read_text() and "std::is_base_of_v<UiSurface, RmlUiSurface>" in (root / "tests/engine_logic_tests.cpp").read_text(),
    "0.9.9b1 compact RmlUi actions/focus": ".recent:focus" in project_hub_rml and ".template:focus" in project_hub_rml and "display: inline-block" in rmlui_document,
    "0.9.9d2 Windows live-resize redraw": "application_live_resize_event_watch" in application_source and "SDL_EVENT_WINDOW_EXPOSED" in application_source and "SDL_AddEventWatch" in application_source and "render_application_frame" in application_source,
    "0.9.9d2 fixed desktop shell": "width: 1070px; max-width: 100%" in project_hub_rml and "#recent-panel { width: 300px; flex-grow: 0; flex-shrink: 1" in project_hub_rml and "#new-panel { width: 680px; max-width: 100%; flex-grow: 0; flex-shrink: 1" in project_hub_rml and "width: 1050px;" in rmlui_document and "#roster-panel" in rmlui_document and "width: 280px;" in rmlui_document and "width: 430px;" in rmlui_document and "width: 270px;" in rmlui_document,
    "0.9.9d3 compact non-wrapping guild controls": ".primary { display: inline-block; width: auto;" in project_hub_rml and ".secondary { display: inline-block; width: auto;" in project_hub_rml and ".control-row" in rmlui_document and ".member { width: 220px; }" in rmlui_document and ".mission { width: 280px; }" in rmlui_document and "width: 150px;" in rmlui_document and "#upgrade { width: 220px; }" in rmlui_document and "white-space: nowrap;" in rmlui_document and ".member, .mission" in rmlui_document and "width: auto;" not in rmlui_document.split(".member, .mission",1)[1].split(".member:hover",1)[0],
    "0.9.9c UI handle behavior tests": "UI handle table keeps script handles backend-neutral and revalidates existence" in (root / "tests/engine_logic_tests.cpp").read_text() and "Legacy UI surface preserves managed semantic properties through the shared facade" in (root / "tests/engine_logic_tests.cpp").read_text(),
    "0.9.9c managed UI dogfood": "UiSurface bridge active" in gameplay_dogfood and "_uiProgress.ValueText = \"0.35\"" in gameplay_dogfood,    "0.9.9d1 shared player target": "add_subdirectory(runtime/player)" in root_cmake and "add_executable(vespera_player" in player_cmake and "VesperaPlayerGame" in player_main and "discover_player_project" in player_main,
    "0.9.9d1 generic export fallback": '$GameTarget = "vespera_player"' in export_script and '$UsesSharedPlayer = -not $GameTarget' in export_script and "-NoMirrors" in export_script and "Runtime model: shared vespera_player" in export_script,
    "0.15 export default warning": "No -Project was supplied" in export_script and "$ProjectWasExplicit" in export_script,
    "0.9.9d1 project managed export isolation": "[switch]$NoMirrors" in managed_build_helper and '$EditorManagedDir = ""' in managed_build_helper and '$RuntimeManagedDir = ""' in managed_build_helper,
    "0.9.9d1 source project runner": "vespera_player.exe" in run_project and "--project" in run_project and "-NoMirrors" in run_project,
    "0.9.9d1 player discovery policy": "exactly one .vesperaproject" in player_project_h and "multiple .vesperaproject files" in player_project_source and "legacy project has no startup_ui" in player_project_source,
    "0.9.9d1 player behavior tests": "Vespera player honors an explicit project path" in (root / "tests/engine_logic_tests.cpp").read_text() and "Packaged Vespera player discovers exactly one sibling project" in (root / "tests/engine_logic_tests.cpp").read_text() and "Packaged Vespera player rejects ambiguous sibling projects" in (root / "tests/engine_logic_tests.cpp").read_text() and "Vespera player explicit startup UI wins over unrelated build roots" in (root / "tests/engine_logic_tests.cpp").read_text() and "Vespera player keeps legacy first-RML compatibility when startup UI is absent" in (root / "tests/engine_logic_tests.cpp").read_text(),
    "0.9.9e starter managed projects": all('managed_project "managed/VesperaGame.Scripts.csproj"' in (root / f"templates/{folder}/Template.vesperaproject").read_text() and 'managed_assembly "VesperaGame.Scripts"' in (root / f"templates/{folder}/Template.vesperaproject").read_text() and 'game_target ' not in (root / f"templates/{folder}/Template.vesperaproject").read_text() for folder in ("2d","3d","empty")),
    "0.9.9e external starter SDK bridge": "VesperaSdkProject" in managed_build_helper and "VesperaSdkProject" in (root / "templates/3d/managed/VesperaGame.Scripts.csproj").read_text() and "VesperaSdkProject" in (root / "templates/2d/managed/VesperaGame.Scripts.csproj").read_text(),
    "0.9.9e editor selected-project managed bootstrap": "Building project C# scripts before editor launch" in (root / "run.ps1").read_text() and "editor-projects" in (root / "run.ps1").read_text() and "-NoMirrors" in (root / "run.ps1").read_text(),
    "0.9.9e starter scene attachments": 'managed_script "VesperaGame.StarterController" 1' in (root / "templates/3d/assets/scenes/main.slscene").read_text() and 'managed_script "VesperaGame.StarterGame" 1' in (root / "templates/2d/assets/scenes/main.slscene").read_text() and 'managed_script "VesperaGame.Game" 1' in (root / "templates/empty/assets/scenes/main.slscene").read_text(),
    "0.9.9e starter behavior tests": "Shipped starter templates own managed projects" in (root / "tests/engine_logic_tests.cpp").read_text() and "Hub-created starter keeps project-owned managed source intact" in (root / "tests/engine_logic_tests.cpp").read_text() and "VESPERA_SOURCE_DIR" in (root / "tests/CMakeLists.txt").read_text(),
    "0.9.9f project v8 startup UI": 'output << "vespera_project 10\\n"' in project_source and 'command == "startup_ui_asset"' in project_source and "startup_ui_reference" in project_source and "startup_ui_reference" in (root / "engine/src/assets/build_manifest.cpp").read_text(),
    "0.9.9f startup UI move/delete safety": "asset is the project startup UI" in asset_authoring and "project.startup_ui = startup_ui.record->relative_path" in asset_authoring and "Project startup UI stable reference repairs after a controlled RML move" in (root / "tests/engine_logic_tests.cpp").read_text(),
    "0.9.9f starter startup UI roots": all('vespera_project 10' in (root / f"templates/{folder}/Template.vesperaproject").read_text() for folder in ("2d","3d","empty")) and all('startup_ui_asset' in (root / f"templates/{folder}/Template.vesperaproject").read_text() for folder in ("2d","3d")) and 'startup_ui_asset' not in (root / "templates/empty/Template.vesperaproject").read_text(),
    "0.9.9f project authoring startup UI": "startup_ui_asset_id" in editor and 'else if (*key == "startup_ui")' in editor and '"startup_ui"' in mcp_bridge and 'ImGui::InputText("Startup RML UI"' in editor and 'ImGui::Button("Use Selected RML")' in editor,
    "0.9.9 editor project title": 'return std::format("{} - Vespera Editor {}", project_name, vespera::kEngineVersion);' in editor and 'state.project.name.empty()' in editor,
    "0.9.9 in-editor build UX": 'ImGui::Begin("Build Game"' in editor and 'start_editor_build_job' in editor and 'Build & Run Development' in editor and 'Open Output Folder' in editor and 'Ctrl+Shift+B' in editor and 'CREATE_NO_WINDOW' in editor,
    "0.9.9 background build log": 'std::future<int> future' in editor and '"build" / "editor-logs"' in editor and 'Recent Build Output' in editor and 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File' in editor,
    "0.9.9 targeted export build": '--target $GameTarget vespera_packager' in export_script and 'Building required native export targets' in export_script and 'CMakeCache.txt' in export_script,
    "0.9.9 final shared branding assets": all((root / f"branding/{name}").is_file() for name in ("vespera_icon.png", "vespera_icon.bmp", "vespera_icon.ico", "vespera_splash.png", "vespera_logo_sting.wav", "vespera.rc.in")),
    "0.9.9 final application startup branding": "startup_splash_image" in application_source and "startup_sound" in application_source and "apply_window_icon" in application_source and "make_splash_packet" in application_source,
    "0.9.9 final player branding": 'config.startup_splash_image = executable_root / "branding/vespera_splash.png"' in player_main and 'config.startup_sound = executable_root / "branding/vespera_logo_sting.wav"' in player_main and 'config.icon_path' in player_main,
    "0.9.9 final packaged branding": 'for (const char* directory : {"branding", "legal"})' in project_package and 'shared vespera_player runtime is missing its staged' in project_package,
    "0.10.0a Editor Play startup RML selection": "select_runtime_rml_document(state.project, state.asset_catalog)" in editor and "Play Mode UI: RmlUi -" in editor and "play_rml_ui_loaded" in editor,
    "0.10.0a Editor Play RmlUi render": "state.play_rml_ui->resize" in editor and "state.play_rml_ui->build_packet()" in editor and "context.render_backend->render_ui(ui_packet" in editor,
    "0.10.0a embedded RmlUi managed seam": "vespera::RmlUiSurface* rml_ui_surface" in play_runtime_h and "ui_surface_ = rml_ui_surface_" in play_runtime and "managed_.initialize(scene, input_, audio_, *assets_, managed_config_, ui_surface_)" in play_runtime,
    "0.10.0a embedded RmlUi input": "runtime.set_mouse_position" in editor and "runtime.set_mouse_button" in editor and "runtime.add_text_input" in editor and "rml_ui_surface_->process_input(input_)" in play_runtime,
    "0.10.0a legacy Play fallback retained": "LegacyUiSurface" in play_runtime_h and "legacy .slui" in editor and "if (!state.play_rml_ui_loaded)" in editor,
    "0.10.0a semantic runtime automation": "inject_rml_pointer" in play_runtime_h and "inject_rml_navigation" in play_runtime_h and "inject_rml_text" in play_runtime_h and '\\\"backend\\\":\\\"rmlui\\\"' in editor and '\\\"backend\\\":\\\"slui\\\"' in editor,
    "0.10.0b Lua dependency": "VESPERA_LUA_SCRIPTING" in root_cmake and "lua-5.4.9.tar.gz" in root_cmake and "vespera_lua" in root_cmake and "src/scripting/lua_script_host.cpp" in engine_cmake,
    "0.10.0b Lua first-class asset": 'AssetKind::LuaScript' in asset_catalog and '"vespera.script.lua"' in asset_catalog and 'extension == ".lua"' in asset_catalog,
    "0.10.0b Project v9 Lua entry": 'output << "vespera_project 10\\n"' in project_source and 'command == "lua_entry_asset"' in project_source and "lua_entry_reference" in project_source and 'lua_entry_asset' in project_asset,
    "0.10.0b Lua build/move/delete safety": "project.lua_entry_reference()" in build_manifest and "asset is the project Lua entry script" in asset_authoring and "project.lua_entry = lua_entry.record->relative_path" in asset_authoring,
    "0.10.0b Lua safe host": "class LuaScriptHost" in lua_host_h and "Start()" in lua_host_h and "Update(delta_seconds)" in lua_host_h and 'lua_pushnil(state); lua_setglobal(state,"dofile")' in lua_host and 'lua_pushnil(state); lua_setglobal(state,"loadfile")' in lua_host and "get_builtin_component_property" in lua_host and "UiSurface*" in lua_host_h,
    "0.10.0b Lua player/editor integration": "initialize_lua(context)" in player_main and "lua_.update" in player_main and "initialize_lua" in play_runtime and "lua_.update" in play_runtime and 'else if (*key == "lua_entry")' in editor and '"lua_entry"' in mcp_bridge,
    "0.10.0b Lua reference dogfood": 'scripts/reference.lua' in project_asset and "initialize_lua(context)" in reference and "lua_host_.update" in reference and "lua_ready_" in reference and "exported runtime did not start Project v9 Lua entry" in (root / "tools/vespera_qa_runner.py").read_text() and "Vespera.Input.pressed" in (root / "examples/reference_game/assets/scripts/reference.lua").read_text(),
    "0.10.0b Lua behavior tests": "Lua scripts are first-class assets and project build roots" in (root / "tests/engine_logic_tests.cpp").read_text() and "Project Lua entry stable reference repairs after move and blocks delete" in (root / "tests/engine_logic_tests.cpp").read_text(),
    "0.10.0b Lua public docs": "Project entry script" in (root / "docs/LUA_SCRIPTING.md").read_text() and "Vespera.Input" in (root / "docs/LUA_SCRIPTING.md").read_text() and "C# remains" in (root / "docs/LUA_SCRIPTING.md").read_text(),
    "0.10.0c RML source editor module": "rml_source_editor.cpp" in editor_cmake and "RmlSourceEditorState" in (root / "editor/rml_source_editor.hpp").read_text() and "InputTextMultiline" in (root / "editor/rml_source_editor.cpp").read_text(),
    "0.10.0c RML asset diagnostics": "Open RML / RCSS Source" in editor and "dependencies_of(record->asset_id)" in editor and "Broken:" in editor,
    "0.10.0c active RML reload after source save": "draw_rml_source_authoring" in editor and "Play Mode RML reloaded after source save" in editor and "dependency->target_asset_id == state.rml_source_editor.asset_id" in editor,
    "0.10.0c legacy slui authoring labeled": "Legacy .slui compatibility/QA authoring. New project UI should use RML/RCSS." in editor,
    "0.10.0 final feature freeze": "0.10.0 is now **feature frozen**" in (root / "docs/ROADMAP.md").read_text() and "0.10.0 final — scripting/UI convergence freeze" in (root / "docs/CHANGELOG.md").read_text(),
})

checks.update({
    "0.11.0 runtime performance counters": "RuntimePerformanceCounters" in (root / "engine/include/vespera/core/game.hpp").read_text() and "record_frame" in (root / "engine/src/core/application.cpp").read_text(),
    "0.14.1 editor performance header ownership": "#include <vespera/core/game.hpp>" in editor and "vespera::RuntimePerformanceCounters performance;" in editor,
    "0.11.0 scene scale stats": "collect_scene_stats" in (root / "engine/include/vespera/scene/scene_stats.hpp").read_text() and "scene_stats" in editor and "scene_stats" in reference,
    "0.11.0 state performance telemetry": '\\"performance\\"' in editor and '\\"performance\\"' in reference and "asset_stats" in editor and "asset_stats" in reference,
    "0.11.0 optional stress QA": "run_scale_stress" in (root / "tools/vespera_qa_runner.py").read_text() and '--stress' in (root / "tools/vespera_qa_runner.py").read_text() and '$Stress' in (root / "tools/run-qa.ps1").read_text(),
    "0.11.0 telemetry behavior tests": "Scene stats report scale-relevant component counts" in (root / "tests/engine_logic_tests.cpp").read_text() and "Runtime performance counters smooth and retain worst frame" in (root / "tests/engine_logic_tests.cpp").read_text(),
})

checks.update({
    "0.13.0 external getting-started path": (root / "docs/GETTING_STARTED.md").exists() and (root / "docs/FIRST_GAME.md").exists() and (root / "docs/BUILD_AND_SHIP.md").exists() and "docs/GETTING_STARTED.md" in (root / "README.md").read_text(),
    "0.13.0 actionable Build Game preflight": "Preflight Details" in editor and "Missing build root:" in editor and "Broken asset reference:" in editor and "stale fallback path" in editor,
    "0.13.0 release icon advisory": "No project game icon is authored; Release will use the Vespera fallback icon." in editor,
})

checks.update({
    "0.14.0 Emberlight sample project": (root / "examples/emberlight_guild/EmberlightGuild.vesperaproject").exists() and (root / "examples/emberlight_guild/managed/GuildGame.cs").exists(),
    "0.14.0 Emberlight RML RCSS": (root / "examples/emberlight_guild/assets/ui/main.rml").exists() and (root / "examples/emberlight_guild/assets/ui/guild.rcss").exists() and "guild.rcss" in (root / "examples/emberlight_guild/assets/ui/main.rml").read_text(),
    "0.14.0 Emberlight dogfood behavior test": "Emberlight Guild sample is a self-contained shared-player project" in (root / "tests/engine_logic_tests.cpp").read_text(),
})

checks.update({
    "1.0.0 final version surface": "project(VesperaEngine VERSION 1.0.0" in root_cmake and 'set(VESPERA_VERSION_LABEL "1.0.0")' in root_cmake and 'SERVER_VERSION = "1.0.0"' in mcp_bridge and "Vespera Engine 1.0.0 export" in export_script,
    "0.15.1 entity lookup cache": "entity_lookup_cache_" in (root / "engine/include/vespera/scene/scene.hpp").read_text() and "Scene ID lookup cache survives vector reordering and destructive edits" in (root / "tests/engine_logic_tests.cpp").read_text(),
    "0.15.1 root transform fast path": "if (entity.parent_id == kInvalidSceneObjectId) return entity.transform;" in (root / "engine/src/scene/scene_hierarchy.cpp").read_text() and "World transform keeps root fast path and nested hierarchy semantics" in (root / "tests/engine_logic_tests.cpp").read_text(),
    "0.15.1 sprite frustum culling": "sprite_radius" in (root / "engine/src/render/d3d12/d3d12_renderer.cpp").read_text() and "frustum.intersects_sphere(world_transform.position, sprite_radius)" in (root / "engine/src/render/d3d12/d3d12_renderer.cpp").read_text(),
    "0.15.2 project v10 vsync": 'output << "vespera_project 10\\n"' in project_source and 'command == "vsync"' in project_source and 'vsync 0' in (root / "examples/performance_lab/VesperaPerformanceLab.vesperaproject").read_text(),
    "0.15.2 renderer telemetry": (root / "engine/include/vespera/render/render_stats.hpp").exists() and "frame_stats() const" in renderer and "GetPerformance" in managed_api and "Performance.TryGetSnapshot" in (root / "examples/performance_lab/managed/PerformanceLab.cs").read_text(),
    "0.15.2 transform batching": "managed_set_transforms" in managed_host and "SetTransforms" in managed_api and "Transform.SetBatch" in (root / "examples/performance_lab/managed/PerformanceLab.cs").read_text(),
    "0.15.2 primitive and sprite instancing": "PrimitiveInstanceData" in renderer and "primitive_instanced_pipeline_state_" in renderer and "sprite_instance_scratch_" in renderer and "DrawIndexedInstanced" in renderer,
    "0.15.2 managed attachment fast path": "attachments_match" in managed_host and "std::unordered_set<Impl::LiveScriptAttachment" in managed_host,
    "0.15.2 benchmark result export": "benchmark-results" in (root / "examples/performance_lab/managed/PerformanceLab.cs").read_text() and "JsonSerializer.Serialize" in (root / "examples/performance_lab/managed/PerformanceLab.cs").read_text(),
    "0.15.2 explicit export project": "AllowReferenceGameFallback" in export_script and "-Project is required" in export_script,
    "0.16.0 hierarchy regression test lifetime fix": "root_after_child" in (root / "tests/engine_logic_tests.cpp").read_text() and "reallocate" in (root / "tests/engine_logic_tests.cpp").read_text(),
    "0.16.0 managed package preflight": "validate_managed_stage" in project_package and "managed export stage is incomplete" in project_package and "before mutating output" not in project_package,
    "0.16.0 package payload self check": "validate_packaged_payload" in project_package and "portable managed payload has no host/fxr" in project_package and "shared player branding payload is missing" in project_package and "shared player legal payload is missing" in project_package,
    "1.0 shared-player legal payload": "Vespera.LICENSE.txt" in project_package and "Vespera.ThirdPartyNotices.md" in project_package and "Copying Vespera branding and legal notices" in (root / "runtime/player/CMakeLists.txt").read_text(encoding="utf-8"),
    "0.16.0 runtime-owned branding package": 'for (const char* directory : {"branding", "legal"})' in project_package and '$BrandingSource = Join-Path $Root "branding"' not in export_script,
    "0.16.0 Release GUI package executable": "patch_windows_pe_subsystem" in project_package and 'options.configuration == "Release"' in project_package and "IMAGE_SUBSYSTEM_WINDOWS_GUI" in project_package,
    "0.16.0 persistent runtime logging": "set_file_sink" in log_header and "Vespera runtime log opened" in log_source and "%LOCALAPPDATA%" not in player_main and '"Vespera" / "Logs"' in player_main and "fatal exception" in player_main and "SetUnhandledExceptionFilter" in player_main and "Unhandled Windows exception" in player_main,
    "0.16.0 exact export runtime discovery": 'runtime\\player\\$NativeConfig\\vespera_player.exe' in export_script and "Refusing to package the first recursive match" in export_script,
    "0.16.0 deterministic managed stage fallback": "multiple staged candidates" in export_script and "Refusing to package an arbitrary stale stage" in export_script,
    "0.16.0 headless Hub release path": "--create-template=" in project_hub and "Headless Project Hub creation requires" in project_hub and "create_project_from_template(options)" in project_hub,
    "0.16.0 experimental 2D decision": "2D / UI Foundation (Experimental)" in project_templates_source and "dedicated orthographic world tooling is not advertised for 1.0" in project_templates_source and "2D / UI FOUNDATION" in project_hub_rml and "playable screen-space 2D starter" in project_hub_rml and "Input.Value(\"move_horizontal\")" in (root / "templates/2d/managed/StarterGame.cs").read_text() and "SetProperty(\"left\"" in (root / "templates/2d/managed/StarterGame.cs").read_text() and 'id="playfield"' in (root / "templates/2d/assets/ui/main.rml").read_text() and (root / "templates/2d/START-HERE.md").exists(),
    "0.16.0 combined release gate": "Fresh Project Hub starter creation" in release_gate and "ManagedDeployment Portable" in release_gate and "Get-PeSubsystem" in release_gate and "IMAGE_SUBSYSTEM_WINDOWS_GUI" in release_gate and "Emberlight Guild" in release_gate and "Performance Lab" in release_gate and "Release runtime log" in release_gate,
    "0.16.2 explicit dotnet root precedence": "Preserve caller intent: an explicit -DotnetRoot must outrank ambient" in project_package and "ScopedEnvironmentVariable dotnet_root" in (root / "tests/engine_logic_tests.cpp").read_text() and "CTest LastTest.log (tail)" in (root / "test.ps1").read_text(),
    "1.0.0 four second startup branding": "startup_splash_minimum_seconds = 4.0f" in (root / "engine/include/vespera/core/application.hpp").read_text() and "kEditorStartupSplashMinimumSeconds = 4.0" in editor and "SDL_PumpEvents();" in application_source and "SDL_PumpEvents();" in editor and "LaunchSeconds = 6" in release_gate and "RC startup branding defaults to four seconds" in (root / "tests/engine_logic_tests.cpp").read_text(),
    "1.0.0 final single-build release gate": "run-release-gate.ps1" in rc_gate and "RC adversarial editor/runtime QA" in rc_gate and "--runtime-export-configuration" in rc_gate and '"Release"' in rc_gate and "PlayCycles" in rc_gate and "SceneSwitches" in rc_gate and "Vespera 1.0.0 RC gate PASSED" in rc_gate and "__vespera_rc_qa_reference" in rc_gate and "RC post-QA source validation" in rc_gate and "scale stress requires a clean saved edit scene" in (root / "tools/vespera_qa_runner.py").read_text(),
    "1.0.0 scene switch torture": "run_scene_switch_stress" in (root / "tools/vespera_qa_runner.py").read_text() and "--scene-switches" in (root / "tools/vespera_qa_runner.py").read_text() and "SceneSwitches" in (root / "tools/run-qa.ps1").read_text(),
    "1.0.0 Release exported-runtime QA": "runtime_export_configuration" in (root / "tools/vespera_qa_runner.py").read_text() and "RuntimeExportConfiguration" in (root / "tools/run-qa.ps1").read_text() and "configuration=runtime_export_configuration" in (root / "tools/vespera_qa_runner.py").read_text(),
    "1.0.0 release validation guide": (root / "docs/RELEASE_VALIDATION.md").exists() and "one native Release build" in (root / "docs/RELEASE_VALIDATION.md").read_text() and "4 seconds" in (root / "docs/RELEASE_VALIDATION.md").read_text(),
})

checks.update({
    "0.15.0 editor texture decoupling": "reference_textures.hpp" not in editor and "register_catalog_texture_assets" in editor and "load_scene_text_resilient" in editor,
    "0.15.0 resilient texture fallback": "make_missing_texture_placeholder" in (root / "engine/src/assets/texture_importer.cpp").read_text() and "load_scene_text_resilient" in (root / "engine/src/scene/scene_io.cpp").read_text() and "make_missing_texture_placeholder" in player_main,
    "0.15.0 empty editor Play guard": "Play Mode requires either an open Vespera project or an explicitly opened scene" in editor,
    "0.15.0 optimized branding icons": all((root / f"branding/{name}").is_file() for name in ("vespera_icon_window.png", "vespera_icon_window.bmp", "vespera_icon.ico")) and "vespera_icon_window.png" in (root / "examples/project_hub/main.cpp").read_text() and "vespera_icon_window.png" in player_main and "vespera_icon_window.bmp" in editor,
    "0.15.0 startup presentation": 'config.startup_splash_image = "branding/vespera_splash.png"' in (root / "examples/project_hub/main.cpp").read_text() and 'config.startup_sound = "branding/vespera_logo_sting.wav"' in (root / "examples/project_hub/main.cpp").read_text() and "startup_splash_minimum_seconds" in application_source and "kEditorStartupSplashMinimumSeconds" in editor,
    "0.15.0 Performance Lab project": (root / "examples/performance_lab/VesperaPerformanceLab.vesperaproject").exists() and (root / "examples/performance_lab/managed/PerformanceLab.cs").exists() and (root / "examples/performance_lab/assets/ui/main.rml").exists() and (root / "examples/performance_lab/assets/ui/performance.rcss").exists(),
    "0.15.0 Performance Lab benchmark": "Run Full Benchmark" in (root / "examples/performance_lab/managed/PerformanceLab.cs").read_text() and "1%low" in (root / "examples/performance_lab/managed/PerformanceLab.cs").read_text() and "BuildSwarm(Preset.Torture, 7000, 5300" in (root / "examples/performance_lab/managed/PerformanceLab.cs").read_text() and "Camera.State" in (root / "examples/performance_lab/managed/PerformanceLab.cs").read_text() and "Performance Gallery" in (root / "examples/performance_lab/assets/scenes/main.slscene").read_text(),
    "0.15.0 fresh starter regression": 'floor_tiles.bmp' not in (root / "templates/3d/assets/scenes/main.slscene").read_text() and "Performance Lab sample is a normal shared-player benchmark project" in (root / "tests/engine_logic_tests.cpp").read_text(),
})

checks.update({
    "0.12.0 Vespera public include tree": (root / "engine/include/vespera/core/application.hpp").exists() and (root / "engine/include/vespera/scene/scene.hpp").exists(),
    "0.12.0 legacy include forwarding": (root / "engine/include/sectorline/core/application.hpp").exists() and "<vespera/core/application.hpp>" in (root / "engine/include/sectorline/core/application.hpp").read_text() and "namespace sectorline = vespera" in (root / "engine/include/vespera/compat/sectorline_namespace.hpp").read_text(),
    "0.12.0 Vespera C++ namespace": "namespace vespera" in (root / "engine/include/vespera/scene/scene.hpp").read_text() and "vespera::Scene" in editor and "sectorline::Scene" not in editor,
    "0.12.0 Vespera build targets": "add_library(vespera_engine STATIC" in engine_cmake and "add_executable(vespera_editor" in (root / "editor/CMakeLists.txt").read_text() and "add_executable(vespera_reference_game" in (root / "examples/reference_game/CMakeLists.txt").read_text(),
    "0.12.0 target compatibility aliases": "add_library(sectorline_engine ALIAS vespera_engine)" in engine_cmake and "add_library(Sectorline::Engine ALIAS vespera_engine)" in engine_cmake and "add_executable(sectorline_editor ALIAS vespera_editor)" in (root / "editor/CMakeLists.txt").read_text(),
    "0.12.0 serialized identifier allowlist": '"sectorline.transform"' in (root / "engine/include/vespera/scene/scene.hpp").read_text() and "sectorline_scene 15" in (root / "docs/SCENE_FORMAT.md").read_text(),
})

current_failed = [name for name, ok in checks.items() if name.startswith(("0.9.8", "0.9.9", "0.12.0", "0.13.0", "0.14.0", "0.14.1", "0.15.0", "0.15.1", "0.15.2", "0.16.0", "0.16.2", "1.0.0")) and not ok]
if current_failed:
    print("Current-checkpoint source checks failed:")
    for name in current_failed:
        print(" -", name)
    sys.exit(1)

print("Vespera Engine 1.0.0 source structure validation passed.")
