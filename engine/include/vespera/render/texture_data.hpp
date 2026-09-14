#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vespera {

// CPU-side RGBA8 texture payload. In 0.1.x this deliberately stays tiny and
// renderer-independent: asset importers can populate it later without exposing
// D3D12/Vulkan objects to the world or game layers.
struct TextureData {
    std::string name;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba8;

    [[nodiscard]] bool valid() const {
        return width > 0
            && height > 0
            && rgba8.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    }
};

} // namespace vespera
