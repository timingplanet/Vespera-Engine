add_library(vespera_engine STATIC
    src/audio/audio.cpp
    src/assets/asset_catalog.cpp
    src/assets/asset_authoring.cpp
    src/assets/rml_asset_references.cpp
    src/assets/audio_clip_asset.cpp
    src/assets/build_manifest.cpp
    src/assets/font_asset.cpp
    src/assets/material_asset.cpp
    src/assets/project_package.cpp
    src/assets/runtime_asset_monitor.cpp
    src/assets/sprite_clip_asset.cpp
    src/assets/sprite_sheet_asset.cpp
    src/assets/texture_importer.cpp
    src/core/application.cpp
    src/core/log.cpp
    src/project/project.cpp
    src/project/project_templates.cpp
    src/render/view_frustum.cpp
    src/runtime/player_project.cpp
    src/input/input.cpp
    src/scripting/managed_script_host.cpp
    src/scene/scene_io.cpp
    src/scene/scene_hierarchy.cpp
    src/scene/prefab.cpp
    src/scene/scene_collision.cpp
    src/scene/scene_triggers.cpp
    src/scene/scene_raycast.cpp
    src/scene/component_access.cpp
    src/scene/scene_validation.cpp
    src/scene/sprite_animation.cpp
    src/ui/ui.cpp
    src/ui/ui_io.cpp
    src/ui/ui_render.cpp
    src/ui/ui_surface.cpp
    src/world/sector_world.cpp
    src/world/sector_mesh.cpp
)

add_library(Vespera::Engine ALIAS vespera_engine)
# Deprecated pre-1.0 target aliases for existing integrations.
add_library(sectorline_engine ALIAS vespera_engine)
add_library(Sectorline::Engine ALIAS vespera_engine)

target_include_directories(vespera_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(vespera_engine
    PUBLIC
        SDL3::SDL3-static
)

target_compile_features(vespera_engine PUBLIC cxx_std_20)

if(VESPERA_LUA_SCRIPTING)
    target_sources(vespera_engine PRIVATE src/scripting/lua_script_host.cpp)
    target_link_libraries(vespera_engine PRIVATE vespera_lua)
    target_compile_definitions(vespera_engine PUBLIC VESPERA_HAS_LUA=1)
endif()

if(VESPERA_RMLUI_UI)
    target_sources(vespera_engine PRIVATE src/ui/rmlui_surface.cpp)
    target_link_libraries(vespera_engine PRIVATE RmlUi::RmlUi)
    target_compile_definitions(vespera_engine PUBLIC VESPERA_HAS_RMLUI=1 VESPERA_HAS_RMLUI_EVALUATION=1)
endif()

if(MSVC)
    target_compile_options(vespera_engine PRIVATE /W4 /permissive- /Zc:__cplusplus)
    if(VESPERA_WARNINGS_AS_ERRORS)
        target_compile_options(vespera_engine PRIVATE /WX)
    endif()
else()
    target_compile_options(vespera_engine PRIVATE -Wall -Wextra -Wpedantic)
    if(VESPERA_WARNINGS_AS_ERRORS)
        target_compile_options(vespera_engine PRIVATE -Werror)
    endif()
endif()

if(WIN32)
    target_sources(vespera_engine PRIVATE
        src/render/d3d12/d3d12_renderer.cpp
    )
    target_compile_definitions(vespera_engine
        PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN
        PRIVATE VESPERA_HAS_D3D12=1
    )
    target_link_libraries(vespera_engine PRIVATE d3d12 d3dcompiler dxgi dxguid windowscodecs ole32 gdi32)
else()
    target_sources(vespera_engine PRIVATE
        src/render/null_renderer.cpp
    )
endif()
