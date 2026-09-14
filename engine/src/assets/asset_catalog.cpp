#include <vespera/assets/asset_catalog.hpp>
#include <vespera/assets/rml_asset_references.hpp>
#include <functional>
#include <utility>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <random>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace vespera {
namespace {

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        return static_cast<char>(c);
    });
    return value;
}

std::string importer_for(AssetKind kind, const std::filesystem::path& path) {
    const auto ext = lowercase(path.extension().string());
    switch (kind) {
        case AssetKind::Scene: return "vespera.scene";
        case AssetKind::EntityPrefab: return "vespera.prefab";
        case AssetKind::Audio:
            if (ext == ".wav") return "vespera.audio.wav";
            if (ext == ".ogg") return "vespera.audio.ogg.pending";
            if (ext == ".mp3") return "vespera.audio.mp3.pending";
            return "vespera.audio.pending";
        case AssetKind::Font:
            if (ext == ".ttf") return "vespera.font.ttf";
            if (ext == ".otf") return "vespera.font.otf";
            return "vespera.font.pending";
        case AssetKind::SpriteClip: return "vespera.sprite_clip";
        case AssetKind::SpriteSheet: return "vespera.sprite_sheet";
        case AssetKind::AudioClip: return "vespera.audio_clip";
        case AssetKind::Material: return "vespera.material";
        case AssetKind::UiDocument: return "vespera.ui";
        case AssetKind::RmlDocument: return "vespera.ui.rml";
        case AssetKind::RmlStyleSheet: return "vespera.ui.rcss";
        case AssetKind::LuaScript: return "vespera.script.lua";
        case AssetKind::Texture:
            if (ext == ".bmp") return "vespera.texture.bmp";
            if (ext == ".tga") return "vespera.texture.tga";
            if (ext == ".png") return "vespera.texture.png";
            if (ext == ".jpg" || ext == ".jpeg") return "vespera.texture.jpeg";
            return "vespera.texture.pending";
    }
    return "vespera.unknown";
}

std::string hash_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::uint64_t hash = 1469598103934665603ull;
    std::array<char, 64 * 1024> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        for (std::streamsize i = 0; i < count; ++i) {
            hash ^= static_cast<std::uint8_t>(buffer[static_cast<std::size_t>(i)]);
            hash *= 1099511628211ull;
        }
    }
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << hash;
    return stream.str();
}

std::int64_t source_mtime(const std::filesystem::path& path) {
    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(path, ec);
    if (ec) return 0;
    return static_cast<std::int64_t>(stamp.time_since_epoch().count());
}

std::string make_asset_id(const std::filesystem::path& relative_path) {
    // IDs are persisted immediately into .vmeta. Random entropy means a rename
    // keeps identity when the sidecar moves with the source asset.
    std::random_device rd;
    const auto now = static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const std::uint64_t seed_a = (static_cast<std::uint64_t>(rd()) << 32u) ^ rd() ^ now;
    const std::uint64_t seed_b = (static_cast<std::uint64_t>(rd()) << 32u) ^ rd()
        ^ static_cast<std::uint64_t>(std::hash<std::string>{}(relative_path.generic_string()));
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << seed_a << std::setw(16) << seed_b;
    return stream.str();
}

struct AssetMetadata {
    bool valid = false;
    int version = 0;
    std::string id;
    std::string importer;
    std::uintmax_t source_size = 0;
    std::string source_hash;
    std::int64_t source_mtime = 0;
    TextureImportSettings texture_settings{};
    bool texture_settings_authored = false;
};

std::string token_lower(std::string value) { return lowercase(std::move(value)); }

TextureUsage parse_texture_usage(std::string value) {
    value = token_lower(std::move(value));
    if (value == "sprite") return TextureUsage::Sprite;
    if (value == "ui") return TextureUsage::UI;
    if (value == "data") return TextureUsage::Data;
    return TextureUsage::World;
}
TextureFilter parse_texture_filter(std::string value) { return token_lower(std::move(value)) == "linear" ? TextureFilter::Linear : TextureFilter::Nearest; }
TextureWrap parse_texture_wrap(std::string value) { return token_lower(std::move(value)) == "clamp" ? TextureWrap::Clamp : TextureWrap::Repeat; }
TextureColorSpace parse_texture_color_space(std::string value) { return token_lower(std::move(value)) == "linear" ? TextureColorSpace::Linear : TextureColorSpace::SRGB; }
TextureAlphaMode parse_texture_alpha(std::string value) {
    value = token_lower(std::move(value));
    if (value == "opaque") return TextureAlphaMode::Opaque;
    if (value == "cutout") return TextureAlphaMode::Cutout;
    if (value == "blend") return TextureAlphaMode::Blend;
    return TextureAlphaMode::Auto;
}
TextureMipmapMode parse_texture_mipmaps(std::string value) {
    value = token_lower(std::move(value));
    if (value == "on") return TextureMipmapMode::On;
    if (value == "off") return TextureMipmapMode::Off;
    return TextureMipmapMode::Auto;
}

AssetMetadata read_metadata(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) return {};
    AssetMetadata result;
    std::string line;
    bool header = false;
    while (std::getline(input, line)) {
        const auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos || line[first] == '#') continue;
        std::istringstream stream(line.substr(first));
        std::string command;
        stream >> command;
        if (!header) {
            if (command != "vespera_meta" || !(stream >> result.version)
                || result.version < 1 || result.version > 3) return {};
            header = true;
        } else if (command == "id") {
            stream >> std::quoted(result.id);
        } else if (command == "importer") {
            stream >> std::quoted(result.importer);
        } else if (command == "source_size") {
            stream >> result.source_size;
        } else if (command == "source_hash") {
            stream >> std::quoted(result.source_hash);
        } else if (command == "source_mtime") {
            stream >> result.source_mtime;
        } else if (command == "texture_usage") {
            std::string value; stream >> std::quoted(value); result.texture_settings.usage = parse_texture_usage(value); result.texture_settings_authored = true;
        } else if (command == "texture_filter") {
            std::string value; stream >> std::quoted(value); result.texture_settings.filter = parse_texture_filter(value); result.texture_settings_authored = true;
        } else if (command == "texture_wrap_u") {
            std::string value; stream >> std::quoted(value); result.texture_settings.wrap_u = parse_texture_wrap(value); result.texture_settings_authored = true;
        } else if (command == "texture_wrap_v") {
            std::string value; stream >> std::quoted(value); result.texture_settings.wrap_v = parse_texture_wrap(value); result.texture_settings_authored = true;
        } else if (command == "texture_color_space") {
            std::string value; stream >> std::quoted(value); result.texture_settings.color_space = parse_texture_color_space(value); result.texture_settings_authored = true;
        } else if (command == "texture_alpha") {
            std::string value; stream >> std::quoted(value); result.texture_settings.alpha_mode = parse_texture_alpha(value); result.texture_settings_authored = true;
        } else if (command == "texture_mipmaps") {
            std::string value; stream >> std::quoted(value); result.texture_settings.mipmaps = parse_texture_mipmaps(value); result.texture_settings_authored = true;
        } else if (command == "texture_max_size") {
            stream >> result.texture_settings.max_size; result.texture_settings.max_size = (std::max)(0, result.texture_settings.max_size); result.texture_settings_authored = true;
        } else if (command == "end_meta") {
            break;
        }
    }
    result.valid = header && !result.id.empty();
    return result;
}

bool write_metadata(
    const std::filesystem::path& path,
    const std::string& id,
    const std::string& importer,
    std::uintmax_t source_size_value,
    const std::string& source_hash,
    std::int64_t source_mtime_value,
    const TextureImportSettings* texture_settings = nullptr
) {
    std::ofstream output(path, std::ios::trunc);
    if (!output) return false;
    output << "vespera_meta 3\n";
    output << "id " << std::quoted(id) << "\n";
    output << "importer " << std::quoted(importer) << "\n";
    output << "source_size " << source_size_value << "\n";
    output << "source_hash " << std::quoted(source_hash) << "\n";
    output << "source_mtime " << source_mtime_value << "\n";
    if (texture_settings) {
        output << "texture_usage " << std::quoted(std::string(texture_usage_name(texture_settings->usage))) << "\n";
        output << "texture_filter " << std::quoted(std::string(texture_filter_name(texture_settings->filter))) << "\n";
        output << "texture_wrap_u " << std::quoted(std::string(texture_wrap_name(texture_settings->wrap_u))) << "\n";
        output << "texture_wrap_v " << std::quoted(std::string(texture_wrap_name(texture_settings->wrap_v))) << "\n";
        output << "texture_color_space " << std::quoted(std::string(texture_color_space_name(texture_settings->color_space))) << "\n";
        output << "texture_alpha " << std::quoted(std::string(texture_alpha_mode_name(texture_settings->alpha_mode))) << "\n";
        output << "texture_mipmaps " << std::quoted(std::string(texture_mipmap_mode_name(texture_settings->mipmaps))) << "\n";
        output << "texture_max_size " << (std::max)(0, texture_settings->max_size) << "\n";
    }
    output << "end_meta\n";
    return static_cast<bool>(output);
}



std::string trim_left(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    value.erase(0, first);
    return value;
}

void add_dependency(
    std::vector<AssetDependency>& out,
    const AssetRecord& source,
    const AssetReference& reference,
    std::string reason,
    const AssetRecord* target,
    bool resolved_by_id = false,
    bool stale_fallback_path = false
) {
    AssetDependency dependency;
    dependency.source_asset_id = source.asset_id;
    dependency.requested_asset_id = reference.asset_id;
    dependency.reference = reference.path.generic_string();
    dependency.reason = std::move(reason);
    dependency.resolved = target != nullptr;
    dependency.resolved_by_id = resolved_by_id;
    dependency.stale_fallback_path = stale_fallback_path;
    if (target) dependency.target_asset_id = target->asset_id;
    out.push_back(std::move(dependency));
}

std::string canonical_asset_name(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (unsigned char c : value) {
        if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) result.push_back(static_cast<char>(c));
    }
    return result;
}

void scan_dependencies(
    const std::vector<AssetRecord>& records,
    std::vector<AssetDependency>& out
) {
    out.clear();
    std::unordered_map<std::string, const AssetRecord*> by_path;
    std::unordered_map<std::string, const AssetRecord*> by_id;
    std::unordered_map<std::string, std::vector<const AssetRecord*>> textures_by_name;
    for (const auto& record : records) {
        by_path.emplace(record.relative_path.lexically_normal().generic_string(), &record);
        if (!record.asset_id.empty()) by_id.emplace(record.asset_id, &record);
        if (record.kind == AssetKind::Texture) textures_by_name[canonical_asset_name(record.display_name)].push_back(&record);
    }

    const auto target_path = [&](std::string_view path) -> const AssetRecord* {
        const auto it = by_path.find(std::filesystem::path(path).lexically_normal().generic_string());
        return it == by_path.end() ? nullptr : it->second;
    };
    struct LocalResolution {
        const AssetRecord* record = nullptr;
        bool by_id = false;
        bool stale = false;
    };
    const auto resolve_reference = [&](const AssetReference& reference) -> LocalResolution {
        if (!reference.asset_id.empty()) {
            const auto it = by_id.find(reference.asset_id);
            if (it != by_id.end()) {
                const bool stale = !reference.path.empty()
                    && reference.path.lexically_normal().generic_string()
                        != it->second->relative_path.lexically_normal().generic_string();
                return {it->second, true, stale};
            }
        }
        if (!reference.path.empty()) return {target_path(reference.path.generic_string()), false, false};
        return {};
    };
    const auto target_texture_name = [&](std::string_view name) -> const AssetRecord* {
        const auto it = textures_by_name.find(canonical_asset_name(name));
        if (it == textures_by_name.end() || it->second.size() != 1) return nullptr;
        return it->second.front();
    };

    for (const auto& source : records) {
        if (source.kind != AssetKind::Scene && source.kind != AssetKind::EntityPrefab
            && source.kind != AssetKind::SpriteClip && source.kind != AssetKind::SpriteSheet
            && source.kind != AssetKind::AudioClip && source.kind != AssetKind::Material
            && source.kind != AssetKind::UiDocument && source.kind != AssetKind::RmlDocument
            && source.kind != AssetKind::RmlStyleSheet) continue;
        std::ifstream input(source.absolute_path);
        if (!input) continue;

        if (source.kind == AssetKind::RmlDocument || source.kind == AssetKind::RmlStyleSheet) {
            const std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            for (const auto& reference : scan_rml_asset_path_references(
                     content, source.relative_path, source.kind == AssetKind::RmlStyleSheet)) {
                const auto* target = target_path(reference.project_path.generic_string());
                add_dependency(out, source, AssetReference{{}, reference.project_path}, reference.reason, target);
            }
            continue;
        }

        std::string line;
        while (std::getline(input, line)) {
            line = trim_left(std::move(line));
            if (line.empty() || line[0] == '#') continue;
            std::istringstream stream(line);
            std::string command;
            stream >> command;

            if (source.kind == AssetKind::SpriteClip && command == "texture") {
                std::string resource_name;
                std::string path;
                if (stream >> std::quoted(resource_name) >> std::quoted(path)) {
                    add_dependency(out, source, AssetReference{{}, path}, "Sprite frame texture", target_path(path));
                }
                continue;
            }

            if (source.kind == AssetKind::SpriteSheet && (command == "source" || command == "source_asset")) {
                AssetReference reference;
                std::string path;
                if (command == "source_asset") {
                    stream >> std::quoted(reference.asset_id) >> std::quoted(path);
                } else {
                    stream >> std::quoted(path);
                }
                reference.path = path;
                if (!reference.empty()) {
                    const auto resolved = resolve_reference(reference);
                    add_dependency(out, source, reference, "Sprite sheet texture", resolved.record, resolved.by_id, resolved.stale);
                }
                continue;
            }

            if (source.kind == AssetKind::Material && command == "base_texture_asset") {
                AssetReference reference;
                std::string path;
                stream >> std::quoted(reference.asset_id) >> std::quoted(path);
                reference.path = path == "-" ? std::filesystem::path{} : std::filesystem::path(path);
                if (reference.asset_id == "-") reference.asset_id.clear();
                if (!reference.empty()) {
                    const auto resolved = resolve_reference(reference);
                    add_dependency(out, source, reference, "Material base texture", resolved.record, resolved.by_id, resolved.stale);
                }
                continue;
            }

            if (source.kind == AssetKind::UiDocument && (command == "image_asset" || command == "font_asset")) {
                AssetReference reference;
                std::string path;
                stream >> std::quoted(reference.asset_id) >> std::quoted(path);
                if (reference.asset_id == "-") reference.asset_id.clear();
                reference.path = path == "-" ? std::filesystem::path{} : std::filesystem::path(path);
                if (!reference.empty()) {
                    const auto resolved = resolve_reference(reference);
                    add_dependency(out, source, reference, command == "image_asset" ? "UI image" : "UI font",
                        resolved.record, resolved.by_id, resolved.stale);
                }
                continue;
            }

            if (source.kind == AssetKind::AudioClip && (command == "source" || command == "source_asset")) {
                AssetReference reference;
                std::string path;
                if (command == "source_asset") {
                    stream >> std::quoted(reference.asset_id) >> std::quoted(path);
                } else {
                    stream >> std::quoted(path);
                }
                reference.path = path;
                if (!reference.empty()) {
                    const auto resolved = resolve_reference(reference);
                    add_dependency(out, source, reference, "Audio source", resolved.record, resolved.by_id, resolved.stale);
                }
                continue;
            }

            if (command == "prefab_source" || command == "prefab_source_asset") {
                AssetReference reference;
                std::string path;
                if (command == "prefab_source_asset") {
                    stream >> std::quoted(reference.asset_id) >> std::quoted(path);
                } else {
                    stream >> std::quoted(path);
                }
                reference.path = path;
                if (!reference.empty()) {
                    const auto resolved = resolve_reference(reference);
                    add_dependency(out, source, reference, "Prefab source", resolved.record, resolved.by_id, resolved.stale);
                }
                continue;
            }

            if (command == "mesh_material_asset") {
                AssetReference reference;
                std::string path;
                if (stream >> std::quoted(reference.asset_id) >> std::quoted(path)) {
                    if (reference.asset_id == "-") reference.asset_id.clear();
                    reference.path = path == "-" ? std::filesystem::path{} : std::filesystem::path(path);
                    if (!reference.empty()) {
                        const auto resolved = resolve_reference(reference);
                        add_dependency(out, source, reference, "Mesh Renderer material", resolved.record, resolved.by_id, resolved.stale);
                    }
                }
                continue;
            }

            if (command == "material") {
                std::string material_name;
                std::string texture_name;
                float r=0, g=0, b=0, a=0, u=0, v=0;
                if (stream >> std::quoted(material_name) >> r >> g >> b >> a >> std::quoted(texture_name) >> u >> v
                    && texture_name != "-") {
                    add_dependency(out, source, AssetReference{{}, texture_name}, "Material texture", target_texture_name(texture_name));
                }
                continue;
            }

            if (command == "sprite_clip") {
                std::string clip_name;
                std::uint32_t directions=0, frames=0;
                float fps=0;
                int loop=0;
                if (stream >> std::quoted(clip_name) >> directions >> frames >> fps >> loop) {
                    const auto count = static_cast<std::size_t>(directions) * frames;
                    for (std::size_t i = 0; i < count; ++i) {
                        std::string texture_name;
                        if (!(stream >> std::quoted(texture_name))) break;
                        if (texture_name != "-") add_dependency(out, source, AssetReference{{}, texture_name}, "Embedded sprite clip texture", target_texture_name(texture_name));
                    }
                }
                continue;
            }

            if (command == "sprite_renderer") {
                float width=0, height=0;
                std::string texture_name;
                if (stream >> width >> height >> std::quoted(texture_name) && texture_name != "-") {
                    add_dependency(out, source, AssetReference{{}, texture_name}, "Sprite renderer texture", target_texture_name(texture_name));
                }
                continue;
            }
        }
    }
}
} // namespace

std::string_view texture_usage_name(TextureUsage value) {
    switch (value) { case TextureUsage::World: return "world"; case TextureUsage::Sprite: return "sprite"; case TextureUsage::UI: return "ui"; case TextureUsage::Data: return "data"; }
    return "world";
}
std::string_view texture_filter_name(TextureFilter value) { return value == TextureFilter::Linear ? "linear" : "nearest"; }
std::string_view texture_wrap_name(TextureWrap value) { return value == TextureWrap::Clamp ? "clamp" : "repeat"; }
std::string_view texture_color_space_name(TextureColorSpace value) { return value == TextureColorSpace::Linear ? "linear" : "srgb"; }
std::string_view texture_alpha_mode_name(TextureAlphaMode value) {
    switch (value) { case TextureAlphaMode::Auto: return "auto"; case TextureAlphaMode::Opaque: return "opaque"; case TextureAlphaMode::Cutout: return "cutout"; case TextureAlphaMode::Blend: return "blend"; }
    return "auto";
}
std::string_view texture_mipmap_mode_name(TextureMipmapMode value) {
    switch (value) { case TextureMipmapMode::Auto: return "auto"; case TextureMipmapMode::On: return "on"; case TextureMipmapMode::Off: return "off"; }
    return "auto";
}

std::string_view asset_kind_name(AssetKind kind) {
    switch (kind) {
        case AssetKind::Scene: return "Scene";
        case AssetKind::EntityPrefab: return "Entity Prefab";
        case AssetKind::Texture: return "Texture";
        case AssetKind::Audio: return "Audio";
        case AssetKind::Font: return "Font";
        case AssetKind::SpriteClip: return "Sprite Clip";
        case AssetKind::SpriteSheet: return "Sprite Sheet";
        case AssetKind::AudioClip: return "Audio Clip";
        case AssetKind::Material: return "Material";
        case AssetKind::UiDocument: return "Legacy UI Document";
        case AssetKind::RmlDocument: return "RmlUi Document";
        case AssetKind::RmlStyleSheet: return "RmlUi Style Sheet";
        case AssetKind::LuaScript: return "Lua Script";
    }
    return "Unknown";
}

std::string_view asset_import_state_name(AssetImportState state) {
    switch (state) {
        case AssetImportState::Ready: return "Ready";
        case AssetImportState::MetadataCreated: return "Imported";
        case AssetImportState::MetadataUpdated: return "Reimported";
        case AssetImportState::MetadataMissing: return "Metadata missing";
    }
    return "Unknown";
}

std::optional<AssetKind> asset_kind_from_path(const std::filesystem::path& path) {
    const auto extension = lowercase(path.extension().string());
    if (extension == ".slscene") return AssetKind::Scene;
    if (extension == ".slprefab") return AssetKind::EntityPrefab;
    if (extension == ".bmp" || extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga") return AssetKind::Texture;
    if (extension == ".wav" || extension == ".ogg" || extension == ".mp3") return AssetKind::Audio;
    if (extension == ".ttf" || extension == ".otf") return AssetKind::Font;
    if (extension == ".slspriteclip") return AssetKind::SpriteClip;
    if (extension == ".slspritesheet") return AssetKind::SpriteSheet;
    if (extension == ".slaudio") return AssetKind::AudioClip;
    if (extension == ".slmat") return AssetKind::Material;
    if (extension == ".slui") return AssetKind::UiDocument;
    if (extension == ".rml") return AssetKind::RmlDocument;
    if (extension == ".rcss") return AssetKind::RmlStyleSheet;
    if (extension == ".lua") return AssetKind::LuaScript;
    return std::nullopt;
}

bool AssetCatalog::refresh(const std::filesystem::path& assets_root, std::string* error_message) {
    AssetCatalogRefreshReport ignored;
    return refresh(assets_root, {}, &ignored, error_message);
}

bool AssetCatalog::refresh(
    const std::filesystem::path& assets_root,
    const AssetCatalogRefreshOptions& options,
    AssetCatalogRefreshReport* report,
    std::string* error_message
) {
    const auto previous_records = records_;
    records_.clear();
    dependencies_.clear();
    root_.clear();
    AssetCatalogRefreshReport local_report;

    if (assets_root.empty()) {
        if (error_message) *error_message = "assets root is empty";
        return false;
    }

    std::error_code ec;
    const auto absolute_root = std::filesystem::absolute(assets_root, ec).lexically_normal();
    if (ec) {
        if (error_message) *error_message = "could not resolve assets root: " + ec.message();
        return false;
    }
    if (!std::filesystem::exists(absolute_root, ec) || ec) {
        if (error_message) *error_message = "assets root does not exist: " + absolute_root.string();
        return false;
    }
    if (!std::filesystem::is_directory(absolute_root, ec) || ec) {
        if (error_message) *error_message = "assets root is not a directory: " + absolute_root.string();
        return false;
    }

    root_ = absolute_root;
    std::filesystem::recursive_directory_iterator iterator(
        root_,
        std::filesystem::directory_options::skip_permission_denied,
        ec
    );
    const std::filesystem::recursive_directory_iterator end;
    if (ec) {
        if (error_message) *error_message = "could not scan assets root: " + ec.message();
        return false;
    }

    std::unordered_set<std::string> seen_ids;

    for (; iterator != end; iterator.increment(ec)) {
        if (ec) {
            if (error_message) *error_message = "asset scan failed: " + ec.message();
            records_.clear();
            return false;
        }
        if (!iterator->is_regular_file(ec) || ec) {
            ec.clear();
            continue;
        }
        if (iterator->path().extension() == ".vmeta") {
            auto source = iterator->path();
            source.replace_extension();
            if (!std::filesystem::is_regular_file(source, ec) || ec) {
                ++local_report.orphaned_metadata;
                ec.clear();
            }
            continue;
        }

        const auto kind = asset_kind_from_path(iterator->path());
        if (!kind) continue;

        const auto relative = std::filesystem::relative(iterator->path(), root_, ec).lexically_normal();
        if (ec) {
            ec.clear();
            continue;
        }

        const auto metadata_path = std::filesystem::path(iterator->path().string() + ".vmeta");
        const bool metadata_file_existed = std::filesystem::is_regular_file(metadata_path, ec) && !ec;
        ec.clear();
        const auto size = iterator->file_size(ec);
        if (ec) { ec.clear(); continue; }
        const auto stamp = source_mtime(iterator->path());
        const auto expected_importer = importer_for(*kind, iterator->path());
        auto metadata = read_metadata(metadata_path);
        const bool metadata_was_invalid = metadata_file_existed && !metadata.valid;
        const bool duplicate_id = metadata.valid && seen_ids.contains(metadata.id);
        if (duplicate_id) metadata.id = make_asset_id(relative);

        std::string source_hash;
        const bool metadata_fast_path = !options.force_rehash
            && metadata.valid
            && !duplicate_id
            && metadata.version >= 2
            && metadata.importer == expected_importer
            && metadata.source_size == size
            && metadata.source_mtime == stamp
            && !metadata.source_hash.empty();
        if (metadata_fast_path) {
            source_hash = metadata.source_hash;
            ++local_report.fast_path_hits;
        } else {
            source_hash = hash_file(iterator->path());
            ++local_report.hashes_computed;
        }
        if (source_hash.empty()) continue;

        AssetImportState import_state = AssetImportState::Ready;
        if (!metadata.valid) {
            metadata.id = make_asset_id(relative);
            metadata.importer = expected_importer;
            metadata.source_size = size;
            metadata.source_hash = source_hash;
            metadata.source_mtime = stamp;
            if (options.write_metadata && write_metadata(metadata_path, metadata.id, metadata.importer, size, source_hash, stamp, *kind == AssetKind::Texture ? &metadata.texture_settings : nullptr)) {
                import_state = AssetImportState::MetadataCreated;
                metadata.version = 3;
                if (*kind == AssetKind::Texture) metadata.texture_settings_authored = true;
                ++local_report.metadata_created;
                if (metadata_was_invalid) ++local_report.metadata_repaired;
            } else {
                import_state = AssetImportState::MetadataMissing;
                ++local_report.metadata_missing;
            }
        } else if (duplicate_id
            || metadata.version < 2
            || metadata.source_size != size
            || metadata.source_hash != source_hash
            || metadata.source_mtime != stamp
            || metadata.importer != expected_importer) {
            metadata.importer = expected_importer;
            metadata.source_size = size;
            metadata.source_hash = source_hash;
            metadata.source_mtime = stamp;
            if (options.write_metadata && write_metadata(metadata_path, metadata.id, metadata.importer, size, source_hash, stamp, *kind == AssetKind::Texture ? &metadata.texture_settings : nullptr)) {
                import_state = AssetImportState::MetadataUpdated;
                metadata.version = 3;
                if (*kind == AssetKind::Texture) metadata.texture_settings_authored = true;
                ++local_report.metadata_updated;
                if (duplicate_id) ++local_report.metadata_repaired;
            } else {
                import_state = AssetImportState::MetadataMissing;
                ++local_report.metadata_missing;
            }
        }

        seen_ids.insert(metadata.id);

        AssetRecord record;
        record.kind = *kind;
        record.absolute_path = iterator->path().lexically_normal();
        record.relative_path = relative;
        record.metadata_path = metadata_path;
        record.display_name = relative.stem().string();
        record.asset_id = std::move(metadata.id);
        record.importer = std::move(metadata.importer);
        record.source_size = size;
        record.source_hash = source_hash;
        record.source_mtime = stamp;
        record.import_state = import_state;
        record.texture_settings = metadata.texture_settings;
        record.texture_settings_authored = metadata.texture_settings_authored;
        records_.push_back(std::move(record));
        ++local_report.scanned_assets;
    }

    std::sort(records_.begin(), records_.end(), [](const AssetRecord& a, const AssetRecord& b) {
        if (a.kind != b.kind) return static_cast<int>(a.kind) < static_cast<int>(b.kind);
        return a.relative_path.generic_string() < b.relative_path.generic_string();
    });
    scan_dependencies(records_, dependencies_);
    local_report.dependency_edges = dependencies_.size();
    local_report.broken_dependencies = static_cast<std::size_t>(std::count_if(
        dependencies_.begin(), dependencies_.end(), [](const AssetDependency& dependency) { return !dependency.resolved; }));
    local_report.stable_reference_edges = static_cast<std::size_t>(std::count_if(
        dependencies_.begin(), dependencies_.end(), [](const AssetDependency& dependency) { return !dependency.requested_asset_id.empty(); }));
    local_report.stale_fallback_paths = static_cast<std::size_t>(std::count_if(
        dependencies_.begin(), dependencies_.end(), [](const AssetDependency& dependency) { return dependency.stale_fallback_path; }));

    // Compare stable IDs across refreshes so tooling can distinguish a content
    // reimport from a source-file move/rename. This remains source-catalog
    // tracking; runtime GPU/audio hot replacement is intentionally separate.
    std::unordered_map<std::string, const AssetRecord*> previous_by_id;
    std::unordered_map<std::string, const AssetRecord*> current_by_id;
    for (const auto& record : previous_records) if (!record.asset_id.empty()) previous_by_id.emplace(record.asset_id, &record);
    for (const auto& record : records_) if (!record.asset_id.empty()) current_by_id.emplace(record.asset_id, &record);

    for (const auto& record : records_) {
        if (record.asset_id.empty()) continue;
        const auto old_it = previous_by_id.find(record.asset_id);
        if (old_it == previous_by_id.end()) {
            local_report.changes.push_back({AssetCatalogChangeKind::Added, record.asset_id, {}, record.relative_path});
            ++local_report.assets_added;
            continue;
        }
        const auto* old = old_it->second;
        if (old->relative_path.lexically_normal() != record.relative_path.lexically_normal()) {
            local_report.changes.push_back({AssetCatalogChangeKind::Moved, record.asset_id, old->relative_path, record.relative_path});
            ++local_report.assets_moved;
        }
        if (old->source_hash != record.source_hash || old->importer != record.importer) {
            local_report.changes.push_back({AssetCatalogChangeKind::ContentChanged, record.asset_id, old->relative_path, record.relative_path});
            ++local_report.assets_changed;
        }
    }
    for (const auto& old : previous_records) {
        if (old.asset_id.empty() || current_by_id.contains(old.asset_id)) continue;
        local_report.changes.push_back({AssetCatalogChangeKind::Removed, old.asset_id, old.relative_path, {}});
        ++local_report.assets_removed;
    }
    if (report) *report = local_report;
    if (error_message) error_message->clear();
    return true;
}

std::vector<const AssetRecord*> AssetCatalog::records_of_kind(AssetKind kind) const {
    std::vector<const AssetRecord*> result;
    for (const auto& record : records_) {
        if (record.kind == kind) result.push_back(&record);
    }
    return result;
}

const AssetRecord* AssetCatalog::find(std::string_view relative_path) const {
    const std::filesystem::path target(relative_path);
    const auto normalized = target.lexically_normal().generic_string();
    for (const auto& record : records_) {
        if (record.relative_path.lexically_normal().generic_string() == normalized) return &record;
    }
    return nullptr;
}

const AssetRecord* AssetCatalog::find_by_id(std::string_view asset_id) const {
    for (const auto& record : records_) {
        if (record.asset_id == asset_id) return &record;
    }
    return nullptr;
}

AssetReferenceResolution AssetCatalog::resolve_reference(const AssetReference& reference) const {
    if (!reference.asset_id.empty()) {
        if (const auto* record = find_by_id(reference.asset_id)) {
            const bool stale = !reference.path.empty()
                && reference.path.lexically_normal().generic_string()
                    != record->relative_path.lexically_normal().generic_string();
            return {record, true, stale};
        }
    }
    if (!reference.path.empty()) {
        if (const auto* record = find(reference.path.generic_string())) return {record, false, false};
    }
    return {};
}

std::vector<const AssetDependency*> AssetCatalog::dependencies_of(std::string_view asset_id) const {
    std::vector<const AssetDependency*> result;
    for (const auto& dependency : dependencies_) {
        if (dependency.source_asset_id == asset_id) result.push_back(&dependency);
    }
    return result;
}

std::vector<const AssetDependency*> AssetCatalog::dependents_of(std::string_view asset_id) const {
    std::vector<const AssetDependency*> result;
    for (const auto& dependency : dependencies_) {
        if (dependency.resolved && dependency.target_asset_id == asset_id) result.push_back(&dependency);
    }
    return result;
}

std::vector<const AssetDependency*> AssetCatalog::broken_dependencies() const {
    std::vector<const AssetDependency*> result;
    for (const auto& dependency : dependencies_) {
        if (!dependency.resolved) result.push_back(&dependency);
    }
    return result;
}

bool AssetCatalog::save_texture_import_settings(
    std::string_view asset_id,
    const TextureImportSettings& settings,
    std::string* error_message
) const {
    const auto* record = find_by_id(asset_id);
    if (!record || record->kind != AssetKind::Texture) {
        if (error_message) *error_message = "asset is not a texture in the current catalog";
        return false;
    }
    auto metadata = read_metadata(record->metadata_path);
    if (!metadata.valid || metadata.id != record->asset_id) {
        if (error_message) *error_message = "texture .vmeta is missing, invalid, or does not match the selected stable ID";
        return false;
    }
    TextureImportSettings normalized = settings;
    normalized.max_size = (std::max)(0, normalized.max_size);
    if (!write_metadata(record->metadata_path, record->asset_id, record->importer,
            record->source_size, record->source_hash, record->source_mtime, &normalized)) {
        if (error_message) *error_message = "could not write texture import settings to .vmeta";
        return false;
    }
    if (error_message) error_message->clear();
    return true;
}

} // namespace vespera
