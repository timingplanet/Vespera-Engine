#pragma once

#include "automation_protocol.hpp"

#include <vespera/scene/component_access.hpp>
#include <vespera/scene/scene.hpp>
#include <vespera/ui/ui.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace vespera::editor {

std::string automation_json_escape(std::string_view value);
const std::string* automation_arg(const AutomationRequest& request, std::string_view key);
std::optional<std::uint64_t> automation_u64(std::string_view value);
std::optional<int> automation_int(std::string_view value);
std::optional<vespera::Vec3> automation_vec3(std::string_view value);
std::optional<bool> automation_bool(std::string_view value);
std::optional<float> automation_float(std::string_view value);
std::optional<vespera::Vec2> automation_vec2(std::string_view value);
std::optional<std::array<float, 4>> automation_color4(std::string_view value);
std::optional<vespera::UiNodeType> automation_ui_node_type(std::string_view value);
std::optional<vespera::UiLayoutMode> automation_ui_layout_mode(std::string_view value);
std::optional<vespera::UiImageFit> automation_ui_image_fit(std::string_view value);
std::optional<vespera::UiHorizontalAlignment> automation_ui_horizontal_alignment(std::string_view value);
std::optional<vespera::UiVerticalAlignment> automation_ui_vertical_alignment(std::string_view value);
const vespera::BuiltinPropertyInfo* automation_property_info(std::string_view component_key, std::string_view property_key);
std::optional<vespera::BuiltinPropertyValue> automation_parse_property_value(vespera::BuiltinPropertyType type, std::string_view value);
std::string automation_property_json(const vespera::BuiltinPropertyValue& value);

} // namespace vespera::editor
