#pragma once

#include <vespera/math/types.hpp>
#include <vespera/render/texture_data.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vespera {

using TextureId = std::uint32_t;
constexpr TextureId kInvalidTexture = (std::numeric_limits<TextureId>::max)();
using MaterialId = std::uint32_t;
constexpr MaterialId kInvalidMaterial = (std::numeric_limits<MaterialId>::max)();

struct WorldMaterial {
    std::string name;
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
    TextureId texture = kInvalidTexture;
    // Tiling in texture repetitions per world unit on each surface axis.
    Vec2 uv_scale{0.5f, 0.5f};
};

struct SectorSide {
    MaterialId material = kInvalidMaterial;
    int adjacent_sector = -1;
};

struct Sector {
    std::string name;
    // Convex polygon vertices in counter-clockwise order on the X/Z plane.
    std::vector<Vec2> vertices;
    // Optional per-edge data. If populated, sides.size() should match vertices.size().
    std::vector<SectorSide> sides;
    float floor_height = 0.0f;
    float ceiling_height = 3.0f;
    MaterialId floor_material = kInvalidMaterial;
    MaterialId ceiling_material = kInvalidMaterial;
    MaterialId wall_material = kInvalidMaterial;
};

class SectorWorld {
public:
    TextureId add_texture(TextureData texture);
    MaterialId add_material(WorldMaterial material);
    bool set_material(std::size_t index, WorldMaterial material);
    bool erase_material(std::size_t index);
    std::size_t add_sector(Sector sector);
    bool set_sector(std::size_t index, Sector sector);
    bool erase_sector(std::size_t index);
    void clear();

    [[nodiscard]] const std::vector<TextureData>& textures() const { return textures_; }
    [[nodiscard]] const std::vector<WorldMaterial>& materials() const { return materials_; }
    [[nodiscard]] const std::vector<Sector>& sectors() const { return sectors_; }
    [[nodiscard]] std::uint64_t revision() const { return revision_; }

    [[nodiscard]] std::optional<std::size_t> find_sector_index(Vec2 point, float margin = 0.0f) const;
    [[nodiscard]] const Sector* find_sector_by_name(std::string_view name) const;

private:
    std::vector<TextureData> textures_;
    std::vector<WorldMaterial> materials_;
    std::vector<Sector> sectors_;
    std::uint64_t revision_ = 1;
};

struct SectorMoveResult {
    Vec3 position{};
    std::size_t sector_index = 0;
    bool changed_sector = false;
};

[[nodiscard]] bool point_inside_sector(const Sector& sector, Vec2 point, float margin = 0.0f);

// Find the side in target_sector that shares the same geometric segment as
// source_sector/source_side. Adjacent convex sectors normally store the shared
// edge in reverse order because both polygons are counter-clockwise.
[[nodiscard]] std::optional<std::size_t> find_matching_sector_side(
    const SectorWorld& world,
    std::size_t source_sector,
    std::size_t source_side,
    std::size_t target_sector,
    float epsilon = 1.0e-4f
);

[[nodiscard]] bool sector_portal_is_passable(
    const SectorWorld& world,
    std::size_t from_sector,
    std::size_t side_index,
    float body_height,
    float max_step_height
);

Vec3 move_circle_inside_sector(
    const Sector& sector,
    Vec3 position,
    Vec3 delta,
    float radius
);

SectorMoveResult move_circle_through_world(
    const SectorWorld& world,
    std::size_t sector_index,
    Vec3 position,
    Vec3 delta,
    float radius,
    float body_height,
    float max_step_height
);

} // namespace vespera
