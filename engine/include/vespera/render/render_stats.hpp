#pragma once

#include <cstdint>

namespace vespera {

// Renderer-independent counters for the most recently submitted frame. These
// are intentionally CPU submission/visibility counters rather than GPU timing;
// backends can populate the same semantic fields without exposing API-specific
// objects to gameplay, telemetry, or automation layers.
struct RenderFrameStats {
    std::uint64_t scene_passes = 0;
    std::uint64_t world_draw_calls = 0;

    std::uint64_t mesh_entities_considered = 0;
    std::uint64_t mesh_instances_visible = 0;
    std::uint64_t mesh_instances_culled = 0;
    std::uint64_t mesh_draw_calls = 0;

    std::uint64_t sprite_entities_considered = 0;
    std::uint64_t sprite_entities_visible = 0;
    std::uint64_t sprite_entities_culled = 0;
    std::uint64_t sprite_draw_calls = 0;

    std::uint64_t point_lights_considered = 0;
    std::uint64_t point_lights_frustum_visible = 0;
    std::uint64_t point_lights_uploaded = 0;

    std::uint64_t ui_draw_calls = 0;

    [[nodiscard]] std::uint64_t total_draw_calls() const {
        return world_draw_calls + mesh_draw_calls + sprite_draw_calls + ui_draw_calls;
    }
};

} // namespace vespera
