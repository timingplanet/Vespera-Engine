#include <vespera/world/sector_world.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace vespera {
namespace {

constexpr float kGeometryEpsilon = 1.0e-5f;

struct EdgeFrame {
    float inward_x = 0.0f;
    float inward_z = 0.0f;
    bool valid = false;
};

EdgeFrame edge_frame(const Vec2& a, const Vec2& b) {
    const float edge_x = b.x - a.x;
    const float edge_z = b.z - a.z;
    const float length_sq = edge_x * edge_x + edge_z * edge_z;
    if (length_sq <= 1.0e-8f) {
        return {};
    }

    const float inv_length = 1.0f / std::sqrt(length_sq);
    return {
        -edge_z * inv_length,
         edge_x * inv_length,
        true,
    };
}

float signed_edge_distance(const Vec2& a, const EdgeFrame& frame, const Vec3& point) {
    return (point.x - a.x) * frame.inward_x + (point.z - a.z) * frame.inward_z;
}

bool valid_adjacent_index(const SectorWorld& world, int index) {
    return index >= 0 && static_cast<std::size_t>(index) < world.sectors().size();
}

bool passable_portal_to(
    const SectorWorld& world,
    std::size_t from_sector,
    std::size_t side_index,
    float body_height,
    float max_step_height,
    std::size_t* adjacent_out
) {
    if (from_sector >= world.sectors().size()) {
        return false;
    }

    const Sector& sector = world.sectors()[from_sector];
    if (sector.sides.size() != sector.vertices.size() || side_index >= sector.sides.size()) {
        return false;
    }

    const int adjacent_index = sector.sides[side_index].adjacent_sector;
    if (!valid_adjacent_index(world, adjacent_index)) {
        return false;
    }

    const Sector& adjacent = world.sectors()[static_cast<std::size_t>(adjacent_index)];
    const float opening_floor = std::max(sector.floor_height, adjacent.floor_height);
    const float opening_ceiling = std::min(sector.ceiling_height, adjacent.ceiling_height);
    const float required_height = std::max(body_height, 0.0f);
    if (opening_ceiling - opening_floor + kGeometryEpsilon < required_height) {
        return false;
    }

    const float step_up = adjacent.floor_height - sector.floor_height;
    if (step_up > std::max(max_step_height, 0.0f) + kGeometryEpsilon) {
        return false;
    }

    if (adjacent_out) {
        *adjacent_out = static_cast<std::size_t>(adjacent_index);
    }
    return true;
}

Vec3 resolve_sector_edges(
    const SectorWorld* world,
    std::size_t sector_index,
    const Sector& sector,
    Vec3 result,
    float radius,
    float body_height,
    float max_step_height,
    bool honor_portals
) {
    const float safe_radius = std::max(radius, 0.0f);

    // A few projection passes resolve corners while retaining sliding along a
    // wall. Passable full-edge portals are intentionally omitted from collision.
    for (int pass = 0; pass < 4; ++pass) {
        bool adjusted = false;
        for (std::size_t i = 0; i < sector.vertices.size(); ++i) {
            if (honor_portals && world && passable_portal_to(
                    *world,
                    sector_index,
                    i,
                    body_height,
                    max_step_height,
                    nullptr)) {
                continue;
            }

            const Vec2 a = sector.vertices[i];
            const Vec2 b = sector.vertices[(i + 1) % sector.vertices.size()];
            const float edge_x = b.x - a.x;
            const float edge_z = b.z - a.z;
            const float length_sq = edge_x * edge_x + edge_z * edge_z;
            const EdgeFrame frame = edge_frame(a, b);
            if (!frame.valid || length_sq <= 1.0e-8f) {
                continue;
            }

            // Walls are finite segments, not infinite half-planes. That matters
            // when a straight boundary is split into solid/portal/solid pieces:
            // the neighboring solid pieces must not close the portal opening.
            const float relative_x = result.x - a.x;
            const float relative_z = result.z - a.z;
            const float raw_t = (relative_x * edge_x + relative_z * edge_z) / length_sq;

            if (raw_t >= 0.0f && raw_t <= 1.0f) {
                const float distance = signed_edge_distance(a, frame, result);
                if (distance < safe_radius) {
                    const float correction = safe_radius - distance;
                    result.x += frame.inward_x * correction;
                    result.z += frame.inward_z * correction;
                    adjusted = true;
                }
                continue;
            }

            const float endpoint_x = raw_t < 0.0f ? a.x : b.x;
            const float endpoint_z = raw_t < 0.0f ? a.z : b.z;
            const float away_x = result.x - endpoint_x;
            const float away_z = result.z - endpoint_z;
            const float distance_sq = away_x * away_x + away_z * away_z;
            if (distance_sq >= safe_radius * safe_radius) {
                continue;
            }

            if (distance_sq > 1.0e-10f) {
                const float distance = std::sqrt(distance_sq);
                const float correction = safe_radius - distance;
                result.x += (away_x / distance) * correction;
                result.z += (away_z / distance) * correction;
            } else {
                result.x += frame.inward_x * safe_radius;
                result.z += frame.inward_z * safe_radius;
            }
            adjusted = true;
        }

        if (!adjusted) {
            break;
        }
    }

    return result;
}

} // namespace

TextureId SectorWorld::add_texture(TextureData texture) {
    const TextureId id = static_cast<TextureId>(textures_.size());
    textures_.push_back(std::move(texture));
    ++revision_;
    return id;
}

MaterialId SectorWorld::add_material(WorldMaterial material) {
    materials_.push_back(std::move(material));
    ++revision_;
    return static_cast<MaterialId>(materials_.size() - 1);
}

bool SectorWorld::set_material(std::size_t index, WorldMaterial material) {
    if (index >= materials_.size()) {
        return false;
    }
    materials_[index] = std::move(material);
    ++revision_;
    return true;
}

bool SectorWorld::erase_material(std::size_t index) {
    if (index >= materials_.size()) {
        return false;
    }

    const auto remap = [index](MaterialId id) -> MaterialId {
        if (id == kInvalidMaterial) {
            return id;
        }
        if (id == static_cast<MaterialId>(index)) {
            return kInvalidMaterial;
        }
        if (id > static_cast<MaterialId>(index)) {
            return id - 1u;
        }
        return id;
    };

    materials_.erase(materials_.begin() + static_cast<std::ptrdiff_t>(index));
    for (auto& sector : sectors_) {
        sector.floor_material = remap(sector.floor_material);
        sector.ceiling_material = remap(sector.ceiling_material);
        sector.wall_material = remap(sector.wall_material);
        for (auto& side : sector.sides) {
            side.material = remap(side.material);
        }
    }
    ++revision_;
    return true;
}

std::size_t SectorWorld::add_sector(Sector sector) {
    if (!sector.sides.empty() && sector.sides.size() != sector.vertices.size()) {
        sector.sides.resize(sector.vertices.size());
    }
    sectors_.push_back(std::move(sector));
    ++revision_;
    return sectors_.size() - 1;
}

bool SectorWorld::set_sector(std::size_t index, Sector sector) {
    if (index >= sectors_.size()) {
        return false;
    }
    if (!sector.sides.empty() && sector.sides.size() != sector.vertices.size()) {
        sector.sides.resize(sector.vertices.size());
    }
    sectors_[index] = std::move(sector);
    ++revision_;
    return true;
}

bool SectorWorld::erase_sector(std::size_t index) {
    if (index >= sectors_.size()) {
        return false;
    }

    sectors_.erase(sectors_.begin() + static_cast<std::ptrdiff_t>(index));
    for (auto& sector : sectors_) {
        for (auto& side : sector.sides) {
            if (side.adjacent_sector == static_cast<int>(index)) {
                side.adjacent_sector = -1;
            } else if (side.adjacent_sector > static_cast<int>(index)) {
                --side.adjacent_sector;
            }
        }
    }
    ++revision_;
    return true;
}

void SectorWorld::clear() {
    textures_.clear();
    materials_.clear();
    sectors_.clear();
    ++revision_;
}

std::optional<std::size_t> SectorWorld::find_sector_index(Vec2 point, float margin) const {
    for (std::size_t i = 0; i < sectors_.size(); ++i) {
        if (point_inside_sector(sectors_[i], point, margin)) return i;
    }
    return std::nullopt;
}

const Sector* SectorWorld::find_sector_by_name(std::string_view name) const {
    for (const auto& sector : sectors_) {
        if (sector.name == name) return &sector;
    }
    return nullptr;
}

bool point_inside_sector(const Sector& sector, Vec2 point, float margin) {
    if (sector.vertices.size() < 3) {
        return false;
    }

    for (std::size_t i = 0; i < sector.vertices.size(); ++i) {
        const Vec2 a = sector.vertices[i];
        const Vec2 b = sector.vertices[(i + 1) % sector.vertices.size()];
        const EdgeFrame frame = edge_frame(a, b);
        if (!frame.valid) {
            continue;
        }

        const float distance = (point.x - a.x) * frame.inward_x + (point.z - a.z) * frame.inward_z;
        if (distance < margin - kGeometryEpsilon) {
            return false;
        }
    }

    return true;
}

std::optional<std::size_t> find_matching_sector_side(
    const SectorWorld& world,
    std::size_t source_sector,
    std::size_t source_side,
    std::size_t target_sector,
    float epsilon
) {
    const auto& sectors = world.sectors();
    if (source_sector >= sectors.size() || target_sector >= sectors.size()) {
        return std::nullopt;
    }

    const auto& source = sectors[source_sector];
    const auto& target = sectors[target_sector];
    if (source.vertices.empty() || target.vertices.empty() || source_side >= source.vertices.size()) {
        return std::nullopt;
    }

    const Vec2 source_a = source.vertices[source_side];
    const Vec2 source_b = source.vertices[(source_side + 1u) % source.vertices.size()];
    const float epsilon_sq = std::max(epsilon, 0.0f) * std::max(epsilon, 0.0f);
    const auto near = [epsilon_sq](Vec2 a, Vec2 b) {
        const float dx = a.x - b.x;
        const float dz = a.z - b.z;
        return dx * dx + dz * dz <= epsilon_sq;
    };

    for (std::size_t side = 0; side < target.vertices.size(); ++side) {
        const Vec2 target_a = target.vertices[side];
        const Vec2 target_b = target.vertices[(side + 1u) % target.vertices.size()];
        if ((near(source_a, target_b) && near(source_b, target_a))
            || (near(source_a, target_a) && near(source_b, target_b))) {
            return side;
        }
    }
    return std::nullopt;
}

bool sector_portal_is_passable(
    const SectorWorld& world,
    std::size_t from_sector,
    std::size_t side_index,
    float body_height,
    float max_step_height
) {
    return passable_portal_to(
        world,
        from_sector,
        side_index,
        body_height,
        max_step_height,
        nullptr
    );
}

Vec3 move_circle_inside_sector(const Sector& sector, Vec3 position, Vec3 delta, float radius) {
    Vec3 result{
        position.x + delta.x,
        position.y + delta.y,
        position.z + delta.z,
    };

    if (sector.vertices.size() < 3) {
        return result;
    }

    return resolve_sector_edges(nullptr, 0, sector, result, radius, 0.0f, 0.0f, false);
}

SectorMoveResult move_circle_through_world(
    const SectorWorld& world,
    std::size_t sector_index,
    Vec3 position,
    Vec3 delta,
    float radius,
    float body_height,
    float max_step_height
) {
    SectorMoveResult output;
    output.position = {
        position.x + delta.x,
        position.y + delta.y,
        position.z + delta.z,
    };
    output.sector_index = sector_index;

    if (world.sectors().empty() || sector_index >= world.sectors().size()) {
        return output;
    }

    // Normal gameplay deltas are far smaller than a sector. Still, allow a few
    // transitions so the controller is deterministic during low frame rates.
    for (int transition = 0; transition < 4; ++transition) {
        const Sector& current = world.sectors()[output.sector_index];
        output.position = resolve_sector_edges(
            &world,
            output.sector_index,
            current,
            output.position,
            radius,
            body_height,
            max_step_height,
            true
        );

        if (point_inside_sector(current, {output.position.x, output.position.z})) {
            break;
        }

        bool crossed = false;
        for (std::size_t side_index = 0; side_index < current.vertices.size(); ++side_index) {
            std::size_t adjacent_index = 0;
            if (!passable_portal_to(
                    world,
                    output.sector_index,
                    side_index,
                    body_height,
                    max_step_height,
                    &adjacent_index)) {
                continue;
            }

            const Sector& adjacent = world.sectors()[adjacent_index];
            if (point_inside_sector(adjacent, {output.position.x, output.position.z})) {
                output.sector_index = adjacent_index;
                output.changed_sector = true;
                crossed = true;
                break;
            }
        }

        if (!crossed) {
            // A malformed or one-way portal should not let the player escape
            // into undefined world space. Re-resolve this move with every edge
            // treated as solid.
            output.position = resolve_sector_edges(
                nullptr,
                output.sector_index,
                current,
                output.position,
                radius,
                body_height,
                max_step_height,
                false
            );
            break;
        }
    }

    return output;
}

} // namespace vespera
