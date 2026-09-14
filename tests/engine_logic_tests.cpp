#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <vespera/assets/asset_authoring.hpp>
#include <vespera/assets/asset_catalog.hpp>
#include <vespera/assets/build_manifest.hpp>
#include <vespera/assets/project_package.hpp>
#include <vespera/assets/rml_asset_references.hpp>
#include <vespera/input/input.hpp>
#include <vespera/project/project.hpp>
#include <vespera/render/view_frustum.hpp>
#include <vespera/project/project_templates.hpp>
#include <vespera/runtime/player_project.hpp>
#include <vespera/ui/ui.hpp>
#include <vespera/ui/ui_surface.hpp>
#include <vespera/ui/rmlui_surface.hpp>
#include <vespera/scene/scene_stats.hpp>
#include <vespera/scene/scene_hierarchy.hpp>
#include <vespera/core/game.hpp>
#include <vespera/core/application.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <type_traits>

namespace fs = std::filesystem;
using namespace vespera;

static_assert(std::is_base_of_v<UiSurface, RmlUiSurface>);

namespace {


class ScopedEnvironmentVariable {
public:
    ScopedEnvironmentVariable(std::string name, std::string value) : name_(std::move(name)) {
        if (const char* current = std::getenv(name_.c_str())) {
            had_original_ = true;
            original_ = current;
        }
        set(value);
    }

    ~ScopedEnvironmentVariable() {
        if (had_original_) set(original_);
        else clear();
    }

    ScopedEnvironmentVariable(const ScopedEnvironmentVariable&) = delete;
    ScopedEnvironmentVariable& operator=(const ScopedEnvironmentVariable&) = delete;

private:
    void set(const std::string& value) {
#ifdef _WIN32
        _putenv_s(name_.c_str(), value.c_str());
#else
        setenv(name_.c_str(), value.c_str(), 1);
#endif
    }

    void clear() {
#ifdef _WIN32
        _putenv_s(name_.c_str(), "");
#else
        unsetenv(name_.c_str());
#endif
    }

    std::string name_;
    std::string original_;
    bool had_original_ = false;
};

class TempDirectory {
public:
    explicit TempDirectory(std::string_view label) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() / ("vespera-tests-" + std::string(label) + "-" + std::to_string(stamp));
        std::error_code ec;
        fs::create_directories(path_, ec);
        REQUIRE_MESSAGE(!ec, "failed to create temporary directory: " << ec.message());
    }

    ~TempDirectory() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    [[nodiscard]] const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

void write_text_file(const fs::path& path, std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE_MESSAGE(output.good(), "failed to create test file: " << path.string());
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    REQUIRE_MESSAGE(output.good(), "failed to write test file: " << path.string());
}

std::string read_text_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE_MESSAGE(input.good(), "failed to read test file: " << path.string());
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

void write_fake_windows_exe(const fs::path& path, std::uint16_t subsystem = 3u) {
    fs::create_directories(path.parent_path());
    std::array<unsigned char, 256> image{};
    image[0] = 'M';
    image[1] = 'Z';
    constexpr std::uint32_t pe_offset = 0x80u;
    image[0x3c] = static_cast<unsigned char>(pe_offset & 0xffu);
    image[0x3d] = static_cast<unsigned char>((pe_offset >> 8u) & 0xffu);
    image[0x3e] = static_cast<unsigned char>((pe_offset >> 16u) & 0xffu);
    image[0x3f] = static_cast<unsigned char>((pe_offset >> 24u) & 0xffu);
    image[pe_offset + 0] = 'P';
    image[pe_offset + 1] = 'E';
    constexpr std::size_t subsystem_offset = pe_offset + 4u + 20u + 68u;
    image[subsystem_offset] = static_cast<unsigned char>(subsystem & 0xffu);
    image[subsystem_offset + 1] = static_cast<unsigned char>((subsystem >> 8u) & 0xffu);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE_MESSAGE(output.good(), "failed to create fake PE: " << path.string());
    output.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    REQUIRE(output.good());
}

std::uint16_t read_fake_windows_subsystem(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    input.seekg(0x3c, std::ios::beg);
    std::array<unsigned char, 4> offset{};
    input.read(reinterpret_cast<char*>(offset.data()), 4);
    REQUIRE(input.good());
    const std::uint32_t pe_offset = static_cast<std::uint32_t>(offset[0])
        | (static_cast<std::uint32_t>(offset[1]) << 8u)
        | (static_cast<std::uint32_t>(offset[2]) << 16u)
        | (static_cast<std::uint32_t>(offset[3]) << 24u);
    input.seekg(static_cast<std::streamoff>(pe_offset + 4u + 20u + 68u), std::ios::beg);
    std::array<unsigned char, 2> value{};
    input.read(reinterpret_cast<char*>(value.data()), 2);
    REQUIRE(input.good());
    return static_cast<std::uint16_t>(value[0]) | (static_cast<std::uint16_t>(value[1]) << 8u);
}

void write_minimal_template(const fs::path& template_root, std::string_view folder) {
    const auto source = template_root / folder;
    fs::create_directories(source / "assets" / "scenes");

    std::ofstream project(source / "Template.vesperaproject");
    REQUIRE(project.good());
    project
        << "vespera_project 9\n"
        << "name \"Template\"\n"
        << "assets \"assets\"\n"
        << "startup_scene \"scenes/main.slscene\"\n"
        << "product_version \"0.1.0\"\n"
        << "managed_deployment \"framework-dependent\"\n"
        << "window_width 1280\n"
        << "window_height 720\n"
        << "window_resizable 1\n"
        << "relative_mouse 0\n"
        << "escape_quits 0\n"
        << "end_project\n";

    std::ofstream scene(source / "assets" / "scenes" / "main.slscene");
    REQUIRE(scene.good());
    scene << "vespera_scene 15\nend_scene\n";
}

} // namespace


TEST_CASE("RC startup branding defaults to four seconds") {
    ApplicationConfig config;
    CHECK(config.startup_splash_minimum_seconds == doctest::Approx(4.0f));
}

TEST_CASE("Scene ID lookup cache survives vector reordering and destructive edits") {
    Scene scene;
    auto& a = scene.create_entity("A");
    const SceneObjectId a_id = a.id;
    auto& b = scene.create_entity("B");
    const SceneObjectId b_id = b.id;
    auto& c = scene.create_entity("C");
    const SceneObjectId c_id = c.id;

    REQUIRE(scene.find_entity(a_id));
    REQUIRE(scene.find_entity(b_id));
    REQUIRE(scene.find_entity(c_id));

    // Public vector mutation is used by editor/serializer workflows. Cached
    // indices must be verified rather than trusted blindly.
    std::swap(scene.entities[0], scene.entities[2]);
    CHECK(scene.find_entity(a_id)->name == "A");
    CHECK(scene.find_entity(c_id)->name == "C");

    CHECK(scene.destroy_entity(b_id));
    CHECK(scene.find_entity(b_id) == nullptr);
    REQUIRE(scene.find_entity(a_id));
    REQUIRE(scene.find_entity(c_id));

    Entity* clone = scene.clone_entity(a_id, "A clone");
    REQUIRE(clone);
    CHECK(scene.find_entity(clone->id) == clone);
}

TEST_CASE("World transform keeps root fast path and nested hierarchy semantics") {
    Scene scene;
    auto& root = scene.create_entity("Root");
    const SceneObjectId root_id = root.id;
    root.transform.position = {3.0f, 4.0f, 5.0f};
    root.transform.rotation = {0.1f, 0.2f, 0.3f};
    root.transform.scale = {2.0f, 2.0f, 2.0f};

    const auto root_world = entity_world_transform(scene, root);
    CHECK(root_world.position.x == doctest::Approx(3.0f));
    CHECK(root_world.position.y == doctest::Approx(4.0f));
    CHECK(root_world.position.z == doctest::Approx(5.0f));

    auto& child = scene.create_entity("Child");
    child.parent_id = root_id;
    child.transform.position = {1.0f, 0.0f, 0.0f};
    child.transform.scale = {0.5f, 0.5f, 0.5f};
    const auto child_world = entity_world_transform(scene, child);
    // create_entity may reallocate Scene::entities, so never keep references
    // across later insertions in tests. Reacquire by stable ID just like runtime
    // systems do before comparing hierarchy composition.
    const Entity* root_after_child = scene.find_entity(root_id);
    REQUIRE(root_after_child != nullptr);
    const auto expected = compose_transforms(root_after_child->transform, child.transform);
    CHECK(child_world.position.x == doctest::Approx(expected.position.x));
    CHECK(child_world.position.y == doctest::Approx(expected.position.y));
    CHECK(child_world.position.z == doctest::Approx(expected.position.z));
    CHECK(child_world.scale.x == doctest::Approx(1.0f));
}

TEST_CASE("ViewFrustum conservatively culls spheres outside a perspective camera") {
    Camera camera;
    camera.position = {0.0f, 0.0f, 0.0f};
    camera.yaw = 0.0f;
    camera.pitch = 0.0f;
    camera.vertical_fov_degrees = 90.0f;
    camera.near_plane = 0.1f;
    camera.far_plane = 100.0f;

    const auto frustum = make_view_frustum(camera, 16.0f / 9.0f);
    CHECK(frustum.intersects_sphere({0.0f, 0.0f, 5.0f}, 0.5f));
    CHECK_FALSE(frustum.intersects_sphere({0.0f, 0.0f, -5.0f}, 0.5f));
    CHECK_FALSE(frustum.intersects_sphere({0.0f, 0.0f, 110.0f}, 1.0f));
    CHECK_FALSE(frustum.intersects_sphere({20.0f, 0.0f, 5.0f}, 0.5f));
    CHECK_FALSE(frustum.intersects_sphere({0.0f, 10.0f, 5.0f}, 0.5f));

    // A large sphere whose volume reaches back into the frustum must remain
    // visible even when its center is outside a side/far plane.
    CHECK(frustum.intersects_sphere({10.0f, 0.0f, 5.0f}, 5.0f));
    CHECK(frustum.intersects_sphere({0.0f, 0.0f, 103.0f}, 4.0f));
}

TEST_CASE("ViewFrustum follows camera yaw and pitch") {
    Camera camera;
    camera.position = {3.0f, 2.0f, -4.0f};
    camera.yaw = 1.57079632679f;
    camera.pitch = 0.0f;
    camera.vertical_fov_degrees = 70.0f;
    const auto right_facing = make_view_frustum(camera, 1.0f);
    CHECK(right_facing.intersects_sphere({9.0f, 2.0f, -4.0f}, 0.25f));
    CHECK_FALSE(right_facing.intersects_sphere({-3.0f, 2.0f, -4.0f}, 0.25f));

    camera.yaw = 0.0f;
    camera.pitch = 0.78539816339f;
    const auto pitched = make_view_frustum(camera, 1.0f);
    CHECK(pitched.intersects_sphere({3.0f, 8.0f, 2.0f}, 0.5f));
}

TEST_CASE("InputSystem key edges survive exactly one frame") {
    InputSystem input;

    input.host_begin_frame();
    input.host_set_key(Key::W, true);
    CHECK(input.down(Key::W));
    CHECK(input.pressed(Key::W));
    CHECK_FALSE(input.released(Key::W));

    input.host_begin_frame();
    CHECK(input.down(Key::W));
    CHECK_FALSE(input.pressed(Key::W));
    CHECK_FALSE(input.released(Key::W));

    input.host_set_key(Key::W, false);
    CHECK_FALSE(input.down(Key::W));
    CHECK_FALSE(input.pressed(Key::W));
    CHECK(input.released(Key::W));

    input.host_begin_frame();
    CHECK_FALSE(input.released(Key::W));
}

TEST_CASE("InputSystem key repeats accumulate and reset per frame") {
    InputSystem input;
    input.host_begin_frame();

    input.host_add_key_repeat(Key::Backspace);
    input.host_add_key_repeat(Key::Backspace);
    input.host_add_key_repeat(Key::Backspace);
    CHECK(input.repeat_count(Key::Backspace) == 3);

    input.host_begin_frame();
    CHECK(input.repeat_count(Key::Backspace) == 0);
}

TEST_CASE("InputSystem transient text and mouse delta reset but position persists") {
    InputSystem input;
    input.host_begin_frame();
    input.host_add_text_input("Ves");
    input.host_add_text_input("pera");
    input.host_add_mouse_delta(3.5f, -2.0f);
    input.host_set_mouse_position(640.0f, 360.0f);

    CHECK(input.text_input() == "Vespera");
    CHECK(input.mouse_delta_x() == doctest::Approx(3.5f));
    CHECK(input.mouse_delta_y() == doctest::Approx(-2.0f));
    CHECK(input.mouse_x() == doctest::Approx(640.0f));
    CHECK(input.mouse_y() == doctest::Approx(360.0f));

    input.host_begin_frame();
    CHECK(input.text_input().empty());
    CHECK(input.mouse_delta_x() == doctest::Approx(0.0f));
    CHECK(input.mouse_delta_y() == doctest::Approx(0.0f));
    CHECK(input.mouse_x() == doctest::Approx(640.0f));
    CHECK(input.mouse_y() == doctest::Approx(360.0f));
}

TEST_CASE("InputMap counts and clears bindings by action") {
    InputSystem input;
    auto& map = input.map();
    map.bind_key("move", Key::W);
    map.bind_key("move", Key::S, -1.0f);
    map.bind_gamepad_axis("look", GamepadAxis::RightX, 1.0f, 0.2f);

    CHECK(map.has_action("move"));
    CHECK(map.has_action("look"));
    CHECK(map.action_count() == 2);
    CHECK(map.binding_count() == 3);

    map.clear_action("move");
    CHECK_FALSE(map.has_action("move"));
    CHECK(map.action_count() == 1);
    CHECK(map.binding_count() == 1);

    map.clear();
    CHECK(map.action_count() == 0);
    CHECK(map.binding_count() == 0);
}

TEST_CASE("Input actions combine digital bindings and clamp to the public range") {
    InputSystem input;
    input.map().bind_key("move", Key::W, 0.75f);
    input.map().bind_key("move", Key::D, 0.75f);

    input.host_begin_frame();
    input.host_set_key(Key::W, true);
    CHECK(input.action_value("move") == doctest::Approx(0.75f));
    CHECK(input.action_pressed("move"));

    input.host_set_key(Key::D, true);
    CHECK(input.action_value("move") == doctest::Approx(1.0f));

    input.host_begin_frame();
    CHECK(input.action_down("move"));
    CHECK_FALSE(input.action_pressed("move"));
}

TEST_CASE("Input action overrides replace bindings and preserve edge semantics") {
    InputSystem input;
    input.map().bind_key("fire", Key::Enter);

    input.host_begin_frame();
    input.host_set_key(Key::Enter, true);
    CHECK(input.action_value("fire") == doctest::Approx(1.0f));

    input.host_set_action_override("fire", -2.0f);
    CHECK(input.action_value("fire") == doctest::Approx(-1.0f));
    CHECK(input.action_pressed("fire"));

    input.host_begin_frame();
    CHECK_FALSE(input.action_pressed("fire"));

    input.host_clear_action_override("fire");
    CHECK(input.action_value("fire") == doctest::Approx(1.0f));
    CHECK_FALSE(input.action_released("fire"));
}

TEST_CASE("Gamepad axes clamp input and apply deadzone remapping") {
    InputSystem input;
    input.map().bind_gamepad_axis("look", GamepadAxis::RightX, 1.0f, 0.2f);

    input.host_begin_frame();
    input.host_set_gamepad_axis(GamepadAxis::RightX, 2.0f);
    CHECK(input.gamepad_axis(GamepadAxis::RightX) == doctest::Approx(1.0f));
    CHECK(input.action_value("look") == doctest::Approx(1.0f));

    input.host_set_gamepad_axis(GamepadAxis::RightX, 0.1f);
    CHECK(input.action_value("look") == doctest::Approx(0.0f));

    input.host_set_gamepad_axis(GamepadAxis::RightX, 0.6f);
    CHECK(input.action_value("look") == doctest::Approx(0.5f));
}

TEST_CASE("Disconnecting a gamepad clears live and previous gamepad state") {
    InputSystem input;
    input.host_set_gamepad_connected(true, "Test Pad");
    input.host_begin_frame();
    input.host_set_gamepad_button(GamepadButton::South, true);
    input.host_set_gamepad_axis(GamepadAxis::LeftX, 0.8f);
    CHECK(input.gamepad_connected());
    CHECK(input.gamepad_down(GamepadButton::South));

    input.host_set_gamepad_connected(false);
    CHECK_FALSE(input.gamepad_connected());
    CHECK(input.gamepad_name().empty());
    CHECK_FALSE(input.gamepad_down(GamepadButton::South));
    CHECK_FALSE(input.gamepad_released(GamepadButton::South));
    CHECK(input.gamepad_axis(GamepadAxis::LeftX) == doctest::Approx(0.0f));
}

TEST_CASE("Project input bindings configure the same semantic InputMap used at runtime") {
    VesperaProject project;
    project.input_bindings = {
        {"move", "key", "W", 1.0f, 0.0f},
        {"move", "key", "S", -1.0f, 0.0f},
        {"look", "gamepad_axis", "RightX", 1.0f, 0.16f},
        {"accept", "gamepad_button", "South", 1.0f, 0.0f},
    };

    InputMap map;
    std::string error;
    CHECK(configure_project_input_map(project, map, &error));
    CHECK(error.empty());
    CHECK(map.action_count() == 3);
    CHECK(map.binding_count() == 4);
}

TEST_CASE("Invalid project input binding rejects cleanly and leaves the destination map empty") {
    VesperaProject project;
    project.input_bindings = {
        {"move", "key", "W", 1.0f, 0.0f},
        {"broken", "key", "DefinitelyNotAKey", 1.0f, 0.0f},
    };

    InputMap map;
    map.bind_key("old", Key::A);
    std::string error;
    CHECK_FALSE(configure_project_input_map(project, map, &error));
    CHECK_FALSE(error.empty());
    CHECK(map.action_count() == 0);
    CHECK(map.binding_count() == 0);
}

TEST_CASE("New in-memory projects start at the current project format") {
    const VesperaProject project;
    CHECK(project.format_version == 10);
    CHECK(project.vsync);
}

TEST_CASE("Project format round-trips shipping, input, and stable build-root data") {
    TempDirectory temp("project-roundtrip");
    const auto project_path = temp.path() / "RoundTrip.vesperaproject";

    VesperaProject original;
    original.name = "Round Trip";
    original.root_directory = temp.path();
    original.project_file = project_path;
    original.assets_directory = "game-assets";
    original.startup_scene = "scenes/start.slscene";
    original.startup_scene_asset_id = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    original.startup_ui = "ui/main.rml";
    original.startup_ui_asset_id = "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
    original.lua_entry = "scripts/main.lua";
    original.lua_entry_asset_id = "ffffffffffffffffffffffffffffffff";
    original.managed_project = "managed/Game.csproj";
    original.managed_assembly = "Game.Scripts";
    original.game_target = "GameTarget";
    original.company_name = "Vespera Tests";
    original.product_version = "2.3.4";
    original.package_name = "round-trip";
    original.managed_deployment = ManagedDeploymentMode::Portable;
    original.executable_name = "RoundTripGame";
    original.build_output_directory = "dist";
    original.game_icon = "textures/icon.png";
    original.game_icon_asset_id = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    original.development_diagnostics = false;
    original.window_title = "Round Trip Window";
    original.window_width = 1600;
    original.window_height = 900;
    original.window_resizable = false;
    original.relative_mouse = true;
    original.escape_quits = true;
    original.vsync = false;
    original.input_bindings = {
        {"move", "key", "W", 1.0f, 0.0f},
        {"look", "gamepad_axis", "RightX", 0.75f, 0.2f},
    };
    original.build_includes = {"ui/main.rml", "audio/theme.slaudio"};
    original.build_include_asset_ids = {
        "cccccccccccccccccccccccccccccccc",
        "dddddddddddddddddddddddddddddddd",
    };

    const auto saved = save_vespera_project(original, project_path);
    REQUIRE_MESSAGE(saved.ok, saved.message);
    const auto serialized = read_text_file(project_path);
    CHECK(serialized.find("vespera_project 10") != std::string::npos);
    const auto first_executable = serialized.find("executable_name ");
    REQUIRE(first_executable != std::string::npos);
    CHECK(serialized.find("executable_name ", first_executable + 1) == std::string::npos);

    VesperaProject loaded;
    const auto read = load_vespera_project(loaded, project_path);
    REQUIRE_MESSAGE(read.ok, read.message);

    CHECK(loaded.format_version == 10);
    CHECK(loaded.name == original.name);
    CHECK(loaded.assets_directory == original.assets_directory);
    CHECK(loaded.startup_scene == original.startup_scene);
    CHECK(loaded.startup_scene_asset_id == original.startup_scene_asset_id);
    CHECK(loaded.startup_ui == original.startup_ui);
    CHECK(loaded.startup_ui_asset_id == original.startup_ui_asset_id);
    CHECK(loaded.lua_entry == original.lua_entry);
    CHECK(loaded.lua_entry_asset_id == original.lua_entry_asset_id);
    CHECK(loaded.managed_project == original.managed_project);
    CHECK(loaded.managed_assembly == original.managed_assembly);
    CHECK(loaded.game_target == original.game_target);
    CHECK(loaded.company_name == original.company_name);
    CHECK(loaded.product_version == original.product_version);
    CHECK(loaded.package_name == original.package_name);
    CHECK(loaded.managed_deployment == ManagedDeploymentMode::Portable);
    CHECK(loaded.executable_name == original.executable_name);
    CHECK(loaded.build_output_directory == original.build_output_directory);
    CHECK(loaded.game_icon == original.game_icon);
    CHECK(loaded.game_icon_asset_id == original.game_icon_asset_id);
    CHECK(loaded.development_diagnostics == original.development_diagnostics);
    CHECK(loaded.window_title == original.window_title);
    CHECK(loaded.window_width == original.window_width);
    CHECK(loaded.window_height == original.window_height);
    CHECK(loaded.window_resizable == original.window_resizable);
    CHECK(loaded.relative_mouse == original.relative_mouse);
    CHECK(loaded.escape_quits == original.escape_quits);
    CHECK(loaded.vsync == original.vsync);
    REQUIRE(loaded.input_bindings.size() == original.input_bindings.size());
    CHECK(loaded.input_bindings[1].action == "look");
    CHECK(loaded.input_bindings[1].device == "gamepad_axis");
    CHECK(loaded.input_bindings[1].code == "RightX");
    CHECK(loaded.input_bindings[1].scale == doctest::Approx(0.75f));
    CHECK(loaded.input_bindings[1].deadzone == doctest::Approx(0.2f));
    CHECK(loaded.build_includes == original.build_includes);
    CHECK(loaded.build_include_asset_ids == original.build_include_asset_ids);
}

TEST_CASE("Project v9 remains load-compatible and defaults VSync on") {
    TempDirectory temp("project-v9-vsync-default");
    const auto project_path = temp.path() / "Legacy.vesperaproject";
    {
        std::ofstream out(project_path);
        out << "vespera_project 9\n"
            << "name \"Legacy\"\n"
            << "assets \"assets\"\n"
            << "window_title \"Legacy\"\n"
            << "window_width 1280\n"
            << "window_height 720\n"
            << "window_resizable 1\n"
            << "relative_mouse 0\n"
            << "escape_quits 0\n"
            << "end_project\n";
    }
    VesperaProject project;
    const auto loaded = load_vespera_project(project, project_path);
    REQUIRE_MESSAGE(loaded.ok, loaded.message);
    // Older projects load and normalize to the current in-memory writer version.
    CHECK(project.format_version == 10);
    CHECK(project.vsync);
}

TEST_CASE("Project loader rejects unsupported format versions with a useful line number") {
    TempDirectory temp("bad-project-version");
    const auto project_path = temp.path() / "Bad.vesperaproject";
    {
        std::ofstream out(project_path);
        out << "vespera_project 999\nend_project\n";
    }

    VesperaProject project;
    const auto result = load_vespera_project(project, project_path);
    CHECK_FALSE(result.ok);
    CHECK(result.message.find("project line 1") != std::string::npos);
    CHECK(result.message.find("unsupported project format version") != std::string::npos);
}

TEST_CASE("Packager refuses managed projects when the staged managed payload is missing") {
    TempDirectory temp("package-managed-preflight");
    const auto root = temp.path() / "project";
    const auto assets = root / "assets";
    write_text_file(assets / "scenes" / "main.slscene", "vespera_scene 15\nend_scene\n");

    AssetCatalog catalog;
    std::string error;
    REQUIRE(catalog.refresh(assets, &error));

    VesperaProject project;
    project.name = "Managed Package";
    project.root_directory = root;
    project.project_file = root / "ManagedPackage.vesperaproject";
    project.assets_directory = "assets";
    project.startup_scene = "scenes/main.slscene";
    project.managed_assembly = "Game.Scripts";
    REQUIRE(save_vespera_project(project, project.project_file));

    ProjectPackageOptions options;
    options.output_directory = temp.path() / "out";
    const auto packaged = export_project_package(project, catalog, options);
    CHECK_FALSE(packaged.ok);
    CHECK(packaged.message.find("no staged managed runtime directory") != std::string::npos);
    CHECK_FALSE(fs::exists(options.output_directory));
}

TEST_CASE("Shared-player package owns branding and validates managed payload closure") {
    TempDirectory temp("package-shared-player");
    const auto root = temp.path() / "project";
    const auto assets = root / "assets";
    write_text_file(assets / "scenes" / "main.slscene", "vespera_scene 15\nend_scene\n");

    AssetCatalog catalog;
    std::string error;
    REQUIRE(catalog.refresh(assets, &error));

    VesperaProject project;
    project.name = "Shipping Test";
    project.root_directory = root;
    project.project_file = root / "ShippingTest.vesperaproject";
    project.assets_directory = "assets";
    project.startup_scene = "scenes/main.slscene";
    project.managed_assembly = "Game.Scripts";
    project.executable_name = "ShippingTest";
    REQUIRE(save_vespera_project(project, project.project_file));

    const auto runtime_dir = temp.path() / "runtime";
    const auto runtime = runtime_dir / "vespera_player.exe";
    write_fake_windows_exe(runtime);
    write_text_file(runtime_dir / "branding" / "vespera_icon_window.png", "icon");
    write_text_file(runtime_dir / "branding" / "vespera_splash.png", "splash");
    write_text_file(runtime_dir / "branding" / "vespera_logo_sting.wav", "sting");
    write_text_file(runtime_dir / "legal" / "Vespera.LICENSE.txt", "license");
    write_text_file(runtime_dir / "legal" / "Vespera.ThirdPartyNotices.md", "notices");

    const auto managed = temp.path() / "managed";
    write_text_file(managed / "Vespera.Managed.runtimeconfig.json", R"({"runtimeOptions":{"framework":{"name":"Microsoft.NETCore.App","version":"8.0.1"}}})");
    write_text_file(managed / "Vespera.NET.dll", "bridge");
    write_text_file(managed / "Game.Scripts.dll", "game");

    ProjectPackageOptions options;
    options.output_directory = temp.path() / "out";
    options.runtime_executable = runtime;
    options.managed_directory = managed;
    options.configuration = "Release";
    const auto packaged = export_project_package(project, catalog, options);
    REQUIRE_MESSAGE(packaged.ok, packaged.message);
    CHECK(fs::is_regular_file(packaged.packaged_runtime));
    CHECK(packaged.packaged_runtime.filename() == fs::path("ShippingTest.exe"));
    CHECK(read_fake_windows_subsystem(packaged.packaged_runtime) == 2u);
    CHECK(fs::is_regular_file(options.output_directory / "branding" / "vespera_icon_window.png"));
    CHECK(fs::is_regular_file(options.output_directory / "branding" / "vespera_splash.png"));
    CHECK(fs::is_regular_file(options.output_directory / "branding" / "vespera_logo_sting.wav"));
    CHECK(fs::is_regular_file(options.output_directory / "legal" / "Vespera.LICENSE.txt"));
    CHECK(fs::is_regular_file(options.output_directory / "legal" / "Vespera.ThirdPartyNotices.md"));
    CHECK(fs::is_regular_file(options.output_directory / "managed" / "Vespera.NET.dll"));
    CHECK(fs::is_regular_file(options.output_directory / "managed" / "Game.Scripts.dll"));
    CHECK(fs::is_regular_file(options.output_directory / "Vespera.PackageManifest.txt"));
    CHECK(fs::is_regular_file(options.output_directory / "Vespera.PackageReport.txt"));
    const auto release_report = read_text_file(options.output_directory / "Vespera.PackageReport.txt");
    CHECK(release_report.find("Windows subsystem: GUI (no console)") != std::string::npos);
    CHECK(release_report.find("Runtime log: %LOCALAPPDATA%\\Vespera\\Logs\\ShippingTest.log") != std::string::npos);

    // Development/source-style packages must retain the console subsystem so
    // direct diagnostic runs are not accidentally made GUI-only by shipping polish.
    ProjectPackageOptions development = options;
    development.output_directory = temp.path() / "out-development";
    development.configuration = "Development";
    const auto development_packaged = export_project_package(project, catalog, development);
    REQUIRE_MESSAGE(development_packaged.ok, development_packaged.message);
    CHECK(read_fake_windows_subsystem(development_packaged.packaged_runtime) == 3u);
}

TEST_CASE("Release packaging preserves specialized custom runtime subsystem policy") {
    TempDirectory temp("package-custom-runtime");
    const auto root = temp.path() / "project";
    const auto assets = root / "assets";
    write_text_file(assets / "scenes" / "main.slscene", "vespera_scene 15\nend_scene\n");

    AssetCatalog catalog;
    std::string error;
    REQUIRE(catalog.refresh(assets, &error));

    VesperaProject project;
    project.name = "Custom Runtime Test";
    project.root_directory = root;
    project.project_file = root / "CustomRuntime.vesperaproject";
    project.assets_directory = "assets";
    project.startup_scene = "scenes/main.slscene";
    project.executable_name = "CustomRuntime";
    REQUIRE(save_vespera_project(project, project.project_file));

    const auto runtime = temp.path() / "runtime" / "custom_game.exe";
    write_fake_windows_exe(runtime);

    ProjectPackageOptions options;
    options.output_directory = temp.path() / "out";
    options.runtime_executable = runtime;
    options.configuration = "Release";
    const auto packaged = export_project_package(project, catalog, options);
    REQUIRE_MESSAGE(packaged.ok, packaged.message);
    CHECK(read_fake_windows_subsystem(packaged.packaged_runtime) == 3u);
    const auto report = read_text_file(packaged.package_report);
    CHECK(report.find("Windows subsystem: custom runtime target-defined") != std::string::npos);
    CHECK(report.find("Runtime log: custom runtime target-defined") != std::string::npos);
}

TEST_CASE("Portable package self-check requires and stages private hostfxr plus runtime") {
    TempDirectory temp("package-portable");
    const auto root = temp.path() / "project";
    const auto assets = root / "assets";
    write_text_file(assets / "scenes" / "main.slscene", "vespera_scene 15\nend_scene\n");

    AssetCatalog catalog;
    std::string error;
    REQUIRE(catalog.refresh(assets, &error));

    VesperaProject project;
    project.name = "Portable Test";
    project.root_directory = root;
    project.project_file = root / "PortableTest.vesperaproject";
    project.assets_directory = "assets";
    project.startup_scene = "scenes/main.slscene";
    project.managed_assembly = "Game.Scripts";
    project.executable_name = "PortableTest";
    project.managed_deployment = ManagedDeploymentMode::Portable;
    REQUIRE(save_vespera_project(project, project.project_file));

    const auto runtime_dir = temp.path() / "runtime";
    const auto runtime = runtime_dir / "vespera_player.exe";
    write_fake_windows_exe(runtime);
    write_text_file(runtime_dir / "branding" / "vespera_icon_window.png", "icon");
    write_text_file(runtime_dir / "branding" / "vespera_splash.png", "splash");
    write_text_file(runtime_dir / "branding" / "vespera_logo_sting.wav", "sting");
    write_text_file(runtime_dir / "legal" / "Vespera.LICENSE.txt", "license");
    write_text_file(runtime_dir / "legal" / "Vespera.ThirdPartyNotices.md", "notices");

    const auto managed = temp.path() / "managed";
    write_text_file(managed / "Vespera.Managed.runtimeconfig.json", R"({"runtimeOptions":{"framework":{"name":"Microsoft.NETCore.App","version":"8.0.1"}}})");
    write_text_file(managed / "Vespera.NET.dll", "bridge");
    write_text_file(managed / "Game.Scripts.dll", "game");

    // The explicit root intentionally sorts after an ambient candidate. Before
    // 0.16.2 candidate roots were lexicographically sorted, so Windows commonly
    // ignored -DotnetRoot and selected C:\Program Files\dotnet instead.
    const auto dotnet = temp.path() / "z-explicit-dotnet";
    write_text_file(dotnet / "host" / "fxr" / "8.0.1" / "hostfxr.dll", "hostfxr");
    write_text_file(dotnet / "shared" / "Microsoft.NETCore.App" / "8.0.1" / "System.Private.CoreLib.dll", "runtime");
    write_text_file(dotnet / "LICENSE.txt", "license");

    const auto ambient_dotnet = temp.path() / "a-ambient-dotnet";
    write_text_file(ambient_dotnet / "host" / "fxr" / "8.0.2" / "hostfxr.dll", "ambient-hostfxr");
    write_text_file(ambient_dotnet / "shared" / "Microsoft.NETCore.App" / "8.0.2" / "System.Private.CoreLib.dll", "ambient-runtime");
    ScopedEnvironmentVariable dotnet_root("DOTNET_ROOT", ambient_dotnet.string());
    ScopedEnvironmentVariable dotnet_root_x64("DOTNET_ROOT_X64", "");
    ScopedEnvironmentVariable program_files("ProgramFiles", (temp.path() / "no-program-files").string());
    ScopedEnvironmentVariable program_w6432("ProgramW6432", (temp.path() / "no-program-w6432").string());

    ProjectPackageOptions options;
    options.output_directory = temp.path() / "out";
    options.runtime_executable = runtime;
    options.managed_directory = managed;
    options.managed_deployment = ManagedDeploymentMode::Portable;
    options.dotnet_root = dotnet;
    options.configuration = "Release";
    const auto packaged = export_project_package(project, catalog, options);
    REQUIRE_MESSAGE(packaged.ok, packaged.message);
    CHECK(packaged.dotnet_runtime_version == "8.0.1");
    CHECK(fs::is_regular_file(options.output_directory / "dotnet" / "host" / "fxr" / "8.0.1" / "hostfxr.dll"));
    CHECK(fs::is_regular_file(options.output_directory / "dotnet" / "shared" / "Microsoft.NETCore.App" / "8.0.1" / "System.Private.CoreLib.dll"));
}

TEST_CASE("Project template catalog exposes the three supported starter types") {
    const auto catalog = project_template_catalog();
    REQUIRE(catalog.size() == 3);
    const auto* two_d = project_template_info("2d");
    REQUIRE(two_d != nullptr);
    CHECK(two_d->display_name.find("Experimental") != std::string_view::npos);
    CHECK(project_template_info("3d") != nullptr);
    CHECK(project_template_info("empty") != nullptr);
    CHECK(project_template_info("missing") == nullptr);
}

TEST_CASE("Project template creation rejects empty names before touching the destination") {
    TempDirectory temp("template-empty-name");
    const auto template_root = temp.path() / "templates";
    write_minimal_template(template_root, "empty");

    const auto destination = temp.path() / "projects";
    const auto result = create_project_from_template({
        .kind = ProjectTemplateKind::Empty,
        .project_name = "   ",
        .destination_directory = destination,
        .template_root = template_root,
    });

    CHECK_FALSE(result.ok);
    CHECK(result.message == "project name cannot be empty");
    CHECK_FALSE(fs::exists(destination));
}

TEST_CASE("Project template creation sanitizes the folder and finalizes project metadata") {
    TempDirectory temp("template-create");
    const auto template_root = temp.path() / "templates";
    write_minimal_template(template_root, "empty");

    const auto result = create_project_from_template({
        .kind = ProjectTemplateKind::Empty,
        .project_name = "  My Fancy Project!  ",
        .destination_directory = temp.path() / "projects",
        .template_root = template_root,
    });

    REQUIRE_MESSAGE(result.ok, result.message);
    CHECK(result.project_root.filename() == "My-Fancy-Project");
    CHECK(result.project_file.filename() == "My-Fancy-Project.vesperaproject");
    CHECK(fs::is_regular_file(result.project_file));
    CHECK_FALSE(fs::exists(result.project_root / "Template.vesperaproject"));
    CHECK(fs::is_regular_file(result.project_root / "assets" / "scenes" / "main.slscene"));

    VesperaProject created;
    const auto loaded = load_vespera_project(created, result.project_file);
    REQUIRE_MESSAGE(loaded.ok, loaded.message);
    CHECK(created.name == "My Fancy Project!");
    CHECK(created.window_title == "My Fancy Project!");
    CHECK(created.package_name == "My-Fancy-Project");
    CHECK(created.executable_name == "My-Fancy-Project");
    CHECK(created.product_version == "0.1.0");
}

TEST_CASE("Project template creation refuses a non-empty destination") {
    TempDirectory temp("template-existing");
    const auto template_root = temp.path() / "templates";
    write_minimal_template(template_root, "empty");

    const auto project_root = temp.path() / "projects" / "Existing";
    fs::create_directories(project_root);
    {
        std::ofstream marker(project_root / "keep.txt");
        marker << "do not overwrite";
    }

    const auto result = create_project_from_template({
        .kind = ProjectTemplateKind::Empty,
        .project_name = "Existing",
        .destination_directory = temp.path() / "projects",
        .template_root = template_root,
    });

    CHECK_FALSE(result.ok);
    CHECK(result.message.find("destination already exists and is not empty") != std::string::npos);
    CHECK(fs::is_regular_file(project_root / "keep.txt"));
}



TEST_CASE("Shipped starter templates own managed projects and use the shared player model") {
    const fs::path source_root = fs::path(VESPERA_SOURCE_DIR);
    struct Expectation {
        const char* folder;
        const char* script_class;
        bool has_rml;
    };
    const Expectation expectations[] = {
        {"2d", "VesperaGame.StarterGame", true},
        {"3d", "VesperaGame.StarterController", true},
        {"empty", "VesperaGame.Game", false},
    };

    for (const auto& expected : expectations) {
        const auto root = source_root / "templates" / expected.folder;
        VesperaProject project;
        const auto loaded = load_vespera_project(project, root / "Template.vesperaproject");
        REQUIRE_MESSAGE(loaded.ok, expected.folder << ": " << loaded.message);
        CHECK(project.managed_project == fs::path("managed/VesperaGame.Scripts.csproj"));
        CHECK(project.managed_assembly == "VesperaGame.Scripts");
        CHECK(project.game_target.empty());
        CHECK(project.escape_quits);
        CHECK(fs::is_regular_file(project.managed_project_path()));
        CHECK(fs::is_regular_file(root / "managed" / (std::string(expected.folder) == "3d" ? "StarterController.cs" : std::string(expected.folder) == "2d" ? "StarterGame.cs" : "Game.cs")));

        const auto scene = read_text_file(root / "assets" / "scenes" / "main.slscene");
        if (std::string(expected.folder) == "3d") CHECK(scene.find("floor_tiles.bmp") == std::string::npos);
        CHECK(scene.find(std::string("managed_script \"") + expected.script_class + "\" 1") != std::string::npos);
        if (expected.has_rml) {
            CHECK(project.format_version == 10);
            CHECK(project.startup_ui == fs::path("ui/main.rml"));
            CHECK(project.vsync);
            CHECK_FALSE(project.startup_ui_asset_id.empty());
            CHECK(project.build_includes.empty());
            if (std::string(expected.folder) == "2d") {
                CHECK_FALSE(project.relative_mouse);
                CHECK(project.input_bindings.size() == 8);
                const auto rml = read_text_file(root / "assets" / "ui" / "main.rml");
                const auto script = read_text_file(root / "managed" / "StarterGame.cs");
                CHECK(rml.find("id=\"playfield\"") != std::string::npos);
                CHECK(rml.find("id=\"player\"") != std::string::npos);
                CHECK(rml.find("id=\"goal\"") != std::string::npos);
                CHECK(script.find("Input.Value(\"move_horizontal\")") != std::string::npos);
                CHECK(script.find("Input.Value(\"move_vertical\")") != std::string::npos);
                CHECK(script.find("SetProperty(\"left\"") != std::string::npos);
                CHECK(script.find("SetProperty(\"top\"") != std::string::npos);
                CHECK(fs::is_regular_file(root / "START-HERE.md"));
            }
        } else {
            CHECK(project.startup_ui.empty());
            CHECK(project.startup_ui_asset_id.empty());
            CHECK(project.build_includes.empty());
        }
    }
}

TEST_CASE("Hub-created starter keeps project-owned managed source intact") {
    TempDirectory temp("real-template-create");
    const auto source_root = fs::path(VESPERA_SOURCE_DIR);
    const auto result = create_project_from_template({
        .kind = ProjectTemplateKind::Game3D,
        .project_name = "Starter Shipping Test",
        .destination_directory = temp.path() / "projects",
        .template_root = source_root / "templates",
    });

    REQUIRE_MESSAGE(result.ok, result.message);
    VesperaProject project;
    const auto loaded = load_vespera_project(project, result.project_file);
    REQUIRE_MESSAGE(loaded.ok, loaded.message);
    CHECK(project.name == "Starter Shipping Test");
    CHECK(project.managed_assembly == "VesperaGame.Scripts");
    CHECK(project.game_target.empty());
    CHECK(fs::is_regular_file(project.managed_project_path()));
    CHECK(fs::is_regular_file(result.project_root / "managed" / "StarterController.cs"));
    CHECK(read_text_file(project.managed_project_path()).find("$(VesperaSdkProject)") != std::string::npos);
}

TEST_CASE("RML asset reference scanner resolves local href src and url paths without false attribute matches") {
    const std::string rml = R"(<rml>
<head><link type="text/rcss" href="../styles/menu.rcss?theme=dark#main" /></head>
<body>
<img src="../textures/panel.png#hero" />
<div data-src="../textures/ignored.png"></div>
<div style="decorator: image(url('../textures/frame.tga?scale=2')); background: url(data:image/png;base64,AAAA);"></div>
<a href="https://example.com/external.rml">external</a>
</body>
</rml>)";

    const auto refs = scan_rml_asset_path_references(rml, "ui/screens/main.rml", false);
    REQUIRE(refs.size() == 3);
    CHECK(refs[0].project_path == fs::path("ui/styles/menu.rcss"));
    CHECK(refs[0].reason == "RmlUi linked asset");
    CHECK(refs[1].project_path == fs::path("ui/textures/panel.png"));
    CHECK(refs[1].reason == "RmlUi source asset");
    CHECK(refs[2].project_path == fs::path("ui/textures/frame.tga"));
    CHECK(refs[2].reason == "RmlUi stylesheet resource");
}

TEST_CASE("RML rewrite preserves query fragments while rebasing a moved source document") {
    const std::string rml = R"(<rml><head><link href="../styles/menu.rcss?theme=dark#main" /></head><body><img src="./icons/play.png#normal" /></body></rml>)";
    const auto rewritten = rewrite_rml_asset_path_references(
        rml,
        "ui/screens/main.rml",
        "ui/screens/nested/main.rml",
        {},
        false
    );

    REQUIRE(rewritten.references_rewritten == 2);
    CHECK(rewritten.text.find("../../styles/menu.rcss?theme=dark#main") != std::string::npos);
    CHECK(rewritten.text.find("../icons/play.png#normal") != std::string::npos);
}

TEST_CASE("Controlled asset move rewrites RML and RCSS path dependents then refreshes cleanly") {
    TempDirectory temp("rml-target-move");
    const auto assets = temp.path() / "assets";
    fs::create_directories(assets);

    write_text_file(assets / "ui" / "main.rml",
        "<rml><head><link href=\"../styles/theme.rcss\" /></head><body><img src=\"../textures/panel.png?scale=2#hero\" /></body></rml>\n");
    write_text_file(assets / "styles" / "theme.rcss",
        "panel { decorator: image(url('../textures/panel.png#slice')); }\n");
    write_text_file(assets / "textures" / "panel.png", "not-a-real-png-but-valid-catalog-source");

    AssetCatalog catalog;
    AssetCatalogRefreshReport before{};
    std::string error;
    REQUIRE(catalog.refresh(assets, {}, &before, &error));
    CHECK(before.broken_dependencies == 0);
    const auto* texture = catalog.find("textures/panel.png");
    REQUIRE(texture != nullptr);
    const std::string texture_id = texture->asset_id;

    const auto moved = move_project_asset(catalog, texture_id, "art/ui/panel.png");
    REQUIRE_MESSAGE(moved.ok, moved.message);
    CHECK(moved.rml_references_rewritten == 2);
    CHECK(moved.rml_files_rewritten == 2);

    const auto rml = read_text_file(assets / "ui" / "main.rml");
    const auto rcss = read_text_file(assets / "styles" / "theme.rcss");
    CHECK(rml.find("../art/ui/panel.png?scale=2#hero") != std::string::npos);
    CHECK(rcss.find("../art/ui/panel.png#slice") != std::string::npos);

    AssetCatalogRefreshReport after{};
    REQUIRE(catalog.refresh(assets, {}, &after, &error));
    CHECK(after.broken_dependencies == 0);
    const auto* moved_texture = catalog.find_by_id(texture_id);
    REQUIRE(moved_texture != nullptr);
    CHECK(moved_texture->relative_path == fs::path("art/ui/panel.png"));
}

TEST_CASE("Controlled RML source move rebases its own local references") {
    TempDirectory temp("rml-source-move");
    const auto assets = temp.path() / "assets";
    fs::create_directories(assets);

    write_text_file(assets / "ui" / "main.rml",
        "<rml><head><link href=\"../styles/theme.rcss\" /></head><body><img src=\"../textures/panel.png\" /></body></rml>\n");
    write_text_file(assets / "styles" / "theme.rcss", "body { color: white; }\n");
    write_text_file(assets / "textures" / "panel.png", "catalog-source");

    AssetCatalog catalog;
    std::string error;
    REQUIRE(catalog.refresh(assets, &error));
    const auto* document = catalog.find("ui/main.rml");
    REQUIRE(document != nullptr);
    const std::string document_id = document->asset_id;

    const auto moved = move_project_asset(catalog, document_id, "screens/menu/main.rml");
    REQUIRE_MESSAGE(moved.ok, moved.message);
    CHECK(moved.rml_references_rewritten == 2);
    CHECK(moved.rml_files_rewritten == 1);

    const auto rml = read_text_file(assets / "screens" / "menu" / "main.rml");
    CHECK(rml.find("../../styles/theme.rcss") != std::string::npos);
    CHECK(rml.find("../../textures/panel.png") != std::string::npos);

    AssetCatalogRefreshReport after{};
    REQUIRE(catalog.refresh(assets, {}, &after, &error));
    CHECK(after.broken_dependencies == 0);
    const auto* moved_document = catalog.find_by_id(document_id);
    REQUIRE(moved_document != nullptr);
    CHECK(moved_document->relative_path == fs::path("screens/menu/main.rml"));
}

TEST_CASE("Controlled asset move still blocks non-RML path-only dependents") {
    TempDirectory temp("legacy-path-block");
    const auto assets = temp.path() / "assets";
    fs::create_directories(assets);

    write_text_file(assets / "textures" / "watcher.png", "catalog-source");
    write_text_file(assets / "clips" / "watcher.slspriteclip",
        "sectorline_sprite_clip 1\nname \"Watcher\"\ndirections 1\nframes_per_direction 1\nfps 4\nloop 1\ntexture \"watcher\" \"textures/watcher.png\"\nend_sprite_clip\n");

    AssetCatalog catalog;
    std::string error;
    REQUIRE(catalog.refresh(assets, &error));
    const auto* texture = catalog.find("textures/watcher.png");
    REQUIRE(texture != nullptr);

    const auto moved = move_project_asset(catalog, texture->asset_id, "art/watcher-renamed.png");
    CHECK_FALSE(moved.ok);
    CHECK(moved.message.find("path-only dependent reference") != std::string::npos);
    CHECK(fs::exists(assets / "textures" / "watcher.png"));
    CHECK_FALSE(fs::exists(assets / "art" / "watcher-renamed.png"));
}


TEST_CASE("RML dependency closure survives controlled asset moves") {
    TempDirectory temp("rml-build-closure");
    const auto assets = temp.path() / "assets";
    fs::create_directories(assets);

    write_text_file(assets / "ui" / "main.rml",
        "<rml><head><link href=\"../styles/theme.rcss\" /></head><body><img src=\"../textures/panel.png\" /></body></rml>\n");
    write_text_file(assets / "styles" / "theme.rcss",
        "panel { decorator: image(url('../textures/panel.png')); }\n");
    write_text_file(assets / "textures" / "panel.png", "catalog-source");
    write_text_file(assets / "scenes" / "main.slscene", "vespera_scene 15\nend_scene\n");

    AssetCatalog catalog;
    std::string error;
    REQUIRE(catalog.refresh(assets, &error));
    const auto* texture = catalog.find("textures/panel.png");
    REQUIRE(texture != nullptr);
    const auto moved = move_project_asset(catalog, texture->asset_id, "art/panel.png");
    REQUIRE_MESSAGE(moved.ok, moved.message);
    REQUIRE(catalog.refresh(assets, &error));

    VesperaProject project;
    project.assets_directory = assets;
    project.startup_scene = "scenes/main.slscene";
    project.startup_scene_asset_id = catalog.find("scenes/main.slscene")->asset_id;
    project.startup_ui = "ui/main.rml";
    project.startup_ui_asset_id = catalog.find("ui/main.rml")->asset_id;
    const auto manifest = build_project_asset_manifest(project, catalog);
    REQUIRE(manifest.valid());

    bool has_rml = false;
    bool has_rcss = false;
    bool has_moved_texture = false;
    for (const auto* asset : manifest.assets) {
        if (!asset) continue;
        has_rml = has_rml || asset->relative_path == fs::path("ui/main.rml");
        has_rcss = has_rcss || asset->relative_path == fs::path("styles/theme.rcss");
        has_moved_texture = has_moved_texture || asset->relative_path == fs::path("art/panel.png");
    }
    CHECK(has_rml);
    CHECK(has_rcss);
    CHECK(has_moved_texture);
}



TEST_CASE("Project startup UI stable reference repairs after a controlled RML move") {
    TempDirectory temp("startup-ui-move");
    const auto assets = temp.path() / "assets";
    write_text_file(assets / "ui" / "main.rml", "<rml><body><div id=\"root\">UI</div></body></rml>\n");

    AssetCatalog catalog;
    std::string error;
    REQUIRE(catalog.refresh(assets, &error));
    const auto* document = catalog.find("ui/main.rml");
    REQUIRE(document != nullptr);
    const std::string document_id = document->asset_id;

    VesperaProject project;
    project.assets_directory = assets;
    project.startup_ui = "ui/main.rml";
    project.startup_ui_asset_id = document_id;

    const auto moved = move_project_asset(catalog, document_id, "ui/screens/main.rml");
    REQUIRE_MESSAGE(moved.ok, moved.message);
    REQUIRE(catalog.refresh(assets, &error));

    const auto repair = repair_stable_asset_fallbacks(project, catalog);
    CHECK(repair.project_references_repaired == 1);
    CHECK(project.startup_ui == fs::path("ui/screens/main.rml"));

    const auto preflight = preflight_delete_project_asset(catalog, project, document_id);
    CHECK_FALSE(preflight.allowed);
    bool saw_startup_ui = false;
    for (const auto& blocker : preflight.blockers) {
        if (blocker.find("project startup UI") != std::string::npos) saw_startup_ui = true;
    }
    CHECK(saw_startup_ui);
}

TEST_CASE("Lua scripts are first-class assets and project build roots") {
    TempDirectory temp("lua-build-root");
    const auto assets = temp.path() / "assets";
    write_text_file(assets / "scripts" / "main.lua", "function Start() end\n");
    write_text_file(assets / "scenes" / "main.slscene", "vespera_scene 15\nend_scene\n");

    AssetCatalog catalog;
    std::string error;
    REQUIRE(catalog.refresh(assets, &error));
    const auto* lua = catalog.find("scripts/main.lua");
    REQUIRE(lua != nullptr);
    CHECK(lua->kind == AssetKind::LuaScript);
    CHECK(asset_kind_name(lua->kind) == "Lua Script");

    VesperaProject project;
    project.assets_directory = assets;
    project.startup_scene = "scenes/main.slscene";
    project.startup_scene_asset_id = catalog.find("scenes/main.slscene")->asset_id;
    project.lua_entry = "scripts/main.lua";
    project.lua_entry_asset_id = lua->asset_id;

    const auto manifest = build_project_asset_manifest(project, catalog);
    REQUIRE(manifest.valid());
    bool saw_lua = false;
    for (const auto* asset : manifest.assets) {
        if (asset && asset->relative_path == fs::path("scripts/main.lua")) saw_lua = true;
    }
    CHECK(saw_lua);
}

TEST_CASE("Project Lua entry stable reference repairs after move and blocks delete") {
    TempDirectory temp("lua-entry-move");
    const auto assets = temp.path() / "assets";
    write_text_file(assets / "scripts" / "main.lua", "function Update(dt) end\n");

    AssetCatalog catalog;
    std::string error;
    REQUIRE(catalog.refresh(assets, &error));
    const auto* lua = catalog.find("scripts/main.lua");
    REQUIRE(lua != nullptr);
    const std::string id = lua->asset_id;

    VesperaProject project;
    project.assets_directory = assets;
    project.lua_entry = "scripts/main.lua";
    project.lua_entry_asset_id = id;

    const auto moved = move_project_asset(catalog, id, "scripts/runtime/main.lua");
    REQUIRE_MESSAGE(moved.ok, moved.message);
    REQUIRE(catalog.refresh(assets, &error));
    const auto repair = repair_stable_asset_fallbacks(project, catalog);
    CHECK(repair.project_references_repaired == 1);
    CHECK(project.lua_entry == fs::path("scripts/runtime/main.lua"));

    const auto preflight = preflight_delete_project_asset(catalog, project, id);
    CHECK_FALSE(preflight.allowed);
    bool saw_lua = false;
    for (const auto& blocker : preflight.blockers) {
        if (blocker.find("project Lua entry script") != std::string::npos) saw_lua = true;
    }
    CHECK(saw_lua);
}

TEST_CASE("Controlled RML source move refuses ambiguous leading-slash references") {
    TempDirectory temp("rml-root-ref-block");
    const auto assets = temp.path() / "assets";
    fs::create_directories(assets);

    write_text_file(assets / "ui" / "main.rml",
        "<rml><body><img src=\"/textures/panel.png\" /></body></rml>\n");
    write_text_file(assets / "ui" / "textures" / "panel.png", "catalog-source");

    AssetCatalog catalog;
    std::string error;
    REQUIRE(catalog.refresh(assets, &error));
    const auto* document = catalog.find("ui/main.rml");
    REQUIRE(document != nullptr);

    const auto moved = move_project_asset(catalog, document->asset_id, "screens/main.rml");
    CHECK_FALSE(moved.ok);
    CHECK(moved.message.find("leading-slash RML/RCSS reference") != std::string::npos);
    CHECK(fs::exists(assets / "ui" / "main.rml"));
    CHECK_FALSE(fs::exists(assets / "screens" / "main.rml"));
}


TEST_CASE("Legacy UI surface exposes named semantic element operations without changing slui layout") {
    UiDocument document;
    const UiNodeId label_id = document.create_node(UiNodeType::Text, "status").id;
    document.find(label_id)->text.text = "Ready";
    const UiNodeId input_id = document.create_node(UiNodeType::TextInput, "player-name").id;
    document.find(input_id)->input.max_length = 5;
    const UiNodeId button_id = document.create_node(UiNodeType::Button, "launch").id;
    UiRuntimeState runtime;
    LegacyUiSurface surface(document, &runtime);

    CHECK(surface.backend_name() == "legacy-slui");
    CHECK(surface.exists("status"));
    CHECK_FALSE(surface.exists("missing"));
    CHECK(surface.text("status") == "Ready");
    CHECK(surface.set_text("status", "Running"));
    CHECK(document.find(label_id)->text.text == "Running");

    CHECK(surface.set_value("player-name", "GuestLong"));
    CHECK(surface.value("player-name") == "Guest");
    CHECK(document.find(input_id)->text.text == "Guest");

    CHECK(surface.focus("launch"));
    CHECK(surface.focused_id() == "launch");
    runtime.hovered = button_id;
    CHECK(surface.hovered_id() == "launch");
    CHECK(surface.click("launch"));
    CHECK(surface.consume_clicks("launch") == 1);
    CHECK(surface.consume_clicks("launch") == 0);

    CHECK(surface.set_disabled("launch", true));
    CHECK_FALSE(document.find(button_id)->button.interactable);
    CHECK_FALSE(surface.click("launch"));
    CHECK(surface.set_disabled("launch", false));
    CHECK(document.find(button_id)->button.interactable);
}

TEST_CASE("Legacy UI surface visibility clears stale interaction state") {
    UiDocument document;
    auto& button = document.create_node(UiNodeType::Button, "danger");
    UiRuntimeState runtime;
    runtime.hovered = button.id;
    runtime.pressed = button.id;
    runtime.focused = button.id;
    runtime.pointer_capture = button.id;
    runtime.pending_clicks = {button.id, button.id};
    LegacyUiSurface surface(document, &runtime);

    REQUIRE(surface.set_visible("danger", false));
    CHECK_FALSE(button.enabled);
    CHECK_FALSE(runtime.hovered.has_value());
    CHECK_FALSE(runtime.pressed.has_value());
    CHECK_FALSE(runtime.focused.has_value());
    CHECK(runtime.pointer_capture == kInvalidUiNodeId);
    CHECK(runtime.pending_clicks.empty());
    CHECK_FALSE(surface.click("danger"));
}

TEST_CASE("Legacy UI surface keeps CSS-only operations explicit instead of pretending support") {
    UiDocument document;
    document.create_node(UiNodeType::Panel, "panel");
    LegacyUiSurface surface(document);

    CHECK_FALSE(surface.set_class("panel", "warning", true));
    CHECK_FALSE(surface.set_property("panel", "display", "none"));
    CHECK_FALSE(surface.focus("panel"));
    CHECK_FALSE(surface.click("panel"));
}

TEST_CASE("UI handle table keeps script handles backend-neutral and revalidates existence") {
    UiDocument document;
    const UiNodeId first_id = document.create_node(UiNodeType::Button, "first").id;
    document.create_node(UiNodeType::Text, "second");
    UiRuntimeState runtime;
    LegacyUiSurface surface(document, &runtime);
    UiHandleTable handles;
    handles.bind(&surface);

    const std::uint64_t first = handles.find("first");
    REQUIRE(first != 0);
    CHECK(handles.find("first") == first);
    CHECK(handles.key(first) == "first");
    CHECK(handles.exists(first));
    CHECK(handles.find("missing") == 0);

    REQUIRE(document.destroy_node(first_id));
    CHECK_FALSE(handles.exists(first));
    CHECK(handles.key(first) == "first");

    handles.bind(nullptr);
    CHECK_FALSE(handles.exists(first));
    CHECK(handles.key(first).empty());
}

TEST_CASE("Legacy UI surface preserves managed semantic properties through the shared facade") {
    UiDocument document;
    const UiNodeId button_id = document.create_node(UiNodeType::Button, "go").id;
    const UiNodeId input_id = document.create_node(UiNodeType::TextInput, "name").id;
    const UiNodeId progress_id = document.create_node(UiNodeType::ProgressBar, "progress").id;
    UiRuntimeState runtime;
    LegacyUiSurface surface(document, &runtime);

    bool state = false;
    CHECK(surface.visible("go", state)); CHECK(state);
    CHECK(surface.set_visible("go", false));
    CHECK(surface.visible("go", state)); CHECK_FALSE(state);
    CHECK(surface.set_visible("go", true));

    CHECK(surface.interactable("go", state)); CHECK(state);
    CHECK(surface.set_interactable("go", false));
    CHECK(surface.interactable("go", state)); CHECK_FALSE(state);
    CHECK_FALSE(surface.click("go"));
    CHECK(surface.set_interactable("go", true));

    CHECK(surface.read_only("name", state)); CHECK_FALSE(state);
    CHECK(surface.set_read_only("name", true));
    CHECK(surface.read_only("name", state)); CHECK(state);
    CHECK(surface.set_read_only("name", false));

    CHECK(surface.set_numeric_value("progress", 2.0f));
    float number = 0.0f;
    CHECK(surface.numeric_value("progress", number));
    CHECK(number == doctest::Approx(document.find(progress_id)->progress.maximum));

    const std::array<float, 4> visual{0.1f, 0.2f, 0.3f, 0.4f};
    CHECK(surface.set_color("go", UiColorSlot::Visual, visual));
    CHECK(document.find(button_id)->visual.color == visual);

    const AssetReference image{"asset-id", "textures/test.png"};
    CHECK(surface.set_asset("go", UiAssetSlot::Image, image, "C:/ignored/by/legacy.png"));
    CHECK(document.find(button_id)->visual.image.asset_id == "asset-id");
    CHECK(document.find(button_id)->visual.image.path == fs::path("textures/test.png"));

    CHECK(surface.focus("go"));
    CHECK(surface.focused_id() == "go");
    CHECK(surface.blur("go"));
    CHECK(surface.focused_id().empty());
    CHECK(document.find(input_id) != nullptr);
}


TEST_CASE("Vespera player honors an explicit project path") {
    TempDirectory temp("player-explicit");
    const auto project_file = temp.path() / "Game.vesperaproject";
    write_text_file(project_file, "vespera_project 7\nname \"Game\"\nassets \"assets\"\nstartup_scene \"scenes/main.slscene\"\nend_project\n");

    const auto discovered = discover_player_project(temp.path() / "vespera_player.exe", project_file);
    REQUIRE_MESSAGE(discovered.ok, discovered.message);
    CHECK(discovered.project_file == fs::absolute(project_file).lexically_normal());
}

TEST_CASE("Packaged Vespera player discovers exactly one sibling project") {
    TempDirectory temp("player-discovery");
    const auto executable = temp.path() / "MyGame.exe";
    const auto project_file = temp.path() / "MyGame.vesperaproject";
    write_text_file(executable, "binary-placeholder");
    write_text_file(project_file, "vespera_project 7\nname \"Game\"\nassets \"assets\"\nstartup_scene \"scenes/main.slscene\"\nend_project\n");

    const auto discovered = discover_player_project(executable);
    REQUIRE_MESSAGE(discovered.ok, discovered.message);
    CHECK(discovered.project_file == fs::absolute(project_file).lexically_normal());
}

TEST_CASE("Packaged Vespera player rejects ambiguous sibling projects") {
    TempDirectory temp("player-ambiguous");
    const auto executable = temp.path() / "MyGame.exe";
    write_text_file(executable, "binary-placeholder");
    write_text_file(temp.path() / "A.vesperaproject", "placeholder");
    write_text_file(temp.path() / "B.vesperaproject", "placeholder");

    const auto discovered = discover_player_project(executable);
    CHECK_FALSE(discovered.ok);
    CHECK(discovered.message.find("multiple .vesperaproject") != std::string::npos);
}

TEST_CASE("Vespera player explicit startup UI wins over unrelated build roots") {
    TempDirectory temp("player-startup-ui");
    const auto assets = temp.path() / "assets";
    write_text_file(assets / "scenes" / "main.slscene", "sectorline_scene 15\ncamera 0 0 -4 0 0 75 0.05 500\nend\n");
    write_text_file(assets / "ui" / "main.rml", "<rml><body>main</body></rml>\n");
    write_text_file(assets / "ui" / "secondary.rml", "<rml><body>secondary</body></rml>\n");

    AssetCatalog catalog;
    std::string error;
    REQUIRE(catalog.refresh(assets, &error));
    const auto* main_rml = catalog.find("ui/main.rml");
    const auto* secondary = catalog.find("ui/secondary.rml");
    REQUIRE(main_rml != nullptr);
    REQUIRE(secondary != nullptr);

    VesperaProject project;
    project.assets_directory = assets;
    project.startup_ui = "ui/secondary.rml";
    project.startup_ui_asset_id = secondary->asset_id;
    project.build_includes = {"ui/main.rml"};
    project.build_include_asset_ids = {main_rml->asset_id};
    const auto selected = select_runtime_rml_document(project, catalog);
    REQUIRE(selected.document != nullptr);
    CHECK(selected.document->relative_path == fs::path("ui/secondary.rml"));
    CHECK(selected.candidate_count == 1);
    CHECK(selected.message.find("project startup_ui") != std::string::npos);
}

TEST_CASE("Vespera player keeps legacy first-RML compatibility when startup UI is absent") {
    TempDirectory temp("player-rml-legacy");
    const auto assets = temp.path() / "assets";
    write_text_file(assets / "ui" / "main.rml", "<rml><body>main</body></rml>\n");
    write_text_file(assets / "ui" / "secondary.rml", "<rml><body>secondary</body></rml>\n");

    AssetCatalog catalog;
    std::string error;
    REQUIRE(catalog.refresh(assets, &error));
    const auto* main_rml = catalog.find("ui/main.rml");
    const auto* secondary = catalog.find("ui/secondary.rml");
    REQUIRE(main_rml != nullptr);
    REQUIRE(secondary != nullptr);

    VesperaProject project;
    project.assets_directory = assets;
    project.build_includes = {"ui/main.rml", "ui/secondary.rml"};
    project.build_include_asset_ids = {main_rml->asset_id, secondary->asset_id};
    const auto selected = select_runtime_rml_document(project, catalog);
    REQUIRE(selected.document != nullptr);
    CHECK(selected.document->relative_path == fs::path("ui/main.rml"));
    CHECK(selected.candidate_count == 2);
    CHECK(selected.message.find("legacy project") != std::string::npos);
}

TEST_CASE("Scene stats report scale-relevant component counts") {
    vespera::Scene scene;
    auto& a = scene.create_entity("Sprite");
    a.add_sprite_renderer();
    a.add_cylinder_collider().is_trigger = true;
    a.add_managed_script("Game.A");
    auto& b = scene.create_entity("Mesh");
    b.add_mesh_renderer();
    b.add_point_light();
    b.add_managed_script("Game.B");
    b.add_managed_script("Game.C");
    b.enabled = false;

    const auto stats = vespera::collect_scene_stats(scene);
    CHECK(stats.sectors == 0);
    CHECK(stats.entities == 2);
    CHECK(stats.enabled_entities == 1);
    CHECK(stats.sprite_renderers == 1);
    CHECK(stats.mesh_renderers == 1);
    CHECK(stats.colliders == 1);
    CHECK(stats.triggers == 1);
    CHECK(stats.point_lights == 1);
    CHECK(stats.managed_scripts == 3);
}

TEST_CASE("Runtime performance counters smooth and retain worst frame") {
    vespera::RuntimePerformanceCounters perf;
    perf.record_frame(10.0, 2.0, 5.0);
    perf.record_frame(30.0, 3.0, 20.0);
    CHECK(perf.frame_index == 2);
    CHECK(perf.last_frame_ms == doctest::Approx(30.0));
    CHECK(perf.max_frame_ms == doctest::Approx(30.0));
    CHECK(perf.smoothed_frame_ms == doctest::Approx(12.0));
    CHECK(perf.update_ms == doctest::Approx(3.0));
    CHECK(perf.render_ms == doctest::Approx(20.0));
    perf.render.mesh_draw_calls = 4;
    perf.render.sprite_draw_calls = 1;
    perf.render.world_draw_calls = 1;
    perf.render.ui_draw_calls = 2;
    CHECK(perf.render.total_draw_calls() == 8);
}

TEST_CASE("Emberlight Guild sample is a self-contained shared-player project") {
    const auto root = fs::path(VESPERA_SOURCE_DIR) / "examples" / "emberlight_guild";
    VesperaProject project;
    const auto loaded = load_vespera_project(project, root / "EmberlightGuild.vesperaproject");
    REQUIRE_MESSAGE(loaded.ok, loaded.message);
    CHECK(project.name == "Emberlight Guild");
    CHECK(project.game_target.empty());
    CHECK(project.managed_project == fs::path("managed/EmberlightGuild.Scripts.csproj"));
    CHECK(project.managed_assembly == "EmberlightGuild.Scripts");

    const auto issues = validate_vespera_project(project);
    std::size_t errors = 0;
    for (const auto& issue : issues)
        if (issue.severity == ProjectValidationSeverity::Error) ++errors;
    CHECK(errors == 0);

    AssetCatalog catalog;
    AssetCatalogRefreshOptions refresh_options;
    refresh_options.write_metadata = false;
    AssetCatalogRefreshReport refresh_report;
    std::string error;
    REQUIRE_MESSAGE(catalog.refresh(project.assets_root(), refresh_options, &refresh_report, &error), error);
    const auto manifest = build_project_asset_manifest(project, catalog);
    REQUIRE(manifest.valid());

    bool saw_scene = false;
    bool saw_rml = false;
    bool saw_rcss = false;
    for (const auto* asset : manifest.assets) {
        if (!asset) continue;
        saw_scene = saw_scene || asset->relative_path == fs::path("scenes/main.slscene");
        saw_rml = saw_rml || asset->relative_path == fs::path("ui/main.rml");
        saw_rcss = saw_rcss || asset->relative_path == fs::path("ui/guild.rcss");
    }
    CHECK(saw_scene);
    CHECK(saw_rml);
    CHECK(saw_rcss);

    const auto selected = select_runtime_rml_document(project, catalog);
    REQUIRE(selected.document != nullptr);
    CHECK(selected.document->relative_path == fs::path("ui/main.rml"));
}


TEST_CASE("Performance Lab sample is a normal shared-player benchmark project") {
    const auto root = fs::path(VESPERA_SOURCE_DIR) / "examples" / "performance_lab";
    VesperaProject project;
    const auto loaded = load_vespera_project(project, root / "VesperaPerformanceLab.vesperaproject");
    REQUIRE_MESSAGE(loaded.ok, loaded.message);
    CHECK(project.name == "Vespera Performance Lab");
    CHECK(project.game_target.empty());
    CHECK(project.managed_project == fs::path("managed/VesperaPerformanceLab.Scripts.csproj"));
    CHECK(project.managed_assembly == "VesperaPerformanceLab.Scripts");
    CHECK(project.startup_ui == fs::path("ui/main.rml"));

    const auto issues = validate_vespera_project(project);
    std::size_t errors = 0;
    for (const auto& issue : issues)
        if (issue.severity == ProjectValidationSeverity::Error) ++errors;
    CHECK(errors == 0);

    AssetCatalog catalog;
    AssetCatalogRefreshOptions refresh_options;
    refresh_options.write_metadata = false;
    AssetCatalogRefreshReport refresh_report;
    std::string error;
    REQUIRE_MESSAGE(catalog.refresh(project.assets_root(), refresh_options, &refresh_report, &error), error);
    const auto manifest = build_project_asset_manifest(project, catalog);
    REQUIRE(manifest.valid());

    bool saw_scene = false;
    bool saw_rml = false;
    bool saw_rcss = false;
    for (const auto* asset : manifest.assets) {
        if (!asset) continue;
        saw_scene = saw_scene || asset->relative_path == fs::path("scenes/main.slscene");
        saw_rml = saw_rml || asset->relative_path == fs::path("ui/main.rml");
        saw_rcss = saw_rcss || asset->relative_path == fs::path("ui/performance.rcss");
    }
    CHECK(saw_scene);
    CHECK(saw_rml);
    CHECK(saw_rcss);

    const auto scene_text = read_text_file(root / "assets" / "scenes" / "main.slscene");
    CHECK(scene_text.find("PerformanceLabController") != std::string::npos);
    CHECK(scene_text.find("Performance Gallery") != std::string::npos);
    const auto script = read_text_file(root / "managed" / "PerformanceLab.cs");
    CHECK(script.find("BuildSwarm(Preset.Torture, 7000, 5300") != std::string::npos);
    CHECK(script.find("Camera.State") != std::string::npos);
    CHECK(script.find("Run Full Benchmark") != std::string::npos);
    CHECK(script.find("1%low") != std::string::npos);
    CHECK(script.find("Transform.SetBatch") != std::string::npos);
    CHECK(script.find("Performance.TryGetSnapshot") != std::string::npos);
    CHECK(script.find("benchmark-results") != std::string::npos);
    const auto rml = read_text_file(root / "assets" / "ui" / "main.rml");
    CHECK(rml.find("update-ms") != std::string::npos);
    CHECK(rml.find("draw-calls") != std::string::npos);
}
