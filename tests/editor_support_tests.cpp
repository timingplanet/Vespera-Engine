#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../editor/editor_automation_values.hpp"
#include "../editor/editor_installation.hpp"

#include <array>
#include <string>

TEST_CASE("automation scalar parsing is strict") {
    using namespace vespera::editor;

    CHECK(automation_u64("42") == 42u);
    CHECK_FALSE(automation_u64("-1").has_value());
    CHECK_FALSE(automation_u64("42x").has_value());

    CHECK(automation_int("-7") == -7);
    CHECK_FALSE(automation_int("7.0").has_value());

    CHECK(automation_bool("true") == true);
    CHECK(automation_bool("0") == false);
    CHECK_FALSE(automation_bool("yes").has_value());

    const auto parsed_float = automation_float("1.25");
    REQUIRE(parsed_float.has_value());
    CHECK(*parsed_float == doctest::Approx(1.25f));
    CHECK_FALSE(automation_float("nan").has_value());
    CHECK_FALSE(automation_float("inf").has_value());
    CHECK_FALSE(automation_float("1.0tail").has_value());
}

TEST_CASE("automation vector and color parsing validates arity") {
    using namespace vespera::editor;

    const auto vec2 = automation_vec2("1.5,-2.0");
    REQUIRE(vec2.has_value());
    CHECK(vec2->x == doctest::Approx(1.5f));
    CHECK(vec2->z == doctest::Approx(-2.0f));
    CHECK_FALSE(automation_vec2("1.5").has_value());

    const auto vec3 = automation_vec3("1,2,3");
    REQUIRE(vec3.has_value());
    CHECK(vec3->x == doctest::Approx(1.0f));
    CHECK(vec3->y == doctest::Approx(2.0f));
    CHECK(vec3->z == doctest::Approx(3.0f));
    CHECK_FALSE(automation_vec3("1,2").has_value());
    CHECK_FALSE(automation_vec3("1,2,3,4").has_value());

    const auto color = automation_color4("0.1,0.2,0.3,1");
    REQUIRE(color.has_value());
    CHECK((*color)[0] == doctest::Approx(0.1f));
    CHECK((*color)[3] == doctest::Approx(1.0f));
    CHECK_FALSE(automation_color4("0.1,0.2,0.3").has_value());
}

TEST_CASE("automation JSON escaping preserves protocol safety") {
    using namespace vespera::editor;
    CHECK(automation_json_escape("a\\b\"c\n\t") == "a\\\\b\\\"c\\n\\t");
    const std::string control(1, static_cast<char>(0x01));
    CHECK(automation_json_escape(control) == "\\u0001");
}

TEST_CASE("automation request arguments are non-mutating lookups") {
    vespera::editor::AutomationRequest request;
    request.args["name"] = "Watcher";
    const auto* found = vespera::editor::automation_arg(request, "name");
    REQUIRE(found != nullptr);
    CHECK(*found == "Watcher");
    CHECK(vespera::editor::automation_arg(request, "missing") == nullptr);
}

TEST_CASE("automation UI enum parsing rejects unknown values") {
    using namespace vespera::editor;
    CHECK(automation_ui_node_type("button") == vespera::UiNodeType::Button);
    CHECK(automation_ui_layout_mode("vertical") == vespera::UiLayoutMode::Vertical);
    CHECK(automation_ui_image_fit("cover") == vespera::UiImageFit::Cover);
    CHECK(automation_ui_horizontal_alignment("center") == vespera::UiHorizontalAlignment::Center);
    CHECK(automation_ui_vertical_alignment("bottom") == vespera::UiVerticalAlignment::Bottom);
    CHECK_FALSE(automation_ui_node_type("spaceship").has_value());
}

TEST_CASE("automation builtin property conversion follows component metadata") {
    using namespace vespera::editor;
    const auto* info = automation_property_info("sectorline.transform", "position");
    REQUIRE(info != nullptr);
    CHECK(info->type == vespera::BuiltinPropertyType::Vec3);

    const auto parsed = automation_parse_property_value(info->type, "1,2,3");
    REQUIRE(parsed.has_value());
    CHECK(automation_property_json(*parsed) == "[1,2,3]");
    CHECK_FALSE(automation_parse_property_value(info->type, "1,2").has_value());
}

TEST_CASE("builder configuration fallback keeps requested config first") {
    using vespera::editor::editor_builder_configuration_fallbacks;

    const auto release = editor_builder_configuration_fallbacks("Release");
    CHECK(release[0] == "Release");
    CHECK(release[1] == "Development");
    CHECK(release[2] == "Debug");

    const auto development = editor_builder_configuration_fallbacks("Development");
    CHECK(development[0] == "Development");
    CHECK(development[1] == "Release");
    CHECK(development[2] == "Debug");

    const auto debug = editor_builder_configuration_fallbacks("Debug");
    CHECK(debug[0] == "Debug");
    CHECK(debug[1] == "Development");
    CHECK(debug[2] == "Release");
}
