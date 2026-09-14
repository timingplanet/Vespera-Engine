#include <vespera/assets/material_asset.hpp>

#include <vespera/assets/asset_catalog.hpp>
#include <vespera/scene/scene.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace vespera {
namespace {

constexpr std::string_view kNoReference = "-";

std::string canonical_name(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (unsigned char c : value) {
        if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out.push_back(static_cast<char>(c));
    }
    return out;
}

TextureId find_world_texture(const SectorWorld& world, const AssetRecord& texture_asset) {
    const auto target = canonical_name(texture_asset.display_name);
    const auto fallback = canonical_name(texture_asset.relative_path.stem().string());
    const auto& textures = world.textures();
    for (std::size_t i = 0; i < textures.size(); ++i) {
        const auto current = canonical_name(textures[i].name);
        if (current == target || current == fallback) return static_cast<TextureId>(i);
    }
    return kInvalidTexture;
}

MaterialAssetIoResult fail(std::string message) {
    MaterialAssetIoResult out;
    out.message = std::move(message);
    return out;
}

} // namespace

MaterialAssetIoResult load_material_asset(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) return fail("could not open material asset: " + path.string());

    MaterialAsset material;
    bool saw_header = false;
    bool saw_end = false;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos || line[first] == '#') continue;
        std::istringstream stream(line.substr(first));
        std::string command;
        stream >> command;

        if (!saw_header) {
            if (command != "vespera_material" || !(stream >> material.version) || material.version != 1) {
                return fail("invalid material header at line " + std::to_string(line_number));
            }
            saw_header = true;
            continue;
        }

        if (command == "name") {
            if (!(stream >> std::quoted(material.name))) return fail("invalid material name at line " + std::to_string(line_number));
        } else if (command == "shader") {
            std::string shader;
            if (!(stream >> std::quoted(shader))) return fail("invalid material shader at line " + std::to_string(line_number));
            const auto parsed = builtin_material_shader_from_name(shader);
            if (!parsed) return fail("unknown material shader '" + shader + "'");
            material.properties.shader = *parsed;
        } else if (command == "base_texture_asset") {
            std::string id, fallback_path;
            if (!(stream >> std::quoted(id) >> std::quoted(fallback_path))) return fail("invalid base texture reference at line " + std::to_string(line_number));
            material.base_texture.asset_id = id == kNoReference ? std::string{} : id;
            material.base_texture.path = fallback_path == kNoReference ? std::filesystem::path{} : std::filesystem::path(fallback_path);
        } else if (command == "base_color") {
            if (!(stream >> material.properties.base_color[0] >> material.properties.base_color[1]
                >> material.properties.base_color[2] >> material.properties.base_color[3])) {
                return fail("invalid base_color at line " + std::to_string(line_number));
            }
        } else if (command == "emission") {
            if (!(stream >> material.properties.emission_color[0] >> material.properties.emission_color[1]
                >> material.properties.emission_color[2] >> material.properties.emission_strength)) {
                return fail("invalid emission at line " + std::to_string(line_number));
            }
        } else if (command == "alpha_cutoff") {
            if (!(stream >> material.properties.alpha_cutoff)) return fail("invalid alpha_cutoff at line " + std::to_string(line_number));
        } else if (command == "end") {
            saw_end = true;
            break;
        } else {
            return fail("unknown material command '" + command + "' at line " + std::to_string(line_number));
        }
    }

    if (!saw_header || !saw_end) return fail("material asset is incomplete: " + path.string());
    if (material.name.empty()) material.name = path.stem().string();
    material.properties.alpha_cutoff = std::clamp(material.properties.alpha_cutoff, 0.0f, 1.0f);
    material.properties.emission_strength = std::max(material.properties.emission_strength, 0.0f);

    MaterialAssetIoResult result;
    result.ok = true;
    result.material = std::move(material);
    result.message = "loaded material asset";
    return result;
}

MaterialAssetIoResult save_material_asset(const std::filesystem::path& path, const MaterialAsset& material) {
    std::error_code ec;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return fail("could not create material directory: " + ec.message());

    const auto temporary = std::filesystem::path(path.string() + ".tmp");
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) return fail("could not write material asset: " + path.string());
    output << "vespera_material 1\n";
    output << "name " << std::quoted(material.name.empty() ? path.stem().string() : material.name) << "\n";
    output << "shader " << std::quoted(std::string(builtin_material_shader_name(material.properties.shader))) << "\n";
    output << "base_texture_asset "
        << std::quoted(material.base_texture.asset_id.empty() ? std::string(kNoReference) : material.base_texture.asset_id) << ' '
        << std::quoted(material.base_texture.path.empty() ? std::string(kNoReference) : material.base_texture.path.generic_string()) << "\n";
    output << "base_color " << material.properties.base_color[0] << ' ' << material.properties.base_color[1] << ' '
        << material.properties.base_color[2] << ' ' << material.properties.base_color[3] << "\n";
    output << "emission " << material.properties.emission_color[0] << ' ' << material.properties.emission_color[1] << ' '
        << material.properties.emission_color[2] << ' ' << material.properties.emission_strength << "\n";
    output << "alpha_cutoff " << std::clamp(material.properties.alpha_cutoff, 0.0f, 1.0f) << "\n";
    output << "end\n";
    output.close();
    if (!output) return fail("failed while writing material asset: " + path.string());

    const auto backup = std::filesystem::path(path.string() + ".vespera_bak");
    bool had_original = std::filesystem::exists(path, ec);
    if (ec) {
        std::filesystem::remove(temporary);
        return fail("could not inspect existing material asset: " + ec.message());
    }

    if (had_original) {
        std::filesystem::remove(backup, ec);
        if (ec) {
            std::filesystem::remove(temporary);
            return fail("could not clear material backup path: " + ec.message());
        }
        std::filesystem::rename(path, backup, ec);
        if (ec) {
            std::filesystem::remove(temporary);
            return fail("could not stage existing material asset for replacement: " + ec.message());
        }
    }

    ec.clear();
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        const auto replace_error = ec.message();
        std::error_code restore_ec;
        if (had_original) std::filesystem::rename(backup, path, restore_ec);
        std::filesystem::remove(temporary);
        if (had_original && restore_ec) {
            return fail("could not replace material asset (" + replace_error
                + ") and backup restore also failed: " + restore_ec.message());
        }
        return fail("could not replace material asset: " + replace_error);
    }

    std::string cleanup_warning;
    if (had_original) {
        std::filesystem::remove(backup, ec);
        if (ec) cleanup_warning = " (warning: old backup could not be removed: " + ec.message() + ")";
    }

    MaterialAssetIoResult result;
    result.ok = true;
    result.material = material;
    result.message = "saved material asset" + cleanup_warning;
    return result;
}

MaterialHydrationReport hydrate_scene_materials(Scene& scene, const AssetCatalog& catalog) {
    MaterialHydrationReport report;
    std::ostringstream warnings;
    for (auto& entity : scene.entities) {
        if (!entity.mesh_renderer) continue;
        auto& mesh = *entity.mesh_renderer;
        mesh.material_resolved = false;
        mesh.resolved_material_texture = kInvalidTexture;
        mesh.resolved_material = {};
        if (mesh.material.empty()) continue;

        const auto resolution = catalog.resolve_reference(mesh.material);
        if (!resolution || resolution.record->kind != AssetKind::Material) {
            ++report.missing_materials;
            warnings << "material reference on entity '" << entity.name << "' could not be resolved; ";
            continue;
        }
        const auto loaded = load_material_asset(resolution.record->absolute_path);
        if (!loaded) {
            ++report.missing_materials;
            warnings << "material '" << resolution.record->relative_path.generic_string() << "' could not be loaded; ";
            continue;
        }

        mesh.resolved_material = loaded.material.properties;
        if (!loaded.material.base_texture.empty()) {
            const auto texture_resolution = catalog.resolve_reference(loaded.material.base_texture);
            if (!texture_resolution || texture_resolution.record->kind != AssetKind::Texture) {
                ++report.missing_textures;
            } else {
                mesh.resolved_material_texture = find_world_texture(scene.world, *texture_resolution.record);
                if (mesh.resolved_material_texture == kInvalidTexture) ++report.missing_textures;
            }
        }
        mesh.material_resolved = true;
        ++report.resolved;
    }
    report.message = warnings.str();
    return report;
}

} // namespace vespera
