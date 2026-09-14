#pragma once

#include <vespera/assets/asset_reference.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vespera {

enum class AssetKind {
    Scene,
    EntityPrefab,
    Texture,
    Audio,
    Font,
    SpriteClip,
    SpriteSheet,
    AudioClip,
    Material,
    UiDocument,
    RmlDocument,
    RmlStyleSheet,
    LuaScript,
};

enum class AssetImportState {
    Ready,
    MetadataCreated,
    MetadataUpdated,
    MetadataMissing,
};

enum class TextureUsage { World, Sprite, UI, Data };
enum class TextureFilter { Nearest, Linear };
enum class TextureWrap { Repeat, Clamp };
enum class TextureColorSpace { SRGB, Linear };
enum class TextureAlphaMode { Auto, Opaque, Cutout, Blend };
enum class TextureMipmapMode { Auto, On, Off };

// Renderer-independent authored texture intent. These settings live in .vmeta
// v3 and deliberately describe source/import semantics rather than D3D12 state.
struct TextureImportSettings {
    TextureUsage usage = TextureUsage::World;
    TextureFilter filter = TextureFilter::Nearest;
    TextureWrap wrap_u = TextureWrap::Repeat;
    TextureWrap wrap_v = TextureWrap::Repeat;
    TextureColorSpace color_space = TextureColorSpace::SRGB;
    TextureAlphaMode alpha_mode = TextureAlphaMode::Auto;
    TextureMipmapMode mipmaps = TextureMipmapMode::Auto;
    int max_size = 0; // 0 = source size

    bool operator==(const TextureImportSettings&) const = default;
};

struct AssetRecord {
    AssetKind kind = AssetKind::Scene;
    std::filesystem::path absolute_path;
    std::filesystem::path relative_path;
    std::filesystem::path metadata_path;
    std::string display_name;
    std::string asset_id;
    std::string importer;
    std::uintmax_t source_size = 0;
    std::string source_hash;
    std::int64_t source_mtime = 0;
    AssetImportState import_state = AssetImportState::Ready;
    TextureImportSettings texture_settings{};
    bool texture_settings_authored = false;
};

struct AssetCatalogRefreshOptions {
    // The editor uses true so newly dropped assets receive stable IDs and
    // changed files get their import fingerprint refreshed. Runtime discovery
    // can also use this safely; if metadata cannot be written, the record is
    // still returned with MetadataMissing rather than failing the catalog.
    bool write_metadata = true;
    // Explicit reimport can bypass the mtime/size cache hint and recompute
    // content hashes. Automatic editor polling leaves this false.
    bool force_rehash = false;
};

enum class AssetCatalogChangeKind {
    Added,
    ContentChanged,
    Moved,
    Removed,
};

struct AssetCatalogChange {
    AssetCatalogChangeKind kind = AssetCatalogChangeKind::Added;
    std::string asset_id;
    std::filesystem::path old_path;
    std::filesystem::path new_path;
};

struct AssetCatalogRefreshReport {
    std::size_t scanned_assets = 0;
    std::size_t metadata_created = 0;
    std::size_t metadata_updated = 0;
    std::size_t metadata_missing = 0;
    std::size_t metadata_repaired = 0;
    std::size_t orphaned_metadata = 0;
    std::size_t hashes_computed = 0;
    std::size_t fast_path_hits = 0;
    std::size_t dependency_edges = 0;
    std::size_t broken_dependencies = 0;
    std::size_t stable_reference_edges = 0;
    std::size_t stale_fallback_paths = 0;
    std::size_t assets_added = 0;
    std::size_t assets_changed = 0;
    std::size_t assets_moved = 0;
    std::size_t assets_removed = 0;
    std::vector<AssetCatalogChange> changes;
};

struct AssetDependency {
    std::string source_asset_id;
    std::string target_asset_id;
    // Stable ID authored by the source asset, when present. Older/path-only
    // records leave this empty.
    std::string requested_asset_id;
    std::string reference;
    std::string reason;
    bool resolved = false;
    bool resolved_by_id = false;
    // True when the stable ID resolved but the human-readable fallback path no
    // longer matches the asset's current catalog path. This is non-fatal and
    // is exactly what stable references are intended to survive.
    bool stale_fallback_path = false;
};

struct AssetReferenceResolution {
    const AssetRecord* record = nullptr;
    bool resolved_by_id = false;
    bool stale_fallback_path = false;

    [[nodiscard]] explicit operator bool() const { return record != nullptr; }
};

[[nodiscard]] std::string_view asset_kind_name(AssetKind kind);
[[nodiscard]] std::string_view asset_import_state_name(AssetImportState state);
[[nodiscard]] std::string_view texture_usage_name(TextureUsage value);
[[nodiscard]] std::string_view texture_filter_name(TextureFilter value);
[[nodiscard]] std::string_view texture_wrap_name(TextureWrap value);
[[nodiscard]] std::string_view texture_color_space_name(TextureColorSpace value);
[[nodiscard]] std::string_view texture_alpha_mode_name(TextureAlphaMode value);
[[nodiscard]] std::string_view texture_mipmap_mode_name(TextureMipmapMode value);
[[nodiscard]] std::optional<AssetKind> asset_kind_from_path(const std::filesystem::path& path);

// Project-local authored asset database. 0.7.x upgrades the original native
// scene/prefab scan into a shared project catalog with stable .vmeta IDs,
// import fingerprints, typed discovery, and the first source dependency graph.
// 0.7.6+ adds ID-first reference resolution and authoring consumers; 0.7.9 adds renderer-independent .vmeta v3 import intent.
class AssetCatalog {
public:
    bool refresh(const std::filesystem::path& assets_root, std::string* error_message = nullptr);
    bool refresh(
        const std::filesystem::path& assets_root,
        const AssetCatalogRefreshOptions& options,
        AssetCatalogRefreshReport* report,
        std::string* error_message = nullptr
    );

    [[nodiscard]] const std::filesystem::path& root() const { return root_; }
    [[nodiscard]] const std::vector<AssetRecord>& records() const { return records_; }
    [[nodiscard]] std::vector<const AssetRecord*> records_of_kind(AssetKind kind) const;
    [[nodiscard]] const AssetRecord* find(std::string_view relative_path) const;
    [[nodiscard]] const AssetRecord* find_by_id(std::string_view asset_id) const;
    [[nodiscard]] AssetReferenceResolution resolve_reference(const AssetReference& reference) const;
    [[nodiscard]] const std::vector<AssetDependency>& dependencies() const { return dependencies_; }
    [[nodiscard]] std::vector<const AssetDependency*> dependencies_of(std::string_view asset_id) const;
    [[nodiscard]] std::vector<const AssetDependency*> dependents_of(std::string_view asset_id) const;
    [[nodiscard]] std::vector<const AssetDependency*> broken_dependencies() const;

    // Persists renderer-independent texture import intent into this asset's
    // .vmeta sidecar without changing its stable ID or source fingerprint.
    bool save_texture_import_settings(
        std::string_view asset_id,
        const TextureImportSettings& settings,
        std::string* error_message = nullptr
    ) const;

private:
    std::filesystem::path root_;
    std::vector<AssetRecord> records_;
    std::vector<AssetDependency> dependencies_;
};

} // namespace vespera
