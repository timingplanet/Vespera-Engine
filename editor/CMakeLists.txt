set(VESPERA_IMGUI_RENDERER_SOURCES)
if(WIN32)
    list(APPEND VESPERA_IMGUI_RENDERER_SOURCES
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
    )
    if(VESPERA_VULKAN_RENDERER)
        list(APPEND VESPERA_IMGUI_RENDERER_SOURCES
            ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
        )
    endif()
elseif(UNIX AND NOT APPLE AND VESPERA_VULKAN_RENDERER)
    list(APPEND VESPERA_IMGUI_RENDERER_SOURCES
        ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
    )
else()
    list(APPEND VESPERA_IMGUI_RENDERER_SOURCES
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
    )
endif()

add_library(vespera_editor_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${VESPERA_IMGUI_RENDERER_SOURCES}
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
)

target_include_directories(vespera_editor_imgui
    PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${imgui_SOURCE_DIR}/misc/cpp
)

target_link_libraries(vespera_editor_imgui PUBLIC SDL3::SDL3-static)
if(WIN32)
    target_link_libraries(vespera_editor_imgui PUBLIC d3d12 dxgi d3dcompiler)
endif()
if(VESPERA_VULKAN_RENDERER AND (WIN32 OR (UNIX AND NOT APPLE)))
    target_link_libraries(vespera_editor_imgui PUBLIC Vulkan::Headers)
    target_compile_definitions(vespera_editor_imgui PUBLIC
        IMGUI_IMPL_VULKAN_NO_PROTOTYPES=1
        VK_NO_PROTOTYPES=1
    )
endif()
target_compile_features(vespera_editor_imgui PUBLIC cxx_std_20)
add_library(sectorline_editor_imgui ALIAS vespera_editor_imgui)

add_executable(vespera_editor
    main.cpp
    editor_command.hpp
    editor_state.hpp
    editor_selection.cpp
    editor_selection.hpp
    editor_console.cpp
    editor_console.hpp
    editor_console_panel.cpp
    editor_console_panel.hpp
    editor_workspace.cpp
    editor_workspace.hpp
    editor_session_dialogs.cpp
    editor_session_dialogs.hpp
    editor_build_window.cpp
    editor_build_window.hpp
    editor_menu.cpp
    editor_menu.hpp
    editor_hierarchy.cpp
    editor_hierarchy.hpp
    editor_sector_view.cpp
    editor_sector_view.hpp
    editor_scene_3d_panel.cpp
    editor_scene_3d_panel.hpp
    editor_game_view.cpp
    editor_game_view.hpp
    editor_ui_authoring.cpp
    editor_ui_authoring.hpp
    editor_project_panel.cpp
    editor_project_panel.hpp
    editor_inspector.cpp
    editor_inspector.hpp
    editor_extensions.cpp
    editor_extensions.hpp
    editor_window_identity.cpp
    editor_window_identity.hpp
    editor_style.cpp
    editor_style.hpp
    editor_startup_splash.cpp
    editor_startup_splash.hpp
    editor_frame_ui.cpp
    editor_frame_ui.hpp
    editor_render_viewports.cpp
    editor_render_viewports.hpp
    editor_windows_d3d12.cpp
    editor_windows_d3d12.hpp
    editor_vulkan.cpp
    editor_vulkan.hpp
    editor_history.cpp
    editor_history.hpp
    editor_assets.cpp
    editor_assets.hpp
    editor_play_controls.cpp
    editor_play_controls.hpp
    editor_project_session.cpp
    editor_project_session.hpp
    editor_command_runtime.hpp
    editor_view_helpers.cpp
    editor_view_helpers.hpp
    editor_scene_commands.cpp
    editor_scene_commands.hpp
    editor_prefabs.cpp
    editor_prefabs.hpp
    editor_managed.cpp
    editor_managed.hpp
    editor_asset_interactions.cpp
    editor_asset_interactions.hpp
    editor_scene_3d.cpp
    editor_scene_3d.hpp
    editor_automation_values.cpp
    editor_automation_values.hpp
    editor_installation.cpp
    editor_installation.hpp
    editor_build_pipeline.cpp
    editor_build_pipeline.hpp
    editor_automation.cpp
    editor_automation.hpp
    extension_api.hpp
    automation_protocol.cpp
    automation_protocol.hpp
    automation_server.cpp
    automation_server.hpp
    automation_tools.hpp
    play_runtime.cpp
    play_runtime.hpp
    rml_source_editor.cpp
    rml_source_editor.hpp
)

target_link_libraries(vespera_editor PRIVATE
    Vespera::Engine
    vespera_editor_imgui
)

if(WIN32)
    target_link_libraries(vespera_editor PRIVATE d3d12 dxgi ws2_32)
    target_compile_definitions(vespera_editor PRIVATE VESPERA_EDITOR_D3D12=1)
endif()
if(VESPERA_VULKAN_RENDERER AND (WIN32 OR (UNIX AND NOT APPLE)))
    target_compile_definitions(vespera_editor PRIVATE VESPERA_EDITOR_VULKAN=1)
endif()

target_compile_features(vespera_editor PRIVATE cxx_std_20)
add_executable(sectorline_editor ALIAS vespera_editor)

if(MSVC)
    target_compile_options(vespera_editor PRIVATE /W4 /permissive- /Zc:__cplusplus)
else()
    target_compile_options(vespera_editor PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(vespera_editor PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:vespera_editor>"
)

add_custom_command(TARGET vespera_editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/examples/reference_game/assets"
        "$<TARGET_FILE_DIR:vespera_editor>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/examples/reference_game/VesperaReference.vesperaproject"
        "$<TARGET_FILE_DIR:vespera_editor>/VesperaReference.vesperaproject"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/branding"
        "$<TARGET_FILE_DIR:vespera_editor>/branding"
    COMMENT "Copying Vespera editor sample project assets and branding"
)

if(WIN32)
    target_sources(vespera_editor PRIVATE "${CMAKE_BINARY_DIR}/generated/vespera.rc")
endif()
