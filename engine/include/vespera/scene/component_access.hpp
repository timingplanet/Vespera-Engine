#pragma once

#include <vespera/scene/scene.hpp>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace vespera {

// Generic value surface for the built-in reflected property layer. This is for
// tooling/bindings/automation, not a replacement for direct native component
// access in performance-sensitive C++ gameplay code.
using BuiltinPropertyValue = std::variant<
    bool,
    float,
    std::string,
    Vec2,
    Vec3,
    std::array<float, 4>,
    TextureId
>;

[[nodiscard]] std::optional<BuiltinPropertyValue> get_builtin_component_property(
    const Entity& entity,
    BuiltinComponentType component,
    std::string_view property_key
);

[[nodiscard]] std::optional<BuiltinPropertyValue> get_builtin_component_property(
    const Entity& entity,
    std::string_view component_key,
    std::string_view property_key
);

// Returns false for an unknown property/component, a missing optional
// component, a value with the wrong type, or a value that violates a basic
// component invariant (for example non-positive collider radius).
bool set_builtin_component_property(
    Entity& entity,
    BuiltinComponentType component,
    std::string_view property_key,
    const BuiltinPropertyValue& value
);

bool set_builtin_component_property(
    Entity& entity,
    std::string_view component_key,
    std::string_view property_key,
    const BuiltinPropertyValue& value
);

} // namespace vespera
