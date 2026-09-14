#pragma once

#include <filesystem>
#include <string>

namespace vespera {

// Human-readable authored references carry both a stable project asset ID and
// a fallback relative path. The ID is authoritative when present; the path
// keeps source files understandable and lets older projects/descriptors migrate
// without a hard break.
struct AssetReference {
    std::string asset_id;
    std::filesystem::path path;

    [[nodiscard]] bool empty() const {
        return asset_id.empty() && path.empty();
    }
};

} // namespace vespera
