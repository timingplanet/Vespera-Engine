#include "editor_automation_values.hpp"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <format>
#include <string>

namespace vespera::editor {

std::string automation_json_escape(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const unsigned char c : value) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) out += std::format("\\u{:04x}", static_cast<unsigned int>(c));
                else out.push_back(static_cast<char>(c));
                break;
        }
    }
    return out;
}
const std::string* automation_arg(const vespera::editor::AutomationRequest& request, std::string_view key) {
    const auto it = request.args.find(std::string(key));
    return it == request.args.end() ? nullptr : &it->second;
}
std::optional<std::uint64_t> automation_u64(std::string_view value) {
    std::uint64_t out = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), out);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) return std::nullopt;
    return out;
}
std::optional<int> automation_int(std::string_view value) {
    int out = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), out);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) return std::nullopt;
    return out;
}
std::optional<vespera::Vec3> automation_vec3(std::string_view value) {
    vespera::Vec3 result{};
    float* fields[] = {&result.x, &result.y, &result.z};
    std::size_t start = 0;
    for (int i = 0; i < 3; ++i) {
        const std::size_t comma = value.find(',', start);
        const std::string_view token = i < 2
            ? (comma == std::string_view::npos ? std::string_view{} : value.substr(start, comma - start))
            : value.substr(start);
        if (token.empty()) return std::nullopt;
        std::string temp(token);
        char* end = nullptr;
        const float parsed = std::strtof(temp.c_str(), &end);
        if (!end || *end != '\0' || !std::isfinite(parsed)) return std::nullopt;
        *fields[i] = parsed;
        if (i < 2) start = comma + 1;
    }
    return result;
}
std::optional<bool> automation_bool(std::string_view value) {
    if (value == "true" || value == "1") return true;
    if (value == "false" || value == "0") return false;
    return std::nullopt;
}
std::optional<float> automation_float(std::string_view value) {
    std::string temp(value);
    char* end = nullptr;
    const float parsed = std::strtof(temp.c_str(), &end);
    if (!end || *end != '\0' || !std::isfinite(parsed)) return std::nullopt;
    return parsed;
}
std::optional<vespera::Vec2> automation_vec2(std::string_view value) {
    const std::size_t comma = value.find(',');
    if (comma == std::string_view::npos) return std::nullopt;
    const auto x = automation_float(value.substr(0, comma));
    const auto z = automation_float(value.substr(comma + 1));
    if (!x || !z) return std::nullopt;
    return vespera::Vec2{*x, *z};
}
std::optional<std::array<float, 4>> automation_color4(std::string_view value) {
    std::array<float, 4> result{};
    std::size_t start = 0;
    for (int i = 0; i < 4; ++i) {
        const std::size_t comma = value.find(',', start);
        const std::string_view token = i < 3
            ? (comma == std::string_view::npos ? std::string_view{} : value.substr(start, comma - start))
            : value.substr(start);
        if (token.empty()) return std::nullopt;
        const auto parsed = automation_float(token);
        if (!parsed) return std::nullopt;
        result[static_cast<std::size_t>(i)] = *parsed;
        if (i < 3) start = comma + 1;
    }
    return result;
}
std::optional<vespera::UiNodeType> automation_ui_node_type(std::string_view value) {
    if (value == "canvas") return vespera::UiNodeType::Canvas;
    if (value == "panel") return vespera::UiNodeType::Panel;
    if (value == "text") return vespera::UiNodeType::Text;
    if (value == "image") return vespera::UiNodeType::Image;
    if (value == "button") return vespera::UiNodeType::Button;
    if (value == "progress" || value == "progress_bar") return vespera::UiNodeType::ProgressBar;
    if (value == "scroll" || value == "scroll_view") return vespera::UiNodeType::ScrollView;
    if (value == "list") return vespera::UiNodeType::List;
    if (value == "grid") return vespera::UiNodeType::Grid;
    if (value == "tabs") return vespera::UiNodeType::Tabs;
    if (value == "modal") return vespera::UiNodeType::Modal;
    if (value == "tooltip") return vespera::UiNodeType::Tooltip;
    if (value == "text_input") return vespera::UiNodeType::TextInput;
    return std::nullopt;
}
std::optional<vespera::UiLayoutMode> automation_ui_layout_mode(std::string_view value) {
    if (value == "none") return vespera::UiLayoutMode::None;
    if (value == "horizontal") return vespera::UiLayoutMode::Horizontal;
    if (value == "vertical") return vespera::UiLayoutMode::Vertical;
    if (value == "grid") return vespera::UiLayoutMode::Grid;
    return std::nullopt;
}
std::optional<vespera::UiImageFit> automation_ui_image_fit(std::string_view value) {
    if (value == "stretch") return vespera::UiImageFit::Stretch;
    if (value == "contain") return vespera::UiImageFit::Contain;
    if (value == "cover") return vespera::UiImageFit::Cover;
    return std::nullopt;
}
std::optional<vespera::UiHorizontalAlignment> automation_ui_horizontal_alignment(std::string_view value) {
    if (value == "left") return vespera::UiHorizontalAlignment::Left;
    if (value == "center") return vespera::UiHorizontalAlignment::Center;
    if (value == "right") return vespera::UiHorizontalAlignment::Right;
    return std::nullopt;
}
std::optional<vespera::UiVerticalAlignment> automation_ui_vertical_alignment(std::string_view value) {
    if (value == "top") return vespera::UiVerticalAlignment::Top;
    if (value == "middle") return vespera::UiVerticalAlignment::Middle;
    if (value == "bottom") return vespera::UiVerticalAlignment::Bottom;
    return std::nullopt;
}
const vespera::BuiltinPropertyInfo* automation_property_info(
    std::string_view component_key,
    std::string_view property_key
) {
    const auto* component = vespera::builtin_component_info(component_key);
    if (!component) return nullptr;
    for (const auto& property : vespera::builtin_component_properties(component->type)) {
        if (property.key == property_key) return &property;
    }
    return nullptr;
}
std::optional<vespera::BuiltinPropertyValue> automation_parse_property_value(
    vespera::BuiltinPropertyType type,
    std::string_view value
) {
    switch (type) {
        case vespera::BuiltinPropertyType::Bool: {
            const auto parsed = automation_bool(value);
            if (parsed) return vespera::BuiltinPropertyValue{*parsed};
            break;
        }
        case vespera::BuiltinPropertyType::Float: {
            const auto parsed = automation_float(value);
            if (parsed) return vespera::BuiltinPropertyValue{*parsed};
            break;
        }
        case vespera::BuiltinPropertyType::String:
            return vespera::BuiltinPropertyValue{std::string(value)};
        case vespera::BuiltinPropertyType::Vec2: {
            const auto parsed = automation_vec2(value);
            if (parsed) return vespera::BuiltinPropertyValue{*parsed};
            break;
        }
        case vespera::BuiltinPropertyType::Vec3: {
            const auto parsed = automation_vec3(value);
            if (parsed) return vespera::BuiltinPropertyValue{*parsed};
            break;
        }
        case vespera::BuiltinPropertyType::Color4: {
            const auto parsed = automation_color4(value);
            if (parsed) return vespera::BuiltinPropertyValue{*parsed};
            break;
        }
        case vespera::BuiltinPropertyType::Texture: {
            const auto parsed = automation_u64(value);
            if (parsed && *parsed <= std::numeric_limits<vespera::TextureId>::max()) {
                return vespera::BuiltinPropertyValue{static_cast<vespera::TextureId>(*parsed)};
            }
            break;
        }
    }
    return std::nullopt;
}
std::string automation_property_json(const vespera::BuiltinPropertyValue& value) {
    return std::visit([](const auto& typed) -> std::string {
        using T = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<T, bool>) {
            return typed ? "true" : "false";
        } else if constexpr (std::is_same_v<T, float>) {
            return std::format("{}", typed);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return std::format("\"{}\"", automation_json_escape(typed));
        } else if constexpr (std::is_same_v<T, vespera::Vec2>) {
            return std::format("[{},{}]", typed.x, typed.z);
        } else if constexpr (std::is_same_v<T, vespera::Vec3>) {
            return std::format("[{},{},{}]", typed.x, typed.y, typed.z);
        } else if constexpr (std::is_same_v<T, std::array<float, 4>>) {
            return std::format("[{},{},{},{}]", typed[0], typed[1], typed[2], typed[3]);
        } else if constexpr (std::is_same_v<T, vespera::TextureId>) {
            return std::format("{}", typed);
        } else {
            return "null";
        }
    }, value);
}

} // namespace vespera::editor
