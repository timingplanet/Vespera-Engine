#include <vespera/assets/runtime_asset_monitor.hpp>

#include <utility>

namespace vespera {

RuntimeAssetMonitorResult RuntimeAssetMonitor::update(
    AssetCatalog& catalog,
    const std::filesystem::path& assets_root,
    double delta_seconds
) {
    RuntimeAssetMonitorResult result;
    if (delta_seconds > 0.0) elapsed_seconds_ += delta_seconds;
    if (elapsed_seconds_ < interval_seconds_) return result;
    elapsed_seconds_ = 0.0;
    result.polled = true;
    AssetCatalogRefreshOptions options;
    options.write_metadata = false;
    std::string error;
    result.ok = catalog.refresh(assets_root, options, &result.report, &error);
    result.message = result.ok ? "runtime asset catalog refreshed" : std::move(error);
    return result;
}

} // namespace vespera
