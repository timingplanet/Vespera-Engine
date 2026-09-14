#include <vespera/render/view_frustum.hpp>

#include <vespera/scene/scene.hpp>

#include <algorithm>
#include <cmath>

namespace vespera {
namespace {

float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross(Vec3 a, Vec3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

Vec3 normalized(Vec3 value) {
    const float length_squared = dot(value, value);
    if (length_squared <= 1.0e-12f) return {0.0f, 0.0f, 0.0f};
    const float inverse_length = 1.0f / std::sqrt(length_squared);
    return {value.x * inverse_length, value.y * inverse_length, value.z * inverse_length};
}

} // namespace

ViewFrustum make_view_frustum(const Camera& camera, float aspect_ratio) {
    ViewFrustum frustum;
    frustum.position = camera.position;

    const float cos_pitch = std::cos(camera.pitch);
    frustum.forward = normalized({
        std::sin(camera.yaw) * cos_pitch,
        std::sin(camera.pitch),
        std::cos(camera.yaw) * cos_pitch,
    });

    constexpr Vec3 world_up{0.0f, 1.0f, 0.0f};
    frustum.right = normalized(cross(world_up, frustum.forward));
    if (dot(frustum.right, frustum.right) <= 1.0e-12f) {
        // Looking almost perfectly vertical makes world-up degenerate. Pick a
        // stable horizontal basis; the conservative sphere test remains valid.
        frustum.right = {1.0f, 0.0f, 0.0f};
    }
    frustum.up = normalized(cross(frustum.forward, frustum.right));

    constexpr float degrees_to_radians = 0.01745329251994329577f;
    const float vertical_fov = std::clamp(camera.vertical_fov_degrees, 30.0f, 130.0f) * degrees_to_radians;
    frustum.tan_half_vertical = std::tan(vertical_fov * 0.5f);
    frustum.tan_half_horizontal = frustum.tan_half_vertical * std::max(aspect_ratio, 0.01f);
    frustum.near_plane = std::max(camera.near_plane, 0.001f);
    frustum.far_plane = std::max(camera.far_plane, frustum.near_plane + 1.0f);
    return frustum;
}

bool ViewFrustum::intersects_sphere(Vec3 center, float radius) const {
    radius = std::max(radius, 0.0f);
    const Vec3 delta{
        center.x - position.x,
        center.y - position.y,
        center.z - position.z,
    };

    const float view_x = dot(delta, right);
    const float view_y = dot(delta, up);
    const float view_z = dot(delta, forward);

    if (view_z + radius < near_plane) return false;
    if (view_z - radius > far_plane) return false;

    // Plane equations in view space:
    //   +/-x <= z*tan(horizontal/2)
    //   +/-y <= z*tan(vertical/2)
    // Scale radius by the plane-normal length so this is a true sphere-plane
    // intersection test rather than a center-only approximation.
    const float horizontal_normal_length = std::sqrt(1.0f + tan_half_horizontal * tan_half_horizontal);
    const float vertical_normal_length = std::sqrt(1.0f + tan_half_vertical * tan_half_vertical);
    if (std::abs(view_x) - view_z * tan_half_horizontal > radius * horizontal_normal_length) return false;
    if (std::abs(view_y) - view_z * tan_half_vertical > radius * vertical_normal_length) return false;
    return true;
}

} // namespace vespera
