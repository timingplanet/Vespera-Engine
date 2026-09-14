#include <vespera/world/sector_mesh.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace vespera {
namespace {

struct MaterialView {
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
    Vec2 uv_scale{0.5f, 0.5f};
    float texture_layer = 0.0f; // layer 0 is the renderer-provided white fallback
};

MaterialView material_view(
    const SectorWorld& world,
    MaterialId material,
    std::array<float, 4> fallback
) {
    MaterialView view;
    view.color = fallback;

    if (material == kInvalidMaterial || material >= world.materials().size()) {
        return view;
    }

    const WorldMaterial& source = world.materials()[material];
    view.color = source.color;
    view.uv_scale = source.uv_scale;
    if (source.texture != kInvalidTexture
        && source.texture < world.textures().size()
        && world.textures()[source.texture].valid()) {
        view.texture_layer = static_cast<float>(source.texture + 1u);
    }
    return view;
}

void append_vertex(
    SectorMesh& mesh,
    const Vec3& position,
    const MaterialView& material,
    float u,
    float v
) {
    mesh.vertices.push_back({
        {position.x, position.y, position.z},
        {material.color[0], material.color[1], material.color[2]},
        {u, v},
        material.texture_layer,
    });
}

void append_quad(
    SectorMesh& mesh,
    const Vec3& a,
    const Vec3& b,
    const Vec3& c,
    const Vec3& d,
    const MaterialView& material,
    float u0,
    float v0,
    float u1,
    float v1
) {
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    append_vertex(mesh, a, material, u0, v0);
    append_vertex(mesh, b, material, u0, v1);
    append_vertex(mesh, c, material, u1, v1);
    append_vertex(mesh, d, material, u1, v0);
    mesh.indices.insert(mesh.indices.end(), {
        base + 0, base + 1, base + 2,
        base + 0, base + 2, base + 3,
    });
}

void append_vertical_band(
    SectorMesh& mesh,
    const Vec2& p0,
    const Vec2& p1,
    float bottom,
    float top,
    const MaterialView& material
) {
    if (top <= bottom) {
        return;
    }

    const float dx = p1.x - p0.x;
    const float dz = p1.z - p0.z;
    const float length = std::sqrt(dx * dx + dz * dz);
    const float u0 = 0.0f;
    const float u1 = length * material.uv_scale.x;
    const float v0 = bottom * material.uv_scale.z;
    const float v1 = top * material.uv_scale.z;

    append_quad(
        mesh,
        {p0.x, bottom, p0.z},
        {p0.x, top, p0.z},
        {p1.x, top, p1.z},
        {p1.x, bottom, p1.z},
        material,
        u0,
        v0,
        u1,
        v1
    );
}

} // namespace

SectorMesh build_sector_mesh(const SectorWorld& world) {
    SectorMesh mesh;

    for (const Sector& sector : world.sectors()) {
        if (sector.vertices.size() < 3 || sector.ceiling_height <= sector.floor_height) {
            continue;
        }

        const MaterialView floor_material = material_view(
            world,
            sector.floor_material,
            {0.16f, 0.16f, 0.18f, 1.0f}
        );
        const MaterialView ceiling_material = material_view(
            world,
            sector.ceiling_material,
            {0.10f, 0.10f, 0.12f, 1.0f}
        );
        const MaterialView default_wall_material = material_view(
            world,
            sector.wall_material,
            {0.32f, 0.32f, 0.35f, 1.0f}
        );

        // 0.1.x sectors are convex, so deterministic fan triangulation is
        // sufficient. UVs use world X/Z coordinates so neighboring floor
        // polygons line up naturally without renderer-specific knowledge.
        const auto floor_base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (const Vec2& p : sector.vertices) {
            append_vertex(
                mesh,
                {p.x, sector.floor_height, p.z},
                floor_material,
                p.x * floor_material.uv_scale.x,
                p.z * floor_material.uv_scale.z
            );
        }
        for (std::uint32_t i = 1; static_cast<std::size_t>(i + 1) < sector.vertices.size(); ++i) {
            mesh.indices.insert(mesh.indices.end(), {floor_base, floor_base + i + 1, floor_base + i});
        }

        const auto ceiling_base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (const Vec2& p : sector.vertices) {
            append_vertex(
                mesh,
                {p.x, sector.ceiling_height, p.z},
                ceiling_material,
                p.x * ceiling_material.uv_scale.x,
                p.z * ceiling_material.uv_scale.z
            );
        }
        for (std::uint32_t i = 1; static_cast<std::size_t>(i + 1) < sector.vertices.size(); ++i) {
            mesh.indices.insert(mesh.indices.end(), {ceiling_base, ceiling_base + i, ceiling_base + i + 1});
        }

        for (std::size_t i = 0; i < sector.vertices.size(); ++i) {
            const SectorSide* side = sector.sides.size() == sector.vertices.size() ? &sector.sides[i] : nullptr;
            const Vec2 p0 = sector.vertices[i];
            const Vec2 p1 = sector.vertices[(i + 1) % sector.vertices.size()];
            const MaterialView wall_material = side && side->material != kInvalidMaterial
                ? material_view(world, side->material, default_wall_material.color)
                : default_wall_material;

            const bool has_adjacent = side
                && side->adjacent_sector >= 0
                && static_cast<std::size_t>(side->adjacent_sector) < world.sectors().size();

            if (!has_adjacent) {
                append_vertical_band(
                    mesh,
                    p0,
                    p1,
                    sector.floor_height,
                    sector.ceiling_height,
                    wall_material
                );
                continue;
            }

            const Sector& adjacent = world.sectors()[static_cast<std::size_t>(side->adjacent_sector)];
            const float opening_bottom = std::max(sector.floor_height, adjacent.floor_height);
            const float opening_top = std::min(sector.ceiling_height, adjacent.ceiling_height);

            if (opening_top <= opening_bottom) {
                append_vertical_band(
                    mesh,
                    p0,
                    p1,
                    sector.floor_height,
                    sector.ceiling_height,
                    wall_material
                );
                continue;
            }

            append_vertical_band(
                mesh,
                p0,
                p1,
                sector.floor_height,
                std::min(opening_bottom, sector.ceiling_height),
                wall_material
            );
            append_vertical_band(
                mesh,
                p0,
                p1,
                std::max(opening_top, sector.floor_height),
                sector.ceiling_height,
                wall_material
            );
        }
    }

    return mesh;
}

} // namespace vespera
