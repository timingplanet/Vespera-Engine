#pragma once

#include <vespera/assets/asset_catalog.hpp>

#include <filesystem>
#include <string>

namespace vespera {

struct RuntimeAssetMonitorResult {
    bool polled = false;
    bool ok = true;
    AssetCatalogRefreshReport report;
    std::string message;
};

// Source/runtime invalidation groundwork. Polling only refreshes the catalog and
// reports stable-ID changes. It deliberately does NOT mutate renderer, Scene or
// SDL audio resources; consumers can queue those replacements at safe points.
class RuntimeAssetMonitor {
public:
    explicit RuntimeAssetMonitor(double interval_seconds = 1.0)
        : interval_seconds_(interval_seconds > 0.05 ? interval_seconds : 0.05) {}

    void reset() { elapsed_seconds_ = 0.0; }
    [[nodiscard]] RuntimeAssetMonitorResult update(
        AssetCatalog& catalog,
        const std::filesystem::path& assets_root,
        double delta_seconds
    );

private:
    double interval_seconds_ = 1.0;
    double elapsed_seconds_ = 0.0;
};

} // namespace vespera
