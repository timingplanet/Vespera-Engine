cmake_minimum_required(VERSION 3.25)

project(VesperaEngine VERSION 1.0.0 LANGUAGES C CXX)

if(WIN32)
    enable_language(RC)
    file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/branding/vespera_icon.ico" VESPERA_ICON_PATH)
    configure_file(
        "${CMAKE_SOURCE_DIR}/branding/vespera.rc.in"
        "${CMAKE_BINARY_DIR}/generated/vespera.rc"
        @ONLY
    )
endif()

# Human-visible build label used by the editor/runtime/MCP surface.
set(VESPERA_VERSION_LABEL "1.0.0")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Public build switches use the Vespera product name. Preserve command-line/cache
# compatibility for the old Sectorline option names through the 1.0 transition.
set(_VESPERA_LEGACY_BUILD_EXAMPLES "")
set(_VESPERA_LEGACY_BUILD_EDITOR "")
set(_VESPERA_LEGACY_BUILD_TOOLS "")
set(_VESPERA_LEGACY_WARNINGS_AS_ERRORS "")
if(DEFINED CACHE{SECTORLINE_BUILD_EXAMPLES})
    get_property(_VESPERA_LEGACY_BUILD_EXAMPLES CACHE SECTORLINE_BUILD_EXAMPLES PROPERTY VALUE)
endif()
if(DEFINED CACHE{SECTORLINE_BUILD_EDITOR})
    get_property(_VESPERA_LEGACY_BUILD_EDITOR CACHE SECTORLINE_BUILD_EDITOR PROPERTY VALUE)
endif()
if(DEFINED CACHE{SECTORLINE_BUILD_TOOLS})
    get_property(_VESPERA_LEGACY_BUILD_TOOLS CACHE SECTORLINE_BUILD_TOOLS PROPERTY VALUE)
endif()
if(DEFINED CACHE{SECTORLINE_WARNINGS_AS_ERRORS})
    get_property(_VESPERA_LEGACY_WARNINGS_AS_ERRORS CACHE SECTORLINE_WARNINGS_AS_ERRORS PROPERTY VALUE)
endif()

option(VESPERA_BUILD_EXAMPLES "Build Vespera example projects" ON)
option(VESPERA_BUILD_EDITOR "Build the Vespera editor" ON)
option(VESPERA_BUILD_TOOLS "Build Vespera developer/package tools" ON)
option(VESPERA_WARNINGS_AS_ERRORS "Treat Vespera warnings as errors" OFF)

foreach(_legacy_pair
    "SECTORLINE_BUILD_EXAMPLES;VESPERA_BUILD_EXAMPLES;${_VESPERA_LEGACY_BUILD_EXAMPLES}"
    "SECTORLINE_BUILD_EDITOR;VESPERA_BUILD_EDITOR;${_VESPERA_LEGACY_BUILD_EDITOR}"
    "SECTORLINE_BUILD_TOOLS;VESPERA_BUILD_TOOLS;${_VESPERA_LEGACY_BUILD_TOOLS}"
    "SECTORLINE_WARNINGS_AS_ERRORS;VESPERA_WARNINGS_AS_ERRORS;${_VESPERA_LEGACY_WARNINGS_AS_ERRORS}")
    list(GET _legacy_pair 0 _old_name)
    list(GET _legacy_pair 1 _new_name)
    list(GET _legacy_pair 2 _old_value)
    if(NOT "${_old_value}" STREQUAL "")
        set(${_new_name} "${_old_value}" CACHE BOOL "Vespera compatibility value migrated from ${_old_name}" FORCE)
        message(DEPRECATION "${_old_name} is deprecated; use ${_new_name}")
    endif()
endforeach()
option(VESPERA_BUILD_TESTS "Build Vespera behavioral unit tests" OFF)
option(VESPERA_TESTS_ONLY "Configure only the lightweight Vespera behavioral tests" OFF)
option(VESPERA_RMLUI_UI "Build the RmlUi-backed Vespera production UI foundation" ON)
option(VESPERA_RMLUI_EVALUATION "Deprecated alias for VESPERA_RMLUI_UI" OFF)
option(VESPERA_LUA_SCRIPTING "Build the lightweight Vespera Lua 5.4 runtime scripting host" ON)
if(VESPERA_RMLUI_EVALUATION)
    set(VESPERA_RMLUI_UI ON CACHE BOOL "Build the RmlUi-backed Vespera production UI foundation" FORCE)
endif()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/include/vespera/core/version.hpp.in
    ${CMAKE_BINARY_DIR}/generated/vespera/core/version.hpp
    @ONLY
)

include(FetchContent)

# Cheap behavioral tests can configure without SDL, RmlUi, the editor, or a GPU
# backend. This keeps the fast test loop independent from the production build.
if(VESPERA_TESTS_ONLY)
    set(VESPERA_BUILD_TESTS ON CACHE BOOL "Build Vespera behavioral unit tests" FORCE)
    enable_testing()
    add_subdirectory(tests)
    return()
endif()

# Pin SDL so engine builds remain reproducible.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    SDL3
    URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.4.14.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(SDL3)

# Lightweight runtime/mod scripting. Lua 5.4.9 is the final 5.4 maintenance
# release and keeps Vespera on the mature 5.4 embedding API while C# remains
# the primary full-game scripting surface. Build the official ANSI-C sources
# directly so projects do not need a machine-wide Lua install.
if(VESPERA_LUA_SCRIPTING)
    FetchContent_Declare(
        lua_source
        URL https://www.lua.org/ftp/lua-5.4.9.tar.gz
        URL_HASH SHA256=2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(lua_source)
    add_library(vespera_lua STATIC
        ${lua_source_SOURCE_DIR}/src/lapi.c
        ${lua_source_SOURCE_DIR}/src/lauxlib.c
        ${lua_source_SOURCE_DIR}/src/lbaselib.c
        ${lua_source_SOURCE_DIR}/src/lcode.c
        ${lua_source_SOURCE_DIR}/src/lcorolib.c
        ${lua_source_SOURCE_DIR}/src/lctype.c
        ${lua_source_SOURCE_DIR}/src/ldblib.c
        ${lua_source_SOURCE_DIR}/src/ldebug.c
        ${lua_source_SOURCE_DIR}/src/ldo.c
        ${lua_source_SOURCE_DIR}/src/ldump.c
        ${lua_source_SOURCE_DIR}/src/lfunc.c
        ${lua_source_SOURCE_DIR}/src/lgc.c
        ${lua_source_SOURCE_DIR}/src/linit.c
        ${lua_source_SOURCE_DIR}/src/liolib.c
        ${lua_source_SOURCE_DIR}/src/llex.c
        ${lua_source_SOURCE_DIR}/src/lmathlib.c
        ${lua_source_SOURCE_DIR}/src/lmem.c
        ${lua_source_SOURCE_DIR}/src/loadlib.c
        ${lua_source_SOURCE_DIR}/src/lobject.c
        ${lua_source_SOURCE_DIR}/src/lopcodes.c
        ${lua_source_SOURCE_DIR}/src/loslib.c
        ${lua_source_SOURCE_DIR}/src/lparser.c
        ${lua_source_SOURCE_DIR}/src/lstate.c
        ${lua_source_SOURCE_DIR}/src/lstring.c
        ${lua_source_SOURCE_DIR}/src/lstrlib.c
        ${lua_source_SOURCE_DIR}/src/ltable.c
        ${lua_source_SOURCE_DIR}/src/ltablib.c
        ${lua_source_SOURCE_DIR}/src/ltm.c
        ${lua_source_SOURCE_DIR}/src/lundump.c
        ${lua_source_SOURCE_DIR}/src/lutf8lib.c
        ${lua_source_SOURCE_DIR}/src/lvm.c
        ${lua_source_SOURCE_DIR}/src/lzio.c
    )
    target_include_directories(vespera_lua PUBLIC ${lua_source_SOURCE_DIR}/src)
    target_compile_definitions(vespera_lua PRIVATE $<$<PLATFORM_ID:Windows>:LUA_USE_WINDOWS>)
    if(UNIX AND NOT APPLE)
        target_compile_definitions(vespera_lua PRIVATE LUA_USE_LINUX)
        target_link_libraries(vespera_lua PRIVATE ${CMAKE_DL_LIBS} m)
    endif()
endif()

# 0.9.8 production RmlUi foundation. The legacy .slui renderer remains available
# for compatibility/QA while new Vespera-facing UI work is built on pinned RmlUi + FreeType.
if(VESPERA_RMLUI_UI)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
    set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)

    FetchContent_Declare(
        freetype
        URL https://github.com/freetype/freetype/archive/refs/tags/VER-2-14-1.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(freetype)
    if(TARGET freetype AND NOT TARGET Freetype::Freetype)
        add_library(Freetype::Freetype ALIAS freetype)
    endif()
    # RmlUi 6.2 treats FreeType as a soft dependency and accepts a consuming
    # project-provided Freetype::Freetype target, so no machine-wide install is
    # required when this fetched alias is present.

    set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
    set(RMLUI_FONT_ENGINE "freetype" CACHE STRING "" FORCE)
    set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "" FORCE)
    set(RMLUI_LOTTIE_PLUGIN OFF CACHE BOOL "" FORCE)
    set(RMLUI_SVG_PLUGIN OFF CACHE BOOL "" FORCE)
    set(RMLUI_HARFBUZZ_SAMPLE OFF CACHE BOOL "" FORCE)
    set(RMLUI_COMPILER_OPTIONS OFF CACHE BOOL "" FORCE)
    set(RMLUI_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
    set(RMLUI_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        rmlui
        URL https://github.com/mikke89/RmlUi/archive/refs/tags/6.2.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(rmlui)
endif()

add_subdirectory(engine)
add_subdirectory(runtime/player)

if(VESPERA_BUILD_TOOLS)
    add_subdirectory(tools/packager)
endif()

if(VESPERA_BUILD_EXAMPLES)
    add_subdirectory(examples/reference_game)
    if(VESPERA_RMLUI_UI)
        add_subdirectory(examples/rmlui_spike)
        add_subdirectory(examples/project_hub)
    endif()
endif()

if(VESPERA_BUILD_EDITOR)
    # Dear ImGui docking branch, pinned to the v1.92.9b-docking commit.
    # The editor uses ImGui for tooling UI only; game/runtime UI is not tied to it.
    FetchContent_Declare(
        imgui
        URL https://github.com/ocornut/imgui/archive/b48d1afbe8ee8b238e2961dc363a949dd7304e23.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(imgui)
    add_subdirectory(editor)
endif()
if(VESPERA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
