add_library(doctest INTERFACE)
add_library(doctest::doctest ALIAS doctest)
target_include_directories(doctest INTERFACE ${CMAKE_SOURCE_DIR}/vendor-mini)

add_executable(vespera_engine_logic_tests
    engine_logic_tests.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/input/input.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/assets/asset_catalog.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/assets/asset_authoring.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/assets/build_manifest.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/assets/project_package.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/assets/rml_asset_references.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/project/project.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/render/view_frustum.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/scene/scene_hierarchy.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/project/project_templates.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/runtime/player_project.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/ui/ui.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/ui/ui_surface.cpp
)

target_include_directories(vespera_engine_logic_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/engine/include
)

target_link_libraries(vespera_engine_logic_tests PRIVATE doctest::doctest)
target_compile_features(vespera_engine_logic_tests PRIVATE cxx_std_20)
target_compile_definitions(vespera_engine_logic_tests PRIVATE VESPERA_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\")

if(MSVC)
    target_compile_options(vespera_engine_logic_tests PRIVATE /W4 /permissive- /Zc:__cplusplus)
    if(VESPERA_WARNINGS_AS_ERRORS)
        target_compile_options(vespera_engine_logic_tests PRIVATE /WX)
    endif()
else()
    target_compile_options(vespera_engine_logic_tests PRIVATE -Wall -Wextra -Wpedantic)
    if(VESPERA_WARNINGS_AS_ERRORS)
        target_compile_options(vespera_engine_logic_tests PRIVATE -Werror)
    endif()
endif()

add_test(NAME vespera.engine.logic COMMAND vespera_engine_logic_tests)
set_tests_properties(vespera.engine.logic PROPERTIES LABELS "unit;engine;logic")
