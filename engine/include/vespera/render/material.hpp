#pragma once

#include <array>
#include <optional>
#include <string_view>

namespace vespera {

enum class BuiltinMaterialShader {
    Lit,
    Unlit,
};

[[nodiscard]] inline constexpr std::string_view builtin_material_shader_name(BuiltinMaterialShader shader) {
    switch (shader) {
        case BuiltinMaterialShader::Lit: return "Vespera/Lit";
        case BuiltinMaterialShader::Unlit: return "Vespera/Unlit";
    }
    return "Vespera/Lit";
}

[[nodiscard]] inline constexpr std::optional<BuiltinMaterialShader> builtin_material_shader_from_name(std::string_view name) {
    if (name == "Vespera/Lit" || name == "lit") return BuiltinMaterialShader::Lit;
    if (name == "Vespera/Unlit" || name == "unlit") return BuiltinMaterialShader::Unlit;
    return std::nullopt;
}

// Renderer-independent material parameters shared by authored .slmat assets,
// scene component caches, D3D12 today and Vulkan later. No descriptor handles,
// root parameters, shader bytecode or backend state may be persisted here.
struct MaterialProperties {
    BuiltinMaterialShader shader = BuiltinMaterialShader::Lit;
    std::array<float, 4> base_color{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 3> emission_color{1.0f, 1.0f, 1.0f};
    float emission_strength = 0.0f;
    float alpha_cutoff = 0.5f;
};

} // namespace vespera
