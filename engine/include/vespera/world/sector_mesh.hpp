#pragma once

#include <vespera/world/sector_world.hpp>

#include <cstdint>
#include <vector>

namespace vespera {

struct SectorMeshVertex {
    float position[3];
    float color[3];
    float uv[2];
    float texture_layer;
};

struct SectorMesh {
    std::vector<SectorMeshVertex> vertices;
    std::vector<std::uint32_t> indices;
};

SectorMesh build_sector_mesh(const SectorWorld& world);

} // namespace vespera
