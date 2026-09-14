#pragma once

#include <vespera/world/sector_world.hpp>

#include <cstdint>

namespace vespera {

class Scene;
struct Entity;
struct SpriteRendererComponent;
struct SpriteAnimationClip;

struct ResolvedSpriteFrame {
    TextureId texture = kInvalidTexture;
    std::uint32_t direction_index = 0;
    std::uint32_t frame_index = 0;
    bool animated = false;
};

ResolvedSpriteFrame resolve_sprite_frame(
    const Scene& scene,
    const Entity& entity,
    const SpriteRendererComponent& sprite,
    double total_seconds
);

std::uint32_t sprite_direction_index(
    const Scene& scene,
    const SpriteAnimationClip& clip,
    const Entity& entity,
    float camera_x,
    float camera_z
);

} // namespace vespera
