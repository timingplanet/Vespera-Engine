#include <vespera/scene/sprite_animation.hpp>

#include <vespera/scene/scene.hpp>
#include <vespera/scene/scene_hierarchy.hpp>

#include <algorithm>
#include <cmath>

namespace vespera {
namespace {

constexpr float kTau = 6.28318530717958647692f;

float wrap_radians(float value) {
    value = std::fmod(value, kTau);
    if (value < 0.0f) value += kTau;
    return value;
}

} // namespace

std::uint32_t sprite_direction_index(
    const Scene& scene,
    const SpriteAnimationClip& clip,
    const Entity& entity,
    float camera_x,
    float camera_z
) {
    if (clip.direction_count <= 1u) return 0u;

    const auto world_transform = entity_world_transform(scene, entity);
    const float dx = camera_x - world_transform.position.x;
    const float dz = camera_z - world_transform.position.z;
    if (std::abs(dx) < 0.00001f && std::abs(dz) < 0.00001f) return 0u;

    const float camera_yaw_from_actor = std::atan2(dx, dz);
    const float relative = wrap_radians(camera_yaw_from_actor - world_transform.rotation.y);
    const float step = kTau / static_cast<float>(clip.direction_count);
    const auto nearest = static_cast<std::uint32_t>(std::floor(relative / step + 0.5f));
    return nearest % clip.direction_count;
}

ResolvedSpriteFrame resolve_sprite_frame(
    const Scene& scene,
    const Entity& entity,
    const SpriteRendererComponent& sprite,
    double total_seconds
) {
    ResolvedSpriteFrame resolved;
    resolved.texture = sprite.texture;

    if (sprite.animation_clip.empty()) return resolved;

    const SpriteAnimationClip* clip = scene.find_sprite_clip(sprite.animation_clip);
    if (!clip || !clip->valid()) return resolved;

    resolved.animated = true;
    resolved.direction_index = sprite_direction_index(
        scene, *clip, entity, scene.camera.position.x, scene.camera.position.z);

    std::uint32_t frame = 0u;
    const float speed = std::max(sprite.animation_speed, 0.0f);
    if (!sprite.animation_paused && clip->frames_per_second > 0.0f && speed > 0.0f && clip->frame_count > 1u) {
        const double local_time = std::max(0.0, total_seconds + static_cast<double>(sprite.animation_time_offset));
        const auto raw = static_cast<std::uint64_t>(std::floor(local_time * clip->frames_per_second * speed));
        frame = clip->loop
            ? static_cast<std::uint32_t>(raw % clip->frame_count)
            : static_cast<std::uint32_t>(std::min<std::uint64_t>(raw, clip->frame_count - 1u));
    }

    resolved.frame_index = frame;
    const TextureId chosen = clip->texture(resolved.direction_index, resolved.frame_index);
    if (chosen != kInvalidTexture) resolved.texture = chosen;
    return resolved;
}

} // namespace vespera
