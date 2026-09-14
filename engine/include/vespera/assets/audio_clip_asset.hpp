#pragma once

#include <vespera/assets/asset_catalog.hpp>

#include <filesystem>
#include <string>

namespace vespera {

struct AudioClipAsset {
    std::string name;
    std::string source_asset_id;
    std::filesystem::path source_path;
    bool source_resolved_by_id = false;
    bool source_fallback_stale = false;
    float volume = 1.0f;
    bool loop = false;
    bool spatial = false;
    float min_distance = 1.0f;
    float max_distance = 20.0f;
};

struct AudioClipAssetResult {
    bool ok = false;
    AudioClipAsset clip;
    std::string message;
    explicit operator bool() const { return ok; }
};

// Authored audio-playback defaults kept separate from raw source audio. The
// current SDL3 backend plays WAV sources; descriptors are already structured so
// future OGG/MP3 import/cooking can preserve gameplay-facing settings.
[[nodiscard]] AudioClipAssetResult load_audio_clip_asset(
    const std::filesystem::path& path,
    const AssetCatalog& catalog
);

} // namespace vespera
