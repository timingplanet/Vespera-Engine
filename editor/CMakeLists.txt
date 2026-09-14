set(VESPERA_IMGUI_RENDERER_SOURCE
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)

if(WIN32)
    set(VESPERA_IMGUI_RENDERER_SOURCE
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
    )
endif()

add_library(vespera_editor_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${VESPERA_IMGUI_RENDERER_SOURCE}
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
target_compile_features(vespera_editor_imgui PUBLIC cxx_std_20)
add_library(sectorline_editor_imgui ALIAS vespera_editor_imgui)

add_executable(vespera_editor
    main.cpp
    editor_command.hpp
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
