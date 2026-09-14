#pragma once

#include <vespera/math/types.hpp>

namespace vespera {

struct Camera;

// Renderer-independent perspective frustum used for conservative CPU-side
// visibility tests. The sphere test deliberately errs on the side of drawing:
// touching/intersecting bounds remain visible so culling cannot shave geometry
// at the viewport edge, near plane, or far plane.
struct ViewFrustum {
    Vec3 position{};
    Vec3 forward{0.0f, 0.0f, 1.0f};
    Vec3 right{1.0f, 0.0f, 0.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
    float tan_half_vertical = 1.0f;
    float tan_half_horizontal = 1.0f;
    float near_plane = 0.05f;
    float far_plane = 500.0f;

    [[nodiscard]] bool intersects_sphere(Vec3 center, float radius) const;
};

[[nodiscard]] ViewFrustum make_view_frustum(const Camera& camera, float aspect_ratio);

} // namespace vespera
