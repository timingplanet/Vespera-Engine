#include <vespera/render/render_backend.hpp>
#include <vespera/render/vulkan/vulkan_native.hpp>

#include <vespera/core/log.hpp>
#include <vespera/render/view_frustum.hpp>
#include <vespera/scene/scene.hpp>
#include <vespera/scene/scene_hierarchy.hpp>
#include <vespera/scene/sprite_animation.hpp>
#include <vespera/ui/ui_render.hpp>
#include <vespera/world/sector_mesh.hpp>

#include "vulkan_bootstrap_shaders.hpp"
#include "vulkan_lighting_shaders.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace vespera {
namespace {

constexpr std::size_t kFramesInFlight = 2;
constexpr VkDeviceSize kInitialVertexBufferBytes = 8u * 1024u * 1024u;
constexpr VkDeviceSize kInitialIndexBufferBytes = 4u * 1024u * 1024u;
constexpr std::size_t kMaxActivePointLights = 32;
constexpr std::size_t kMaxLightingViewsPerFrame = 8;

struct BootstrapVertex {
    float clip_position[4]{};
    float color[4]{};
    float uv[2]{};
    float texture_layer = 0.0f;
    float world_position[3]{};
    float emission[4]{};
    // x = 1 for Unlit, y reserved, z = alpha cutoff, w reserved.
    float material_params[4]{0.0f, 0.0f, 0.5f, 0.0f};
};

struct FrameGeometry {
    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VkDeviceMemory vertex_memory = VK_NULL_HANDLE;
    void* vertex_mapped = nullptr;
    VkDeviceSize vertex_capacity = 0;

    VkBuffer index_buffer = VK_NULL_HANDLE;
    VkDeviceMemory index_memory = VK_NULL_HANDLE;
    void* index_mapped = nullptr;
    VkDeviceSize index_capacity = 0;

    VkDeviceSize vertex_cursor = 0;
    VkDeviceSize index_cursor = 0;
};

struct GeometryUpload {
    VkDeviceSize vertex_offset = 0;
    VkDeviceSize index_offset = 0;
};

enum class BatchKind : std::uint8_t {
    World,
    Mesh,
    Sprite,
};

struct DrawBatch {
    BatchKind kind = BatchKind::World;
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
};

struct ActivePointLight {
    Vec3 position{};
    float radius = 0.0f;
    std::array<float, 3> color{1.0f, 1.0f, 1.0f};
    float intensity = 0.0f;
    float distance_squared = 0.0f;
};

struct VulkanGpuPointLight {
    float position_radius[4]{0.0f, 0.0f, 0.0f, 1.0f};
    float color_intensity[4]{};
};

struct VulkanLightingConstants {
    float ambient[4]{0.55f, 0.57f, 0.62f, 1.0f};
    std::array<VulkanGpuPointLight, kMaxActivePointLights> lights{};
};

static_assert(sizeof(VulkanGpuPointLight) == 32);
static_assert(sizeof(VulkanLightingConstants) == 16 + kMaxActivePointLights * 32);

struct CameraProjection {
    Vec3 forward{};
    Vec3 right{};
    Vec3 up{};
    float focal = 1.0f;
    float aspect = 1.0f;
    float depth_scale = 1.0f;
    float depth_offset = 0.0f;
};

float dot3(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross3(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

Vec3 normalize3(Vec3 value) {
    const float length_squared = dot3(value, value);
    if (length_squared <= 1.0e-12f) return {1.0f, 0.0f, 0.0f};
    const float inverse_length = 1.0f / std::sqrt(length_squared);
    return {value.x * inverse_length, value.y * inverse_length, value.z * inverse_length};
}

Vec3 rotate_x(Vec3 value, float radians) {
    const float c = std::cos(radians);
    const float sn = std::sin(radians);
    return {value.x, value.y * c - value.z * sn, value.y * sn + value.z * c};
}

Vec3 rotate_y(Vec3 value, float radians) {
    const float c = std::cos(radians);
    const float sn = std::sin(radians);
    return {value.x * c + value.z * sn, value.y, -value.x * sn + value.z * c};
}

Vec3 rotate_z(Vec3 value, float radians) {
    const float c = std::cos(radians);
    const float sn = std::sin(radians);
    return {value.x * c - value.y * sn, value.x * sn + value.y * c, value.z};
}

Vec3 transform_point(Vec3 local, const TransformComponent& transform) {
    Vec3 value{
        local.x * transform.scale.x,
        local.y * transform.scale.y,
        local.z * transform.scale.z,
    };
    value = rotate_x(value, transform.rotation.x);
    value = rotate_y(value, transform.rotation.y);
    value = rotate_z(value, transform.rotation.z);
    value.x += transform.position.x;
    value.y += transform.position.y;
    value.z += transform.position.z;
    return value;
}

CameraProjection make_camera_projection(const Camera& camera, int view_width, int view_height) {
    CameraProjection projection;
    const float cos_pitch = std::cos(camera.pitch);
    projection.forward = normalize3({
        std::sin(camera.yaw) * cos_pitch,
        std::sin(camera.pitch),
        std::cos(camera.yaw) * cos_pitch,
    });
    projection.right = normalize3(cross3({0.0f, 1.0f, 0.0f}, projection.forward));
    if (std::abs(dot3(projection.right, projection.right)) <= 1.0e-8f) {
        projection.right = {1.0f, 0.0f, 0.0f};
    }
    projection.up = normalize3(cross3(projection.forward, projection.right));

    constexpr float kPi = 3.14159265358979323846f;
    const float fov_radians = std::clamp(camera.vertical_fov_degrees, 30.0f, 130.0f) * kPi / 180.0f;
    projection.focal = 1.0f / std::tan(fov_radians * 0.5f);
    projection.aspect = static_cast<float>(std::max(view_width, 1)) / static_cast<float>(std::max(view_height, 1));
    const float near_plane = std::max(camera.near_plane, 0.001f);
    const float far_plane = std::max(camera.far_plane, near_plane + 1.0f);
    projection.depth_scale = far_plane / (far_plane - near_plane);
    projection.depth_offset = -(near_plane * far_plane) / (far_plane - near_plane);
    return projection;
}

BootstrapVertex make_bootstrap_vertex(
    const Vec3& world_position,
    const std::array<float, 4>& color,
    float u,
    float v,
    float texture_layer,
    const Camera& camera,
    const CameraProjection& projection,
    const std::array<float, 4>& emission = {0.0f, 0.0f, 0.0f, 0.0f},
    const std::array<float, 4>& material_params = {0.0f, 0.0f, 0.5f, 0.0f}
) {
    const Vec3 delta{
        world_position.x - camera.position.x,
        world_position.y - camera.position.y,
        world_position.z - camera.position.z,
    };
    const float view_x = dot3(delta, projection.right);
    const float view_y = dot3(delta, projection.up);
    const float view_z = dot3(delta, projection.forward);

    BootstrapVertex vertex{};
    vertex.clip_position[0] = view_x * projection.focal / projection.aspect;
    // Positive-height Vulkan viewports map NDC -Y toward the top of the target.
    vertex.clip_position[1] = -view_y * projection.focal;
    vertex.clip_position[2] = projection.depth_scale * view_z + projection.depth_offset;
    vertex.clip_position[3] = view_z;
    std::copy(color.begin(), color.end(), vertex.color);
    vertex.uv[0] = u;
    vertex.uv[1] = v;
    vertex.texture_layer = texture_layer;
    vertex.world_position[0] = world_position.x;
    vertex.world_position[1] = world_position.y;
    vertex.world_position[2] = world_position.z;
    std::copy(emission.begin(), emission.end(), vertex.emission);
    std::copy(material_params.begin(), material_params.end(), vertex.material_params);
    return vertex;
}

std::array<SectorMesh, 4> build_primitive_meshes() {
    auto append_vertex = [](SectorMesh& mesh, float x, float y, float z, float u, float v) {
        mesh.vertices.push_back({{x, y, z}, {1.0f, 1.0f, 1.0f}, {u, v}, 0.0f});
    };
    auto append_quad = [&](SectorMesh& mesh, Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        append_vertex(mesh, a.x, a.y, a.z, 0.0f, 1.0f);
        append_vertex(mesh, b.x, b.y, b.z, 1.0f, 1.0f);
        append_vertex(mesh, c.x, c.y, c.z, 1.0f, 0.0f);
        append_vertex(mesh, d.x, d.y, d.z, 0.0f, 0.0f);
        mesh.indices.insert(mesh.indices.end(), {base, base + 1u, base + 2u, base, base + 2u, base + 3u});
    };

    std::array<SectorMesh, 4> meshes;
    auto& cube = meshes[static_cast<std::size_t>(PrimitiveMeshType::Cube)];
    append_quad(cube, {-0.5f,-0.5f,-0.5f}, {0.5f,-0.5f,-0.5f}, {0.5f,0.5f,-0.5f}, {-0.5f,0.5f,-0.5f});
    append_quad(cube, {0.5f,-0.5f,0.5f}, {-0.5f,-0.5f,0.5f}, {-0.5f,0.5f,0.5f}, {0.5f,0.5f,0.5f});
    append_quad(cube, {-0.5f,-0.5f,0.5f}, {-0.5f,-0.5f,-0.5f}, {-0.5f,0.5f,-0.5f}, {-0.5f,0.5f,0.5f});
    append_quad(cube, {0.5f,-0.5f,-0.5f}, {0.5f,-0.5f,0.5f}, {0.5f,0.5f,0.5f}, {0.5f,0.5f,-0.5f});
    append_quad(cube, {-0.5f,0.5f,-0.5f}, {0.5f,0.5f,-0.5f}, {0.5f,0.5f,0.5f}, {-0.5f,0.5f,0.5f});
    append_quad(cube, {-0.5f,-0.5f,0.5f}, {0.5f,-0.5f,0.5f}, {0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f,-0.5f});

    auto& plane = meshes[static_cast<std::size_t>(PrimitiveMeshType::Plane)];
    append_quad(plane, {-0.5f,0.0f,-0.5f}, {0.5f,0.0f,-0.5f}, {0.5f,0.0f,0.5f}, {-0.5f,0.0f,0.5f});

    constexpr int segments = 16;
    auto& cylinder = meshes[static_cast<std::size_t>(PrimitiveMeshType::Cylinder)];
    for (int i = 0; i < segments; ++i) {
        const float a0 = 6.28318530718f * static_cast<float>(i) / segments;
        const float a1 = 6.28318530718f * static_cast<float>(i + 1) / segments;
        const float x0 = std::cos(a0) * 0.5f, z0 = std::sin(a0) * 0.5f;
        const float x1 = std::cos(a1) * 0.5f, z1 = std::sin(a1) * 0.5f;
        append_quad(cylinder, {x0,-0.5f,z0}, {x1,-0.5f,z1}, {x1,0.5f,z1}, {x0,0.5f,z0});
        auto base = static_cast<std::uint32_t>(cylinder.vertices.size());
        append_vertex(cylinder, 0.0f, 0.5f, 0.0f, 0.5f, 0.5f);
        append_vertex(cylinder, x0, 0.5f, z0, 0.0f, 0.0f);
        append_vertex(cylinder, x1, 0.5f, z1, 1.0f, 0.0f);
        cylinder.indices.insert(cylinder.indices.end(), {base, base + 1u, base + 2u});
        base = static_cast<std::uint32_t>(cylinder.vertices.size());
        append_vertex(cylinder, 0.0f, -0.5f, 0.0f, 0.5f, 0.5f);
        append_vertex(cylinder, x1, -0.5f, z1, 1.0f, 1.0f);
        append_vertex(cylinder, x0, -0.5f, z0, 0.0f, 1.0f);
        cylinder.indices.insert(cylinder.indices.end(), {base, base + 1u, base + 2u});
    }

    auto& sphere = meshes[static_cast<std::size_t>(PrimitiveMeshType::Sphere)];
    constexpr int stacks = 10;
    constexpr int slices = 16;
    for (int y = 0; y <= stacks; ++y) {
        const float v = static_cast<float>(y) / stacks;
        const float phi = v * 3.14159265359f;
        for (int x = 0; x <= slices; ++x) {
            const float u = static_cast<float>(x) / slices;
            const float theta = u * 6.28318530718f;
            const float sp = std::sin(phi);
            append_vertex(sphere, std::cos(theta) * sp * 0.5f, std::cos(phi) * 0.5f,
                std::sin(theta) * sp * 0.5f, u, 1.0f - v);
        }
    }
    for (int y = 0; y < stacks; ++y) {
        for (int x = 0; x < slices; ++x) {
            const std::uint32_t a = static_cast<std::uint32_t>(y * (slices + 1) + x);
            const std::uint32_t b = a + static_cast<std::uint32_t>(slices + 1);
            sphere.indices.insert(sphere.indices.end(), {a, b, a + 1u, a + 1u, b, b + 1u});
        }
    }
    return meshes;
}

const char* vk_result_name(VkResult result) {
    switch (result) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_EVENT_SET: return "VK_EVENT_SET";
        case VK_EVENT_RESET: return "VK_EVENT_RESET";
        case VK_INCOMPLETE: return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        default: return "VK_UNKNOWN_RESULT";
    }
}

void check_vk(VkResult result, std::string_view operation) {
    if (result == VK_SUCCESS) return;
    throw std::runtime_error(std::format("{} failed with {} ({})", operation, vk_result_name(result), static_cast<int>(result)));
}

template <typename T>
T load_global(PFN_vkGetInstanceProcAddr get_instance_proc_addr, const char* name) {
    return reinterpret_cast<T>(get_instance_proc_addr(VK_NULL_HANDLE, name));
}

template <typename T>
T load_instance(PFN_vkGetInstanceProcAddr get_instance_proc_addr, VkInstance instance, const char* name) {
    return reinterpret_cast<T>(get_instance_proc_addr(instance, name));
}

template <typename T>
T load_device(PFN_vkGetDeviceProcAddr get_device_proc_addr, VkDevice device, const char* name) {
    return reinterpret_cast<T>(get_device_proc_addr(device, name));
}

struct QueueFamilies {
    std::optional<std::uint32_t> graphics;
    std::optional<std::uint32_t> present;

    [[nodiscard]] bool complete() const {
        return graphics.has_value() && present.has_value();
    }
};

struct SwapchainSupport {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> present_modes;
};

struct VulkanFunctions {
    PFN_vkGetInstanceProcAddr get_instance_proc_addr = nullptr;
    PFN_vkCreateInstance create_instance = nullptr;

    PFN_vkDestroyInstance destroy_instance = nullptr;
    PFN_vkEnumeratePhysicalDevices enumerate_physical_devices = nullptr;
    PFN_vkGetPhysicalDeviceProperties get_physical_device_properties = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties get_physical_device_memory_properties = nullptr;
    PFN_vkGetPhysicalDeviceFormatProperties get_physical_device_format_properties = nullptr;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties get_physical_device_queue_family_properties = nullptr;
    PFN_vkEnumerateDeviceExtensionProperties enumerate_device_extension_properties = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR get_physical_device_surface_capabilities = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR get_physical_device_surface_formats = nullptr;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR get_physical_device_surface_present_modes = nullptr;
    PFN_vkCreateDevice create_device = nullptr;
    PFN_vkGetDeviceProcAddr get_device_proc_addr = nullptr;

    PFN_vkDestroyDevice destroy_device = nullptr;
    PFN_vkGetDeviceQueue get_device_queue = nullptr;
    PFN_vkDeviceWaitIdle device_wait_idle = nullptr;
    PFN_vkCreateSwapchainKHR create_swapchain = nullptr;
    PFN_vkDestroySwapchainKHR destroy_swapchain = nullptr;
    PFN_vkGetSwapchainImagesKHR get_swapchain_images = nullptr;
    PFN_vkAcquireNextImageKHR acquire_next_image = nullptr;
    PFN_vkQueuePresentKHR queue_present = nullptr;
    PFN_vkCreateImageView create_image_view = nullptr;
    PFN_vkDestroyImageView destroy_image_view = nullptr;
    PFN_vkCreateImage create_image = nullptr;
    PFN_vkDestroyImage destroy_image = nullptr;
    PFN_vkGetImageMemoryRequirements get_image_memory_requirements = nullptr;
    PFN_vkBindImageMemory bind_image_memory = nullptr;
    PFN_vkCreateBuffer create_buffer = nullptr;
    PFN_vkDestroyBuffer destroy_buffer = nullptr;
    PFN_vkGetBufferMemoryRequirements get_buffer_memory_requirements = nullptr;
    PFN_vkAllocateMemory allocate_memory = nullptr;
    PFN_vkFreeMemory free_memory = nullptr;
    PFN_vkBindBufferMemory bind_buffer_memory = nullptr;
    PFN_vkMapMemory map_memory = nullptr;
    PFN_vkUnmapMemory unmap_memory = nullptr;
    PFN_vkCreateSampler create_sampler = nullptr;
    PFN_vkDestroySampler destroy_sampler = nullptr;
    PFN_vkCreateDescriptorSetLayout create_descriptor_set_layout = nullptr;
    PFN_vkDestroyDescriptorSetLayout destroy_descriptor_set_layout = nullptr;
    PFN_vkCreateDescriptorPool create_descriptor_pool = nullptr;
    PFN_vkDestroyDescriptorPool destroy_descriptor_pool = nullptr;
    PFN_vkAllocateDescriptorSets allocate_descriptor_sets = nullptr;
    PFN_vkUpdateDescriptorSets update_descriptor_sets = nullptr;
    PFN_vkCreateShaderModule create_shader_module = nullptr;
    PFN_vkDestroyShaderModule destroy_shader_module = nullptr;
    PFN_vkCreatePipelineLayout create_pipeline_layout = nullptr;
    PFN_vkDestroyPipelineLayout destroy_pipeline_layout = nullptr;
    PFN_vkCreateGraphicsPipelines create_graphics_pipelines = nullptr;
    PFN_vkDestroyPipeline destroy_pipeline = nullptr;
    PFN_vkCreateRenderPass create_render_pass = nullptr;
    PFN_vkDestroyRenderPass destroy_render_pass = nullptr;
    PFN_vkCreateFramebuffer create_framebuffer = nullptr;
    PFN_vkDestroyFramebuffer destroy_framebuffer = nullptr;
    PFN_vkCreateCommandPool create_command_pool = nullptr;
    PFN_vkDestroyCommandPool destroy_command_pool = nullptr;
    PFN_vkAllocateCommandBuffers allocate_command_buffers = nullptr;
    PFN_vkFreeCommandBuffers free_command_buffers = nullptr;
    PFN_vkResetCommandBuffer reset_command_buffer = nullptr;
    PFN_vkBeginCommandBuffer begin_command_buffer = nullptr;
    PFN_vkEndCommandBuffer end_command_buffer = nullptr;
    PFN_vkCmdBeginRenderPass cmd_begin_render_pass = nullptr;
    PFN_vkCmdEndRenderPass cmd_end_render_pass = nullptr;
    PFN_vkCmdClearAttachments cmd_clear_attachments = nullptr;
    PFN_vkCmdBindPipeline cmd_bind_pipeline = nullptr;
    PFN_vkCmdSetViewport cmd_set_viewport = nullptr;
    PFN_vkCmdSetScissor cmd_set_scissor = nullptr;
    PFN_vkCmdBindVertexBuffers cmd_bind_vertex_buffers = nullptr;
    PFN_vkCmdBindIndexBuffer cmd_bind_index_buffer = nullptr;
    PFN_vkCmdBindDescriptorSets cmd_bind_descriptor_sets = nullptr;
    PFN_vkCmdPipelineBarrier cmd_pipeline_barrier = nullptr;
    PFN_vkCmdCopyBufferToImage cmd_copy_buffer_to_image = nullptr;
    PFN_vkCmdDrawIndexed cmd_draw_indexed = nullptr;
    PFN_vkCmdDraw cmd_draw = nullptr;
    PFN_vkCreateSemaphore create_semaphore = nullptr;
    PFN_vkDestroySemaphore destroy_semaphore = nullptr;
    PFN_vkCreateFence create_fence = nullptr;
    PFN_vkDestroyFence destroy_fence = nullptr;
    PFN_vkWaitForFences wait_for_fences = nullptr;
    PFN_vkResetFences reset_fences = nullptr;
    PFN_vkQueueSubmit queue_submit = nullptr;
};

class VulkanRenderer final : public RenderBackend, public VulkanNativeAccess {
public:
    ~VulkanRenderer() override {
        shutdown();
    }

    bool initialize(SDL_Window* window) override {
        window_ = window;
        if (!window_) {
            log::error("Vulkan initialization failed: window is null.");
            return false;
        }

        try {
            // SDL_WINDOW_VULKAN loads the platform Vulkan loader before the renderer
            // is initialized. Retrieve dispatch through SDL so Vespera does not link
            // directly against a machine-specific Vulkan loader library.
            functions_.get_instance_proc_addr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
                SDL_Vulkan_GetVkGetInstanceProcAddr());
            if (!functions_.get_instance_proc_addr) {
                throw std::runtime_error(std::format(
                    "SDL_Vulkan_GetVkGetInstanceProcAddr failed: {}", SDL_GetError()));
            }

            functions_.create_instance = load_global<PFN_vkCreateInstance>(
                functions_.get_instance_proc_addr, "vkCreateInstance");
            if (!functions_.create_instance) {
                throw std::runtime_error("Vulkan loader did not expose vkCreateInstance.");
            }

            create_instance();
            load_instance_functions();

            if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_)) {
                throw std::runtime_error(std::format("SDL_Vulkan_CreateSurface failed: {}", SDL_GetError()));
            }

            choose_physical_device();
            create_device();
            load_device_functions();
            create_command_resources();
            create_sync_objects();
            create_geometry_buffers();
            create_world_descriptor_resources();
            create_ui_descriptor_resources();
            create_swapchain_resources();
            query_capabilities();

            log::info(std::format(
                "Vulkan initialized on '{}' (API {}.{}, {}x{}, vsync={}).",
                capabilities_.adapter_name,
                capabilities_.feature_level_major,
                capabilities_.feature_level_minor,
                width_,
                height_,
                vsync_enabled_ ? "on" : "off"));
            log::info("Vulkan renderer ready: sector geometry, primitive entities, sprites, per-pixel point lighting, runtime UI, depth, and world materials enabled.");
            return true;
        } catch (const std::exception& ex) {
            log::error(std::format("Vulkan initialization failed: {}", ex.what()));
            shutdown();
            return false;
        }
    }

    void resize(int pixel_width, int pixel_height) override {
        pending_width_ = std::max(pixel_width, 0);
        pending_height_ = std::max(pixel_height, 0);
        if (!device_ || pixel_width <= 0 || pixel_height <= 0) {
            swapchain_recreate_pending_ = true;
            return;
        }
        if (pixel_width == width_ && pixel_height == height_) return;
        swapchain_recreate_pending_ = true;
    }

    void set_vsync_enabled(bool enabled) override {
        if (vsync_enabled_ == enabled) return;
        vsync_enabled_ = enabled;
        if (device_) swapchain_recreate_pending_ = true;
    }

    [[nodiscard]] bool vsync_enabled() const override { return vsync_enabled_; }
    [[nodiscard]] const RenderFrameStats& frame_stats() const override { return frame_stats_; }

    bool begin_frame(const RenderFrameConfig& config = {}) override {
        if (!device_ || frame_open_) return false;

        if (swapchain_recreate_pending_ || !swapchain_) {
            if (!recreate_swapchain()) return false;
        }
        if (width_ <= 0 || height_ <= 0 || swapchain_framebuffers_.empty()) return false;

        const std::size_t frame = current_frame_;
        try {
            check_vk(functions_.wait_for_fences(
                device_, 1, &in_flight_fences_[frame], VK_TRUE, std::numeric_limits<std::uint64_t>::max()),
                "vkWaitForFences");

            const VkResult acquire = functions_.acquire_next_image(
                device_, swapchain_, std::numeric_limits<std::uint64_t>::max(),
                image_available_semaphores_[frame], VK_NULL_HANDLE, &image_index_);
            if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
                swapchain_recreate_pending_ = true;
                return false;
            }
            if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
                check_vk(acquire, "vkAcquireNextImageKHR");
            }
            if (acquire == VK_SUBOPTIMAL_KHR) {
                swapchain_recreate_pending_ = true;
            }

            check_vk(functions_.reset_fences(device_, 1, &in_flight_fences_[frame]), "vkResetFences");
            check_vk(functions_.reset_command_buffer(command_buffers_[frame], 0), "vkResetCommandBuffer");
            frame_geometry_[frame].vertex_cursor = 0;
            frame_geometry_[frame].index_cursor = 0;
            lighting_view_cursor_ = 0;

            VkCommandBufferBeginInfo begin_info{};
            begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            check_vk(functions_.begin_command_buffer(command_buffers_[frame], &begin_info), "vkBeginCommandBuffer");

            current_clear_color_ = config.clear_color;
            std::array<VkClearValue, 2> clear_values{};
            std::copy(current_clear_color_.begin(), current_clear_color_.end(), clear_values[0].color.float32);
            clear_values[1].depthStencil.depth = 1.0f;
            clear_values[1].depthStencil.stencil = 0;

            VkRenderPassBeginInfo render_pass_info{};
            render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            render_pass_info.renderPass = render_pass_;
            render_pass_info.framebuffer = swapchain_framebuffers_.at(image_index_);
            render_pass_info.renderArea.offset = {0, 0};
            render_pass_info.renderArea.extent = swapchain_extent_;
            render_pass_info.clearValueCount = static_cast<std::uint32_t>(clear_values.size());
            render_pass_info.pClearValues = clear_values.data();
            functions_.cmd_begin_render_pass(command_buffers_[frame], &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

            frame_stats_ = {};
            frame_open_ = true;
            return true;
        } catch (const std::exception& ex) {
            log::error(std::format("Vulkan begin frame failed: {}", ex.what()));
            recover_current_frame_fence();
            frame_open_ = false;
            return false;
        }
    }

    void render_scene(
        const Scene& scene,
        double total_seconds,
        const Camera* camera_override,
        const RenderViewport* requested_viewport) override {
        if (!frame_open_ || !graphics_pipeline_ || !sprite_pipeline_ || !device_ || width_ <= 0 || height_ <= 0) return;

        try {
            int view_x = 0;
            int view_y = 0;
            int view_width = width_;
            int view_height = height_;
            if (requested_viewport && requested_viewport->width > 0 && requested_viewport->height > 0) {
                view_x = std::clamp(requested_viewport->x, 0, width_);
                view_y = std::clamp(requested_viewport->y, 0, height_);
                view_width = std::clamp(requested_viewport->width, 0, width_ - view_x);
                view_height = std::clamp(requested_viewport->height, 0, height_ - view_y);
                if (view_width <= 0 || view_height <= 0) return;
                clear_render_viewport(view_x, view_y, view_width, view_height);
            }

            ensure_world_mesh(scene.world);
            ++frame_stats_.scene_passes;
            const Camera& camera = camera_override ? *camera_override : scene.camera;
            const float aspect = static_cast<float>(view_width) / static_cast<float>(std::max(view_height, 1));
            const ViewFrustum frustum = make_view_frustum(camera, aspect);
            const auto active_lights = collect_active_lights(scene, camera, frustum);
            const VkDescriptorSet world_descriptor_set = upload_lighting_constants(active_lights);
            if (!world_descriptor_set) return;
            const CameraProjection projection = make_camera_projection(camera, view_width, view_height);
            build_scene_geometry(scene, total_seconds, camera, projection, frustum);
            if (scene_vertices_.empty() || scene_indices_.empty() || scene_batches_.empty()) return;

            const auto upload = upload_dynamic_geometry(scene_vertices_, scene_indices_);
            if (!upload) return;

            VkViewport viewport{};
            viewport.x = static_cast<float>(view_x);
            viewport.y = static_cast<float>(view_y);
            viewport.width = static_cast<float>(view_width);
            viewport.height = static_cast<float>(view_height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;

            VkRect2D scissor{};
            scissor.offset = {view_x, view_y};
            scissor.extent = {static_cast<std::uint32_t>(view_width), static_cast<std::uint32_t>(view_height)};

            const VkCommandBuffer command_buffer = command_buffers_[current_frame_];
            if (!world_texture_view_ || !world_texture_sampler_) return;
            functions_.cmd_bind_descriptor_sets(
                command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_,
                0, 1, &world_descriptor_set, 0, nullptr);
            functions_.cmd_set_viewport(command_buffer, 0, 1, &viewport);
            functions_.cmd_set_scissor(command_buffer, 0, 1, &scissor);

            const VkBuffer vertex_buffer = frame_geometry_[current_frame_].vertex_buffer;
            const VkDeviceSize vertex_offset = upload->vertex_offset;
            functions_.cmd_bind_vertex_buffers(command_buffer, 0, 1, &vertex_buffer, &vertex_offset);
            functions_.cmd_bind_index_buffer(
                command_buffer, frame_geometry_[current_frame_].index_buffer,
                upload->index_offset, VK_INDEX_TYPE_UINT32);

            VkPipeline bound_pipeline = VK_NULL_HANDLE;
            for (const DrawBatch& batch : scene_batches_) {
                const VkPipeline pipeline = batch.kind == BatchKind::Sprite ? sprite_pipeline_ : graphics_pipeline_;
                if (pipeline != bound_pipeline) {
                    functions_.cmd_bind_pipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                    bound_pipeline = pipeline;
                }
                functions_.cmd_draw_indexed(command_buffer, batch.index_count, 1, batch.first_index, 0, 0);
                switch (batch.kind) {
                    case BatchKind::World: ++frame_stats_.world_draw_calls; break;
                    case BatchKind::Mesh: ++frame_stats_.mesh_draw_calls; break;
                    case BatchKind::Sprite: ++frame_stats_.sprite_draw_calls; break;
                }
            }
        } catch (const std::exception& ex) {
            log::error(std::format("Vulkan scene render failed: {}", ex.what()));
        }
    }

    void render_ui(
        const UiRenderPacket& packet,
        const RenderViewport* requested_viewport = nullptr
    ) override {
        if (!frame_open_ || !device_ || !ui_pipeline_ || packet.vertices.empty() || !packet.atlas || !packet.atlas->valid()) {
            return;
        }

        try {
            int view_x = 0;
            int view_y = 0;
            int view_width = width_;
            int view_height = height_;
            if (requested_viewport && requested_viewport->width > 0 && requested_viewport->height > 0) {
                view_x = std::clamp(requested_viewport->x, 0, width_);
                view_y = std::clamp(requested_viewport->y, 0, height_);
                view_width = std::clamp(requested_viewport->width, 0, width_ - view_x);
                view_height = std::clamp(requested_viewport->height, 0, height_ - view_y);
                if (view_width <= 0 || view_height <= 0) return;
            }

            ensure_ui_atlas(packet);
            if (!ui_descriptor_set_ || !ui_atlas_view_ || !ui_sampler_) return;

            ui_vertices_.clear();
            ui_vertices_.reserve(packet.vertices.size());
            const float safe_width = static_cast<float>(std::max(view_width, 1));
            const float safe_height = static_cast<float>(std::max(view_height, 1));
            for (const UiDrawVertex& source : packet.vertices) {
                BootstrapVertex vertex{};
                vertex.clip_position[0] = (source.x / safe_width) * 2.0f - 1.0f;
                vertex.clip_position[1] = (source.y / safe_height) * 2.0f - 1.0f;
                vertex.clip_position[2] = 0.0f;
                vertex.clip_position[3] = 1.0f;
                vertex.color[0] = static_cast<float>(source.color_rgba8 & 0xFFu) / 255.0f;
                vertex.color[1] = static_cast<float>((source.color_rgba8 >> 8u) & 0xFFu) / 255.0f;
                vertex.color[2] = static_cast<float>((source.color_rgba8 >> 16u) & 0xFFu) / 255.0f;
                vertex.color[3] = static_cast<float>((source.color_rgba8 >> 24u) & 0xFFu) / 255.0f;
                vertex.uv[0] = source.u;
                vertex.uv[1] = source.v;
                vertex.texture_layer = 0.0f;
                ui_vertices_.push_back(vertex);
            }

            const auto upload = upload_dynamic_vertices(ui_vertices_);
            if (!upload) return;

            VkViewport viewport{};
            viewport.x = static_cast<float>(view_x);
            viewport.y = static_cast<float>(view_y);
            viewport.width = static_cast<float>(view_width);
            viewport.height = static_cast<float>(view_height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            VkRect2D scissor{};
            scissor.offset = {view_x, view_y};
            scissor.extent = {static_cast<std::uint32_t>(view_width), static_cast<std::uint32_t>(view_height)};

            const VkCommandBuffer command_buffer = command_buffers_[current_frame_];
            functions_.cmd_bind_pipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ui_pipeline_);
            functions_.cmd_bind_descriptor_sets(
                command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ui_pipeline_layout_,
                0, 1, &ui_descriptor_set_, 0, nullptr);
            functions_.cmd_set_viewport(command_buffer, 0, 1, &viewport);
            functions_.cmd_set_scissor(command_buffer, 0, 1, &scissor);
            const VkBuffer vertex_buffer = frame_geometry_[current_frame_].vertex_buffer;
            const VkDeviceSize vertex_offset = *upload;
            functions_.cmd_bind_vertex_buffers(command_buffer, 0, 1, &vertex_buffer, &vertex_offset);
            functions_.cmd_draw(command_buffer, static_cast<std::uint32_t>(ui_vertices_.size()), 1, 0, 0);
            ++frame_stats_.ui_draw_calls;
        } catch (const std::exception& ex) {
            log::error(std::format("Vulkan UI render failed: {}", ex.what()));
        }
    }

    bool end_frame() override {
        if (!frame_open_ || !device_) return false;

        const std::size_t frame = current_frame_;
        frame_open_ = false;
        bool submitted = false;
        try {
            functions_.cmd_end_render_pass(command_buffers_[frame]);
            check_vk(functions_.end_command_buffer(command_buffers_[frame]), "vkEndCommandBuffer");

            constexpr VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkSubmitInfo submit_info{};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit_info.waitSemaphoreCount = 1;
            submit_info.pWaitSemaphores = &image_available_semaphores_[frame];
            submit_info.pWaitDstStageMask = &wait_stage;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &command_buffers_[frame];
            submit_info.signalSemaphoreCount = 1;
            submit_info.pSignalSemaphores = &render_finished_semaphores_[frame];
            check_vk(functions_.queue_submit(graphics_queue_, 1, &submit_info, in_flight_fences_[frame]), "vkQueueSubmit");
            submitted = true;

            VkPresentInfoKHR present_info{};
            present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            present_info.waitSemaphoreCount = 1;
            present_info.pWaitSemaphores = &render_finished_semaphores_[frame];
            present_info.swapchainCount = 1;
            present_info.pSwapchains = &swapchain_;
            present_info.pImageIndices = &image_index_;

            const VkResult present = functions_.queue_present(present_queue_, &present_info);
            if (present == VK_ERROR_OUT_OF_DATE_KHR || present == VK_SUBOPTIMAL_KHR) {
                swapchain_recreate_pending_ = true;
            } else if (present != VK_SUCCESS) {
                check_vk(present, "vkQueuePresentKHR");
            }

            current_frame_ = (current_frame_ + 1) % kFramesInFlight;
            return true;
        } catch (const std::exception& ex) {
            log::error(std::format("Vulkan end frame failed: {}", ex.what()));
            if (!submitted) {
                recover_current_frame_fence();
            } else if (functions_.device_wait_idle) {
                // The current fence may still be owned by a successful queue
                // submission. Do not destroy/recreate it until the device is idle.
                (void)functions_.device_wait_idle(device_);
            }
            return false;
        }
    }

    [[nodiscard]] int target_width() const override { return width_; }
    [[nodiscard]] int target_height() const override { return height_; }

    [[nodiscard]] std::uint32_t vulkan_api_version() const override { return VK_API_VERSION_1_0; }
    [[nodiscard]] VkInstance vulkan_instance() const override { return instance_; }
    [[nodiscard]] VkPhysicalDevice vulkan_physical_device() const override { return physical_device_; }
    [[nodiscard]] VkDevice vulkan_device() const override { return device_; }
    [[nodiscard]] std::uint32_t vulkan_graphics_queue_family() const override {
        return queue_families_.graphics.value_or(0);
    }
    [[nodiscard]] VkQueue vulkan_graphics_queue() const override { return graphics_queue_; }
    [[nodiscard]] VkRenderPass vulkan_render_pass() const override { return render_pass_; }
    [[nodiscard]] VkCommandBuffer vulkan_active_command_buffer() const override {
        if (!frame_open_) return VK_NULL_HANDLE;
        return command_buffers_[current_frame_];
    }
    [[nodiscard]] std::uint32_t vulkan_min_image_count() const override {
        return swapchain_min_image_count_;
    }
    [[nodiscard]] std::uint32_t vulkan_image_count() const override {
        return static_cast<std::uint32_t>(swapchain_images_.size());
    }
    [[nodiscard]] PFN_vkVoidFunction vulkan_load_function(const char* name) const override {
        if (!name || !*name) return nullptr;
        if (device_ && functions_.get_device_proc_addr) {
            if (PFN_vkVoidFunction fn = functions_.get_device_proc_addr(device_, name)) return fn;
        }
        if (instance_ && functions_.get_instance_proc_addr) {
            if (PFN_vkVoidFunction fn = functions_.get_instance_proc_addr(instance_, name)) return fn;
        }
        if (functions_.get_instance_proc_addr) {
            return functions_.get_instance_proc_addr(VK_NULL_HANDLE, name);
        }
        return nullptr;
    }
    void vulkan_wait_for_gpu() override {
        if (device_ && functions_.device_wait_idle) {
            check_vk(functions_.device_wait_idle(device_), "vkDeviceWaitIdle(editor overlay)");
        }
    }

    void shutdown() override {
        if (device_ && functions_.device_wait_idle) {
            (void)functions_.device_wait_idle(device_);
        }

        destroy_swapchain_resources();
        destroy_ui_atlas_texture();
        destroy_world_texture_array();
        destroy_ui_descriptor_resources();
        destroy_world_descriptor_resources();
        destroy_geometry_buffers();
        destroy_sync_objects();

        if (device_ && upload_command_pool_ && functions_.destroy_command_pool) {
            functions_.destroy_command_pool(device_, upload_command_pool_, nullptr);
        }
        upload_command_pool_ = VK_NULL_HANDLE;
        if (device_ && command_pool_ && functions_.destroy_command_pool) {
            functions_.destroy_command_pool(device_, command_pool_, nullptr);
        }
        command_pool_ = VK_NULL_HANDLE;
        command_buffers_.fill(VK_NULL_HANDLE);

        if (device_ && functions_.destroy_device) {
            functions_.destroy_device(device_, nullptr);
        }
        device_ = VK_NULL_HANDLE;
        graphics_queue_ = VK_NULL_HANDLE;
        present_queue_ = VK_NULL_HANDLE;

        if (surface_ && instance_) {
            SDL_Vulkan_DestroySurface(instance_, surface_, nullptr);
        }
        surface_ = VK_NULL_HANDLE;

        if (instance_ && functions_.destroy_instance) {
            functions_.destroy_instance(instance_, nullptr);
        }
        instance_ = VK_NULL_HANDLE;
        physical_device_ = VK_NULL_HANDLE;

        window_ = nullptr;
        width_ = 0;
        height_ = 0;
        pending_width_ = 0;
        pending_height_ = 0;
        frame_open_ = false;
        swapchain_recreate_pending_ = false;
        current_frame_ = 0;
        image_index_ = 0;
        capabilities_ = {};
        frame_stats_ = {};
        queue_families_ = {};
        cached_world_ = nullptr;
        cached_world_revision_ = 0;
        world_mesh_ = {};
        scene_vertices_.clear();
        scene_indices_.clear();
        scene_batches_.clear();
        ui_vertices_.clear();
        geometry_capacity_warning_emitted_ = false;
        world_texture_width_ = 0;
        world_texture_height_ = 0;
        world_texture_layers_ = 0;
        functions_ = {};
    }

    [[nodiscard]] std::string_view name() const override { return "Vulkan"; }
    [[nodiscard]] const RendererCapabilities& capabilities() const override { return capabilities_; }

private:
    void create_instance() {
        Uint32 extension_count = 0;
        const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&extension_count);
        if (!extensions || extension_count == 0) {
            throw std::runtime_error(std::format(
                "SDL_Vulkan_GetInstanceExtensions failed: {}", SDL_GetError()));
        }

        VkApplicationInfo application_info{};
        application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        application_info.pApplicationName = "Vespera";
        application_info.applicationVersion = VK_MAKE_API_VERSION(0, 1, 1, 0);
        application_info.pEngineName = "Vespera Engine";
        application_info.engineVersion = VK_MAKE_API_VERSION(0, 1, 1, 0);
        application_info.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        create_info.pApplicationInfo = &application_info;
        create_info.enabledExtensionCount = extension_count;
        create_info.ppEnabledExtensionNames = extensions;

        check_vk(functions_.create_instance(&create_info, nullptr, &instance_), "vkCreateInstance");
    }

    void load_instance_functions() {
#define VESPERA_LOAD_INSTANCE(member, type, name) \
        functions_.member = load_instance<type>(functions_.get_instance_proc_addr, instance_, name); \
        if (!functions_.member) throw std::runtime_error(std::string("Vulkan loader did not expose ") + name + ".")

        VESPERA_LOAD_INSTANCE(destroy_instance, PFN_vkDestroyInstance, "vkDestroyInstance");
        VESPERA_LOAD_INSTANCE(enumerate_physical_devices, PFN_vkEnumeratePhysicalDevices, "vkEnumeratePhysicalDevices");
        VESPERA_LOAD_INSTANCE(get_physical_device_properties, PFN_vkGetPhysicalDeviceProperties, "vkGetPhysicalDeviceProperties");
        VESPERA_LOAD_INSTANCE(get_physical_device_memory_properties, PFN_vkGetPhysicalDeviceMemoryProperties, "vkGetPhysicalDeviceMemoryProperties");
        VESPERA_LOAD_INSTANCE(get_physical_device_format_properties, PFN_vkGetPhysicalDeviceFormatProperties, "vkGetPhysicalDeviceFormatProperties");
        VESPERA_LOAD_INSTANCE(get_physical_device_queue_family_properties, PFN_vkGetPhysicalDeviceQueueFamilyProperties, "vkGetPhysicalDeviceQueueFamilyProperties");
        VESPERA_LOAD_INSTANCE(enumerate_device_extension_properties, PFN_vkEnumerateDeviceExtensionProperties, "vkEnumerateDeviceExtensionProperties");
        VESPERA_LOAD_INSTANCE(get_physical_device_surface_capabilities, PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        VESPERA_LOAD_INSTANCE(get_physical_device_surface_formats, PFN_vkGetPhysicalDeviceSurfaceFormatsKHR, "vkGetPhysicalDeviceSurfaceFormatsKHR");
        VESPERA_LOAD_INSTANCE(get_physical_device_surface_present_modes, PFN_vkGetPhysicalDeviceSurfacePresentModesKHR, "vkGetPhysicalDeviceSurfacePresentModesKHR");
        VESPERA_LOAD_INSTANCE(create_device, PFN_vkCreateDevice, "vkCreateDevice");
        VESPERA_LOAD_INSTANCE(get_device_proc_addr, PFN_vkGetDeviceProcAddr, "vkGetDeviceProcAddr");
#undef VESPERA_LOAD_INSTANCE
    }

    void load_device_functions() {
#define VESPERA_LOAD_DEVICE(member, type, name) \
        functions_.member = load_device<type>(functions_.get_device_proc_addr, device_, name); \
        if (!functions_.member) throw std::runtime_error(std::string("Vulkan device did not expose ") + name + ".")

        VESPERA_LOAD_DEVICE(destroy_device, PFN_vkDestroyDevice, "vkDestroyDevice");
        VESPERA_LOAD_DEVICE(get_device_queue, PFN_vkGetDeviceQueue, "vkGetDeviceQueue");
        VESPERA_LOAD_DEVICE(device_wait_idle, PFN_vkDeviceWaitIdle, "vkDeviceWaitIdle");
        VESPERA_LOAD_DEVICE(create_swapchain, PFN_vkCreateSwapchainKHR, "vkCreateSwapchainKHR");
        VESPERA_LOAD_DEVICE(destroy_swapchain, PFN_vkDestroySwapchainKHR, "vkDestroySwapchainKHR");
        VESPERA_LOAD_DEVICE(get_swapchain_images, PFN_vkGetSwapchainImagesKHR, "vkGetSwapchainImagesKHR");
        VESPERA_LOAD_DEVICE(acquire_next_image, PFN_vkAcquireNextImageKHR, "vkAcquireNextImageKHR");
        VESPERA_LOAD_DEVICE(queue_present, PFN_vkQueuePresentKHR, "vkQueuePresentKHR");
        VESPERA_LOAD_DEVICE(create_image_view, PFN_vkCreateImageView, "vkCreateImageView");
        VESPERA_LOAD_DEVICE(destroy_image_view, PFN_vkDestroyImageView, "vkDestroyImageView");
        VESPERA_LOAD_DEVICE(create_image, PFN_vkCreateImage, "vkCreateImage");
        VESPERA_LOAD_DEVICE(destroy_image, PFN_vkDestroyImage, "vkDestroyImage");
        VESPERA_LOAD_DEVICE(get_image_memory_requirements, PFN_vkGetImageMemoryRequirements, "vkGetImageMemoryRequirements");
        VESPERA_LOAD_DEVICE(bind_image_memory, PFN_vkBindImageMemory, "vkBindImageMemory");
        VESPERA_LOAD_DEVICE(create_buffer, PFN_vkCreateBuffer, "vkCreateBuffer");
        VESPERA_LOAD_DEVICE(destroy_buffer, PFN_vkDestroyBuffer, "vkDestroyBuffer");
        VESPERA_LOAD_DEVICE(get_buffer_memory_requirements, PFN_vkGetBufferMemoryRequirements, "vkGetBufferMemoryRequirements");
        VESPERA_LOAD_DEVICE(allocate_memory, PFN_vkAllocateMemory, "vkAllocateMemory");
        VESPERA_LOAD_DEVICE(free_memory, PFN_vkFreeMemory, "vkFreeMemory");
        VESPERA_LOAD_DEVICE(bind_buffer_memory, PFN_vkBindBufferMemory, "vkBindBufferMemory");
        VESPERA_LOAD_DEVICE(map_memory, PFN_vkMapMemory, "vkMapMemory");
        VESPERA_LOAD_DEVICE(unmap_memory, PFN_vkUnmapMemory, "vkUnmapMemory");
        VESPERA_LOAD_DEVICE(create_sampler, PFN_vkCreateSampler, "vkCreateSampler");
        VESPERA_LOAD_DEVICE(destroy_sampler, PFN_vkDestroySampler, "vkDestroySampler");
        VESPERA_LOAD_DEVICE(create_descriptor_set_layout, PFN_vkCreateDescriptorSetLayout, "vkCreateDescriptorSetLayout");
        VESPERA_LOAD_DEVICE(destroy_descriptor_set_layout, PFN_vkDestroyDescriptorSetLayout, "vkDestroyDescriptorSetLayout");
        VESPERA_LOAD_DEVICE(create_descriptor_pool, PFN_vkCreateDescriptorPool, "vkCreateDescriptorPool");
        VESPERA_LOAD_DEVICE(destroy_descriptor_pool, PFN_vkDestroyDescriptorPool, "vkDestroyDescriptorPool");
        VESPERA_LOAD_DEVICE(allocate_descriptor_sets, PFN_vkAllocateDescriptorSets, "vkAllocateDescriptorSets");
        VESPERA_LOAD_DEVICE(update_descriptor_sets, PFN_vkUpdateDescriptorSets, "vkUpdateDescriptorSets");
        VESPERA_LOAD_DEVICE(create_shader_module, PFN_vkCreateShaderModule, "vkCreateShaderModule");
        VESPERA_LOAD_DEVICE(destroy_shader_module, PFN_vkDestroyShaderModule, "vkDestroyShaderModule");
        VESPERA_LOAD_DEVICE(create_pipeline_layout, PFN_vkCreatePipelineLayout, "vkCreatePipelineLayout");
        VESPERA_LOAD_DEVICE(destroy_pipeline_layout, PFN_vkDestroyPipelineLayout, "vkDestroyPipelineLayout");
        VESPERA_LOAD_DEVICE(create_graphics_pipelines, PFN_vkCreateGraphicsPipelines, "vkCreateGraphicsPipelines");
        VESPERA_LOAD_DEVICE(destroy_pipeline, PFN_vkDestroyPipeline, "vkDestroyPipeline");
        VESPERA_LOAD_DEVICE(create_render_pass, PFN_vkCreateRenderPass, "vkCreateRenderPass");
        VESPERA_LOAD_DEVICE(destroy_render_pass, PFN_vkDestroyRenderPass, "vkDestroyRenderPass");
        VESPERA_LOAD_DEVICE(create_framebuffer, PFN_vkCreateFramebuffer, "vkCreateFramebuffer");
        VESPERA_LOAD_DEVICE(destroy_framebuffer, PFN_vkDestroyFramebuffer, "vkDestroyFramebuffer");
        VESPERA_LOAD_DEVICE(create_command_pool, PFN_vkCreateCommandPool, "vkCreateCommandPool");
        VESPERA_LOAD_DEVICE(destroy_command_pool, PFN_vkDestroyCommandPool, "vkDestroyCommandPool");
        VESPERA_LOAD_DEVICE(allocate_command_buffers, PFN_vkAllocateCommandBuffers, "vkAllocateCommandBuffers");
        VESPERA_LOAD_DEVICE(free_command_buffers, PFN_vkFreeCommandBuffers, "vkFreeCommandBuffers");
        VESPERA_LOAD_DEVICE(reset_command_buffer, PFN_vkResetCommandBuffer, "vkResetCommandBuffer");
        VESPERA_LOAD_DEVICE(begin_command_buffer, PFN_vkBeginCommandBuffer, "vkBeginCommandBuffer");
        VESPERA_LOAD_DEVICE(end_command_buffer, PFN_vkEndCommandBuffer, "vkEndCommandBuffer");
        VESPERA_LOAD_DEVICE(cmd_begin_render_pass, PFN_vkCmdBeginRenderPass, "vkCmdBeginRenderPass");
        VESPERA_LOAD_DEVICE(cmd_end_render_pass, PFN_vkCmdEndRenderPass, "vkCmdEndRenderPass");
        VESPERA_LOAD_DEVICE(cmd_clear_attachments, PFN_vkCmdClearAttachments, "vkCmdClearAttachments");
        VESPERA_LOAD_DEVICE(cmd_bind_pipeline, PFN_vkCmdBindPipeline, "vkCmdBindPipeline");
        VESPERA_LOAD_DEVICE(cmd_set_viewport, PFN_vkCmdSetViewport, "vkCmdSetViewport");
        VESPERA_LOAD_DEVICE(cmd_set_scissor, PFN_vkCmdSetScissor, "vkCmdSetScissor");
        VESPERA_LOAD_DEVICE(cmd_bind_vertex_buffers, PFN_vkCmdBindVertexBuffers, "vkCmdBindVertexBuffers");
        VESPERA_LOAD_DEVICE(cmd_bind_index_buffer, PFN_vkCmdBindIndexBuffer, "vkCmdBindIndexBuffer");
        VESPERA_LOAD_DEVICE(cmd_bind_descriptor_sets, PFN_vkCmdBindDescriptorSets, "vkCmdBindDescriptorSets");
        VESPERA_LOAD_DEVICE(cmd_pipeline_barrier, PFN_vkCmdPipelineBarrier, "vkCmdPipelineBarrier");
        VESPERA_LOAD_DEVICE(cmd_copy_buffer_to_image, PFN_vkCmdCopyBufferToImage, "vkCmdCopyBufferToImage");
        VESPERA_LOAD_DEVICE(cmd_draw_indexed, PFN_vkCmdDrawIndexed, "vkCmdDrawIndexed");
        VESPERA_LOAD_DEVICE(cmd_draw, PFN_vkCmdDraw, "vkCmdDraw");
        VESPERA_LOAD_DEVICE(create_semaphore, PFN_vkCreateSemaphore, "vkCreateSemaphore");
        VESPERA_LOAD_DEVICE(destroy_semaphore, PFN_vkDestroySemaphore, "vkDestroySemaphore");
        VESPERA_LOAD_DEVICE(create_fence, PFN_vkCreateFence, "vkCreateFence");
        VESPERA_LOAD_DEVICE(destroy_fence, PFN_vkDestroyFence, "vkDestroyFence");
        VESPERA_LOAD_DEVICE(wait_for_fences, PFN_vkWaitForFences, "vkWaitForFences");
        VESPERA_LOAD_DEVICE(reset_fences, PFN_vkResetFences, "vkResetFences");
        VESPERA_LOAD_DEVICE(queue_submit, PFN_vkQueueSubmit, "vkQueueSubmit");
#undef VESPERA_LOAD_DEVICE
    }

    QueueFamilies find_queue_families(VkPhysicalDevice device) const {
        QueueFamilies families;
        std::uint32_t count = 0;
        functions_.get_physical_device_queue_family_properties(device, &count, nullptr);
        std::vector<VkQueueFamilyProperties> properties(count);
        functions_.get_physical_device_queue_family_properties(device, &count, properties.data());

        for (std::uint32_t index = 0; index < count; ++index) {
            if (properties[index].queueCount > 0 && (properties[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
                if (!families.graphics) families.graphics = index;
            }
            if (properties[index].queueCount > 0 && SDL_Vulkan_GetPresentationSupport(instance_, device, index)) {
                if (!families.present) families.present = index;
            }
            if (families.complete() && families.graphics == families.present) break;
        }
        return families;
    }

    bool device_supports_swapchain(VkPhysicalDevice device) const {
        std::uint32_t count = 0;
        const VkResult count_result = functions_.enumerate_device_extension_properties(device, nullptr, &count, nullptr);
        if (count_result != VK_SUCCESS || count == 0) return false;
        std::vector<VkExtensionProperties> extensions(count);
        const VkResult list_result = functions_.enumerate_device_extension_properties(device, nullptr, &count, extensions.data());
        if (list_result != VK_SUCCESS) return false;
        return std::any_of(extensions.begin(), extensions.end(), [](const VkExtensionProperties& extension) {
            return std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
        });
    }

    SwapchainSupport query_swapchain_support(VkPhysicalDevice device) const {
        SwapchainSupport support;
        check_vk(functions_.get_physical_device_surface_capabilities(device, surface_, &support.capabilities),
            "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

        std::uint32_t format_count = 0;
        check_vk(functions_.get_physical_device_surface_formats(device, surface_, &format_count, nullptr),
            "vkGetPhysicalDeviceSurfaceFormatsKHR(count)");
        support.formats.resize(format_count);
        if (format_count > 0) {
            check_vk(functions_.get_physical_device_surface_formats(device, surface_, &format_count, support.formats.data()),
                "vkGetPhysicalDeviceSurfaceFormatsKHR(list)");
            support.formats.resize(format_count);
        }

        std::uint32_t mode_count = 0;
        check_vk(functions_.get_physical_device_surface_present_modes(device, surface_, &mode_count, nullptr),
            "vkGetPhysicalDeviceSurfacePresentModesKHR(count)");
        support.present_modes.resize(mode_count);
        if (mode_count > 0) {
            check_vk(functions_.get_physical_device_surface_present_modes(device, surface_, &mode_count, support.present_modes.data()),
                "vkGetPhysicalDeviceSurfacePresentModesKHR(list)");
            support.present_modes.resize(mode_count);
        }
        return support;
    }

    void choose_physical_device() {
        std::uint32_t count = 0;
        check_vk(functions_.enumerate_physical_devices(instance_, &count, nullptr), "vkEnumeratePhysicalDevices(count)");
        if (count == 0) throw std::runtime_error("No Vulkan physical devices were found.");

        std::vector<VkPhysicalDevice> devices(count);
        check_vk(functions_.enumerate_physical_devices(instance_, &count, devices.data()), "vkEnumeratePhysicalDevices(list)");

        int best_score = -1;
        VkPhysicalDevice best = VK_NULL_HANDLE;
        QueueFamilies best_families;
        for (VkPhysicalDevice device : devices) {
            const QueueFamilies families = find_queue_families(device);
            if (!families.complete() || !device_supports_swapchain(device)) continue;

            const SwapchainSupport swapchain = query_swapchain_support(device);
            if (swapchain.formats.empty() || swapchain.present_modes.empty()) continue;

            VkPhysicalDeviceProperties properties{};
            functions_.get_physical_device_properties(device, &properties);
            int score = static_cast<int>(properties.limits.maxImageDimension2D / 1024u);
            if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score += 10000;
            else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score += 5000;
            else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU) score += 1000;

            if (score > best_score) {
                best_score = score;
                best = device;
                best_families = families;
            }
        }

        if (!best) {
            throw std::runtime_error("No Vulkan device supports graphics, presentation, and VK_KHR_swapchain.");
        }
        physical_device_ = best;
        queue_families_ = best_families;
    }

    void create_device() {
        const float queue_priority = 1.0f;
        std::array<std::uint32_t, 2> unique_families{
            queue_families_.graphics.value(),
            queue_families_.present.value()
        };
        std::size_t unique_count = 1;
        if (unique_families[1] != unique_families[0]) unique_count = 2;

        std::array<VkDeviceQueueCreateInfo, 2> queue_infos{};
        for (std::size_t i = 0; i < unique_count; ++i) {
            queue_infos[i].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue_infos[i].queueFamilyIndex = unique_families[i];
            queue_infos[i].queueCount = 1;
            queue_infos[i].pQueuePriorities = &queue_priority;
        }

        constexpr std::array<const char*, 1> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkPhysicalDeviceFeatures features{};
        VkDeviceCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.queueCreateInfoCount = static_cast<std::uint32_t>(unique_count);
        create_info.pQueueCreateInfos = queue_infos.data();
        create_info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
        create_info.ppEnabledExtensionNames = extensions.data();
        create_info.pEnabledFeatures = &features;

        check_vk(functions_.create_device(physical_device_, &create_info, nullptr, &device_), "vkCreateDevice");
    }

    void create_command_resources() {
        functions_.get_device_queue(device_, queue_families_.graphics.value(), 0, &graphics_queue_);
        functions_.get_device_queue(device_, queue_families_.present.value(), 0, &present_queue_);
        if (!graphics_queue_ || !present_queue_) throw std::runtime_error("Vulkan device queues were not created.");

        VkCommandPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex = queue_families_.graphics.value();
        check_vk(functions_.create_command_pool(device_, &pool_info, nullptr, &command_pool_), "vkCreateCommandPool");

        VkCommandPoolCreateInfo upload_pool_info{};
        upload_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        upload_pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        upload_pool_info.queueFamilyIndex = queue_families_.graphics.value();
        check_vk(functions_.create_command_pool(
            device_, &upload_pool_info, nullptr, &upload_command_pool_), "vkCreateCommandPool(upload)");

        VkCommandBufferAllocateInfo allocate_info{};
        allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate_info.commandPool = command_pool_;
        allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate_info.commandBufferCount = static_cast<std::uint32_t>(command_buffers_.size());
        check_vk(functions_.allocate_command_buffers(device_, &allocate_info, command_buffers_.data()),
            "vkAllocateCommandBuffers");
    }

    std::uint32_t find_memory_type(std::uint32_t type_bits, VkMemoryPropertyFlags required) const {
        VkPhysicalDeviceMemoryProperties properties{};
        functions_.get_physical_device_memory_properties(physical_device_, &properties);
        for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
            if ((type_bits & (1u << index)) == 0) continue;
            if ((properties.memoryTypes[index].propertyFlags & required) == required) return index;
        }
        throw std::runtime_error(std::format(
            "No Vulkan memory type satisfies required property flags 0x{:x}.",
            static_cast<unsigned int>(required)));
    }

    void create_buffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkBuffer& buffer,
        VkDeviceMemory& memory,
        void*& mapped) {
        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = size;
        buffer_info.usage = usage;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check_vk(functions_.create_buffer(device_, &buffer_info, nullptr, &buffer), "vkCreateBuffer");

        VkMemoryRequirements requirements{};
        functions_.get_buffer_memory_requirements(device_, buffer, &requirements);

        VkMemoryAllocateInfo allocate_info{};
        allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate_info.allocationSize = requirements.size;
        allocate_info.memoryTypeIndex = find_memory_type(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        check_vk(functions_.allocate_memory(device_, &allocate_info, nullptr, &memory), "vkAllocateMemory(buffer)");
        check_vk(functions_.bind_buffer_memory(device_, buffer, memory, 0), "vkBindBufferMemory");
        check_vk(functions_.map_memory(device_, memory, 0, requirements.size, 0, &mapped), "vkMapMemory(buffer)");
    }

    void destroy_frame_geometry(FrameGeometry& geometry) {
        if (!device_) return;
        if (geometry.vertex_mapped && geometry.vertex_memory && functions_.unmap_memory) {
            functions_.unmap_memory(device_, geometry.vertex_memory);
        }
        geometry.vertex_mapped = nullptr;
        if (geometry.index_mapped && geometry.index_memory && functions_.unmap_memory) {
            functions_.unmap_memory(device_, geometry.index_memory);
        }
        geometry.index_mapped = nullptr;
        if (geometry.vertex_buffer && functions_.destroy_buffer) {
            functions_.destroy_buffer(device_, geometry.vertex_buffer, nullptr);
        }
        if (geometry.index_buffer && functions_.destroy_buffer) {
            functions_.destroy_buffer(device_, geometry.index_buffer, nullptr);
        }
        if (geometry.vertex_memory && functions_.free_memory) {
            functions_.free_memory(device_, geometry.vertex_memory, nullptr);
        }
        if (geometry.index_memory && functions_.free_memory) {
            functions_.free_memory(device_, geometry.index_memory, nullptr);
        }
        geometry = {};
    }

    void create_geometry_buffers() {
        for (FrameGeometry& geometry : frame_geometry_) {
            try {
                create_buffer(
                    kInitialVertexBufferBytes,
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    geometry.vertex_buffer,
                    geometry.vertex_memory,
                    geometry.vertex_mapped);
                geometry.vertex_capacity = kInitialVertexBufferBytes;
                create_buffer(
                    kInitialIndexBufferBytes,
                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                    geometry.index_buffer,
                    geometry.index_memory,
                    geometry.index_mapped);
                geometry.index_capacity = kInitialIndexBufferBytes;
            } catch (...) {
                destroy_frame_geometry(geometry);
                throw;
            }
        }
    }

    void destroy_geometry_buffers() {
        for (FrameGeometry& geometry : frame_geometry_) destroy_frame_geometry(geometry);
    }

    void ensure_world_mesh(const SectorWorld& world) {
        if (cached_world_ == &world && cached_world_revision_ == world.revision()) return;
        if (device_ && functions_.device_wait_idle) {
            check_vk(functions_.device_wait_idle(device_), "vkDeviceWaitIdle(world rebuild)");
        }
        create_world_texture_array(world);
        world_mesh_ = build_sector_mesh(world);
        cached_world_ = &world;
        cached_world_revision_ = world.revision();
        log::info(std::format(
            "Vulkan world mesh rebuilt: {} sectors | {} vertices | {} triangles | {} texture layer(s)",
            world.sectors().size(),
            world_mesh_.vertices.size(),
            world_mesh_.indices.size() / 3u,
            world_texture_layers_));
    }

    void clear_render_viewport(int x, int y, int viewport_width, int viewport_height) {
        if (!frame_open_ || !functions_.cmd_clear_attachments || viewport_width <= 0 || viewport_height <= 0) return;

        std::array<VkClearAttachment, 2> attachments{};
        attachments[0].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        attachments[0].colorAttachment = 0;
        std::copy(current_clear_color_.begin(), current_clear_color_.end(), attachments[0].clearValue.color.float32);
        attachments[1].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        attachments[1].clearValue.depthStencil.depth = 1.0f;
        attachments[1].clearValue.depthStencil.stencil = 0;

        VkClearRect rect{};
        rect.rect.offset = {x, y};
        rect.rect.extent = {static_cast<std::uint32_t>(viewport_width), static_cast<std::uint32_t>(viewport_height)};
        rect.baseArrayLayer = 0;
        rect.layerCount = 1;
        functions_.cmd_clear_attachments(
            command_buffers_[current_frame_],
            static_cast<std::uint32_t>(attachments.size()), attachments.data(),
            1, &rect);
    }

    std::vector<ActivePointLight> collect_active_lights(
        const Scene& scene,
        const Camera& camera,
        const ViewFrustum& frustum
    ) {
        std::vector<ActivePointLight> lights;
        lights.reserve((std::min)(scene.entities.size(), std::size_t{128}));
        for (const Entity& entity : scene.entities) {
            if (!entity.enabled || !entity.point_light) continue;
            ++frame_stats_.point_lights_considered;
            const TransformComponent transform = entity_world_transform(scene, entity);
            const PointLightComponent& light = *entity.point_light;
            const float radius = light.radius * (std::max)({
                std::abs(transform.scale.x),
                std::abs(transform.scale.y),
                std::abs(transform.scale.z),
            });
            if (radius <= 0.0f || light.intensity <= 0.0f || !frustum.intersects_sphere(transform.position, radius)) {
                continue;
            }
            ++frame_stats_.point_lights_frustum_visible;
            const float dx = transform.position.x - camera.position.x;
            const float dy = transform.position.y - camera.position.y;
            const float dz = transform.position.z - camera.position.z;
            lights.push_back({
                transform.position,
                radius,
                {light.color[0], light.color[1], light.color[2]},
                light.intensity,
                dx * dx + dy * dy + dz * dz,
            });
        }
        const auto nearer = [](const ActivePointLight& a, const ActivePointLight& b) {
            return a.distance_squared < b.distance_squared;
        };
        if (lights.size() > kMaxActivePointLights) {
            std::partial_sort(
                lights.begin(), lights.begin() + static_cast<std::ptrdiff_t>(kMaxActivePointLights),
                lights.end(), nearer);
            lights.resize(kMaxActivePointLights);
        } else {
            std::sort(lights.begin(), lights.end(), nearer);
        }
        frame_stats_.point_lights_uploaded += lights.size();
        return lights;
    }

    VkDescriptorSet upload_lighting_constants(const std::vector<ActivePointLight>& lights) {
        if (!lighting_mapped_ || lighting_stride_ == 0 || lighting_view_cursor_ >= kMaxLightingViewsPerFrame) {
            if (lighting_view_cursor_ >= kMaxLightingViewsPerFrame) {
                log::warn("Vulkan frame exceeded the supported lighting-view count; extra scene pass skipped.");
            }
            return VK_NULL_HANDLE;
        }
        const std::size_t descriptor_index = current_frame_ * kMaxLightingViewsPerFrame + lighting_view_cursor_++;
        VulkanLightingConstants constants{};
        const std::size_t count = std::min(lights.size(), kMaxActivePointLights);
        for (std::size_t index = 0; index < count; ++index) {
            const ActivePointLight& source = lights[index];
            VulkanGpuPointLight& destination = constants.lights[index];
            destination.position_radius[0] = source.position.x;
            destination.position_radius[1] = source.position.y;
            destination.position_radius[2] = source.position.z;
            destination.position_radius[3] = std::max(source.radius, 0.0001f);
            destination.color_intensity[0] = source.color[0];
            destination.color_intensity[1] = source.color[1];
            destination.color_intensity[2] = source.color[2];
            destination.color_intensity[3] = std::max(source.intensity, 0.0f);
        }
        std::memcpy(
            static_cast<std::byte*>(lighting_mapped_) + lighting_stride_ * descriptor_index,
            &constants,
            sizeof(constants));
        return world_descriptor_sets_[descriptor_index];
    }

    void append_indices(const std::vector<std::uint32_t>& source, std::uint32_t vertex_base) {
        if (scene_vertices_.size() > static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)())) {
            throw std::runtime_error("Vulkan scene vertex count exceeds 32-bit index range.");
        }
        scene_indices_.reserve(scene_indices_.size() + source.size());
        for (std::uint32_t index : source) scene_indices_.push_back(vertex_base + index);
    }

    void build_scene_geometry(
        const Scene& scene,
        double total_seconds,
        const Camera& camera,
        const CameraProjection& projection,
        const ViewFrustum& frustum
    ) {
        scene_vertices_.clear();
        scene_indices_.clear();
        scene_batches_.clear();

        scene_vertices_.reserve(world_mesh_.vertices.size() + scene.entities.size() * 16u);
        scene_indices_.reserve(world_mesh_.indices.size() + scene.entities.size() * 36u);

        if (!world_mesh_.vertices.empty() && !world_mesh_.indices.empty()) {
            const std::uint32_t first_index = static_cast<std::uint32_t>(scene_indices_.size());
            const std::uint32_t vertex_base = static_cast<std::uint32_t>(scene_vertices_.size());
            for (const SectorMeshVertex& source : world_mesh_.vertices) {
                const Vec3 world_position{source.position[0], source.position[1], source.position[2]};
                const std::array<float, 4> base_color{source.color[0], source.color[1], source.color[2], 1.0f};
                scene_vertices_.push_back(make_bootstrap_vertex(
                    world_position, base_color, source.uv[0], source.uv[1], source.texture_layer, camera, projection));
            }
            append_indices(world_mesh_.indices, vertex_base);
            scene_batches_.push_back({
                BatchKind::World,
                first_index,
                static_cast<std::uint32_t>(scene_indices_.size()) - first_index,
            });
        }

        std::array<std::vector<std::pair<const Entity*, TransformComponent>>, 4> visible_meshes;
        for (const Entity& entity : scene.entities) {
            if (!entity.enabled || !entity.mesh_renderer) continue;
            ++frame_stats_.mesh_entities_considered;
            const auto primitive_index = static_cast<std::size_t>(entity.mesh_renderer->primitive);
            if (primitive_index >= primitive_meshes_.size() || primitive_meshes_[primitive_index].indices.empty()) continue;
            const TransformComponent transform = entity_world_transform(scene, entity);
            const float bounding_radius = 0.5f * std::sqrt(
                transform.scale.x * transform.scale.x
                + transform.scale.y * transform.scale.y
                + transform.scale.z * transform.scale.z);
            if (!frustum.intersects_sphere(transform.position, bounding_radius)) {
                ++frame_stats_.mesh_instances_culled;
                continue;
            }
            ++frame_stats_.mesh_instances_visible;
            visible_meshes[primitive_index].push_back({&entity, transform});
        }

        for (std::size_t primitive_index = 0; primitive_index < visible_meshes.size(); ++primitive_index) {
            const auto& visible = visible_meshes[primitive_index];
            if (visible.empty()) continue;
            const SectorMesh& primitive = primitive_meshes_[primitive_index];
            const std::uint32_t first_index = static_cast<std::uint32_t>(scene_indices_.size());
            for (const auto& [entity, transform] : visible) {
                const MeshRendererComponent& mesh = *entity->mesh_renderer;
                const MaterialProperties& material = mesh.resolved_material;
                const bool use_material = mesh.material_resolved;
                const std::array<float, 4> tint{
                    mesh.color[0] * (use_material ? material.base_color[0] : 1.0f),
                    mesh.color[1] * (use_material ? material.base_color[1] : 1.0f),
                    mesh.color[2] * (use_material ? material.base_color[2] : 1.0f),
                    mesh.color[3] * (use_material ? material.base_color[3] : 1.0f),
                };
                const TextureId texture = use_material && mesh.resolved_material_texture != kInvalidTexture
                    ? mesh.resolved_material_texture : mesh.texture;
                const float texture_layer = texture != kInvalidTexture && texture < scene.world.textures().size()
                    ? static_cast<float>(texture + 1u) : 0.0f;
                const bool unlit = use_material && material.shader == BuiltinMaterialShader::Unlit;
                const std::array<float, 3> emission = use_material ? material.emission_color : std::array<float, 3>{1.0f, 1.0f, 1.0f};
                const float emission_strength = use_material ? material.emission_strength : 0.0f;

                const std::uint32_t vertex_base = static_cast<std::uint32_t>(scene_vertices_.size());
                for (const SectorMeshVertex& source : primitive.vertices) {
                    const Vec3 world_position = transform_point(
                        {source.position[0], source.position[1], source.position[2]}, transform);
                    const std::array<float, 4> base_color{
                        source.color[0] * tint[0], source.color[1] * tint[1], source.color[2] * tint[2], tint[3]};
                    const std::array<float, 4> emission_params{
                        emission[0], emission[1], emission[2], emission_strength};
                    const std::array<float, 4> material_params{
                        unlit ? 1.0f : 0.0f, 0.0f, use_material ? material.alpha_cutoff : 0.5f, 0.0f};
                    scene_vertices_.push_back(make_bootstrap_vertex(
                        world_position, base_color, source.uv[0], source.uv[1], texture_layer,
                        camera, projection, emission_params, material_params));
                }
                append_indices(primitive.indices, vertex_base);
            }
            scene_batches_.push_back({
                BatchKind::Mesh,
                first_index,
                static_cast<std::uint32_t>(scene_indices_.size()) - first_index,
            });
        }

        struct SpriteCandidate {
            const Entity* entity = nullptr;
            TransformComponent transform{};
            ResolvedSpriteFrame frame{};
            float distance_squared = 0.0f;
        };
        std::vector<SpriteCandidate> sprites;
        sprites.reserve(scene.entities.size());
        for (const Entity& entity : scene.entities) {
            if (!entity.enabled || !entity.sprite_renderer) continue;
            ++frame_stats_.sprite_entities_considered;
            const SpriteRendererComponent& sprite = *entity.sprite_renderer;
            const TransformComponent transform = entity_world_transform(scene, entity);
            const float sprite_width = std::abs(sprite.size.x * transform.scale.x);
            const float sprite_height = std::abs(sprite.size.z * transform.scale.y);
            const float radius = 0.5f * std::sqrt(sprite_width * sprite_width + sprite_height * sprite_height);
            if (!frustum.intersects_sphere(transform.position, radius)) {
                ++frame_stats_.sprite_entities_culled;
                continue;
            }
            const ResolvedSpriteFrame resolved = resolve_sprite_frame(scene, entity, sprite, total_seconds);
            if (resolved.texture == kInvalidTexture || resolved.texture >= scene.world.textures().size()) continue;
            const float dx = transform.position.x - camera.position.x;
            const float dy = transform.position.y - camera.position.y;
            const float dz = transform.position.z - camera.position.z;
            sprites.push_back({&entity, transform, resolved, dx * dx + dy * dy + dz * dz});
            ++frame_stats_.sprite_entities_visible;
        }
        std::sort(sprites.begin(), sprites.end(), [](const SpriteCandidate& a, const SpriteCandidate& b) {
            return a.distance_squared > b.distance_squared;
        });

        if (!sprites.empty()) {
            const std::uint32_t first_index = static_cast<std::uint32_t>(scene_indices_.size());
            constexpr std::array<Vec3, 4> local_positions{{
                {-0.5f, 0.0f, 0.0f},
                { 0.5f, 0.0f, 0.0f},
                { 0.5f, 1.0f, 0.0f},
                {-0.5f, 1.0f, 0.0f},
            }};
            constexpr std::array<std::array<float, 2>, 4> uvs{{
                {0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f},
            }};
            constexpr std::array<std::uint32_t, 6> indices{{0, 1, 2, 0, 2, 3}};
            for (const SpriteCandidate& candidate : sprites) {
                const SpriteRendererComponent& sprite = *candidate.entity->sprite_renderer;
                const float width = sprite.size.x * candidate.transform.scale.x;
                const float height = sprite.size.z * candidate.transform.scale.y;
                const float texture_layer = static_cast<float>(candidate.frame.texture + 1u);
                const std::array<float, 4> base_color = sprite.color;
                const std::uint32_t vertex_base = static_cast<std::uint32_t>(scene_vertices_.size());
                for (std::size_t i = 0; i < local_positions.size(); ++i) {
                    Vec3 local{local_positions[i].x * width, local_positions[i].y * height, 0.0f};
                    local = rotate_y(local, camera.yaw);
                    const Vec3 world_position{
                        candidate.transform.position.x + local.x,
                        candidate.transform.position.y + local.y,
                        candidate.transform.position.z + local.z,
                    };
                    scene_vertices_.push_back(make_bootstrap_vertex(
                        world_position, base_color, uvs[i][0], uvs[i][1], texture_layer, camera, projection));
                }
                for (std::uint32_t index : indices) scene_indices_.push_back(vertex_base + index);
            }
            scene_batches_.push_back({
                BatchKind::Sprite,
                first_index,
                static_cast<std::uint32_t>(scene_indices_.size()) - first_index,
            });
        }
    }

    std::optional<GeometryUpload> upload_dynamic_geometry(
        const std::vector<BootstrapVertex>& vertices,
        const std::vector<std::uint32_t>& indices
    ) {
        FrameGeometry& geometry = frame_geometry_[current_frame_];
        if (!geometry.vertex_mapped || !geometry.index_mapped) return std::nullopt;
        const VkDeviceSize vertex_bytes = static_cast<VkDeviceSize>(vertices.size() * sizeof(BootstrapVertex));
        const VkDeviceSize index_bytes = static_cast<VkDeviceSize>(indices.size() * sizeof(std::uint32_t));
        if (geometry.vertex_cursor + vertex_bytes > geometry.vertex_capacity
            || geometry.index_cursor + index_bytes > geometry.index_capacity) {
            if (!geometry_capacity_warning_emitted_) {
                log::warn(std::format(
                    "Vulkan dynamic geometry capacity exceeded (vertex cursor {} + {} / {}, index cursor {} + {} / {} bytes); draw skipped.",
                    geometry.vertex_cursor, vertex_bytes, geometry.vertex_capacity,
                    geometry.index_cursor, index_bytes, geometry.index_capacity));
                geometry_capacity_warning_emitted_ = true;
            }
            return std::nullopt;
        }
        GeometryUpload upload{geometry.vertex_cursor, geometry.index_cursor};
        if (vertex_bytes > 0) {
            std::memcpy(static_cast<std::byte*>(geometry.vertex_mapped) + geometry.vertex_cursor,
                vertices.data(), static_cast<std::size_t>(vertex_bytes));
        }
        if (index_bytes > 0) {
            std::memcpy(static_cast<std::byte*>(geometry.index_mapped) + geometry.index_cursor,
                indices.data(), static_cast<std::size_t>(index_bytes));
        }
        geometry.vertex_cursor += vertex_bytes;
        geometry.index_cursor += index_bytes;
        return upload;
    }

    std::optional<VkDeviceSize> upload_dynamic_vertices(const std::vector<BootstrapVertex>& vertices) {
        FrameGeometry& geometry = frame_geometry_[current_frame_];
        if (!geometry.vertex_mapped) return std::nullopt;
        const VkDeviceSize vertex_bytes = static_cast<VkDeviceSize>(vertices.size() * sizeof(BootstrapVertex));
        if (geometry.vertex_cursor + vertex_bytes > geometry.vertex_capacity) {
            if (!geometry_capacity_warning_emitted_) {
                log::warn(std::format(
                    "Vulkan dynamic vertex capacity exceeded (cursor {} + {} / {} bytes); UI draw skipped.",
                    geometry.vertex_cursor, vertex_bytes, geometry.vertex_capacity));
                geometry_capacity_warning_emitted_ = true;
            }
            return std::nullopt;
        }
        const VkDeviceSize offset = geometry.vertex_cursor;
        if (vertex_bytes > 0) {
            std::memcpy(static_cast<std::byte*>(geometry.vertex_mapped) + geometry.vertex_cursor,
                vertices.data(), static_cast<std::size_t>(vertex_bytes));
        }
        geometry.vertex_cursor += vertex_bytes;
        return offset;
    }

    void create_world_descriptor_resources() {
        constexpr std::size_t kDescriptorCount = kFramesInFlight * kMaxLightingViewsPerFrame;
        const std::array<VkDescriptorSetLayoutBinding, 2> bindings{{
            VkDescriptorSetLayoutBinding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            VkDescriptorSetLayoutBinding{1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        }};

        VkDescriptorSetLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = static_cast<std::uint32_t>(bindings.size());
        layout_info.pBindings = bindings.data();
        check_vk(functions_.create_descriptor_set_layout(
            device_, &layout_info, nullptr, &world_descriptor_set_layout_), "vkCreateDescriptorSetLayout(world)");

        const std::array<VkDescriptorPoolSize, 2> pool_sizes{{
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<std::uint32_t>(kDescriptorCount)},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<std::uint32_t>(kDescriptorCount)},
        }};
        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.maxSets = static_cast<std::uint32_t>(kDescriptorCount);
        pool_info.poolSizeCount = static_cast<std::uint32_t>(pool_sizes.size());
        pool_info.pPoolSizes = pool_sizes.data();
        check_vk(functions_.create_descriptor_pool(
            device_, &pool_info, nullptr, &world_descriptor_pool_), "vkCreateDescriptorPool(world)");

        std::array<VkDescriptorSetLayout, kDescriptorCount> layouts{};
        layouts.fill(world_descriptor_set_layout_);
        VkDescriptorSetAllocateInfo allocate_info{};
        allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocate_info.descriptorPool = world_descriptor_pool_;
        allocate_info.descriptorSetCount = static_cast<std::uint32_t>(layouts.size());
        allocate_info.pSetLayouts = layouts.data();
        check_vk(functions_.allocate_descriptor_sets(
            device_, &allocate_info, world_descriptor_sets_.data()), "vkAllocateDescriptorSets(world)");

        VkPhysicalDeviceProperties properties{};
        functions_.get_physical_device_properties(physical_device_, &properties);
        const VkDeviceSize alignment = std::max<VkDeviceSize>(
            properties.limits.minUniformBufferOffsetAlignment, VkDeviceSize{1});
        lighting_stride_ = (sizeof(VulkanLightingConstants) + alignment - 1) / alignment * alignment;
        create_buffer(
            lighting_stride_ * static_cast<VkDeviceSize>(kDescriptorCount),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            lighting_buffer_, lighting_memory_, lighting_mapped_);

        for (std::size_t index = 0; index < kDescriptorCount; ++index) {
            VkDescriptorBufferInfo buffer_info{};
            buffer_info.buffer = lighting_buffer_;
            buffer_info.offset = lighting_stride_ * static_cast<VkDeviceSize>(index);
            buffer_info.range = sizeof(VulkanLightingConstants);
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = world_descriptor_sets_[index];
            write.dstBinding = 1;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.pBufferInfo = &buffer_info;
            functions_.update_descriptor_sets(device_, 1, &write, 0, nullptr);
        }

        VkSamplerCreateInfo sampler_info{};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter = VK_FILTER_NEAREST;
        sampler_info.minFilter = VK_FILTER_NEAREST;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.mipLodBias = 0.0f;
        sampler_info.anisotropyEnable = VK_FALSE;
        sampler_info.compareEnable = VK_FALSE;
        sampler_info.minLod = 0.0f;
        sampler_info.maxLod = 0.0f;
        sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        sampler_info.unnormalizedCoordinates = VK_FALSE;
        check_vk(functions_.create_sampler(device_, &sampler_info, nullptr, &world_texture_sampler_), "vkCreateSampler(world)");
    }

    void destroy_world_descriptor_resources() {
        world_descriptor_sets_.fill(VK_NULL_HANDLE);
        if (lighting_mapped_ && lighting_memory_ && functions_.unmap_memory) {
            functions_.unmap_memory(device_, lighting_memory_);
        }
        lighting_mapped_ = nullptr;
        if (device_ && lighting_buffer_ && functions_.destroy_buffer) {
            functions_.destroy_buffer(device_, lighting_buffer_, nullptr);
        }
        lighting_buffer_ = VK_NULL_HANDLE;
        if (device_ && lighting_memory_ && functions_.free_memory) {
            functions_.free_memory(device_, lighting_memory_, nullptr);
        }
        lighting_memory_ = VK_NULL_HANDLE;
        lighting_stride_ = 0;
        lighting_view_cursor_ = 0;

        if (device_ && world_descriptor_pool_ && functions_.destroy_descriptor_pool) {
            functions_.destroy_descriptor_pool(device_, world_descriptor_pool_, nullptr);
        }
        world_descriptor_pool_ = VK_NULL_HANDLE;
        if (device_ && world_texture_sampler_ && functions_.destroy_sampler) {
            functions_.destroy_sampler(device_, world_texture_sampler_, nullptr);
        }
        world_texture_sampler_ = VK_NULL_HANDLE;
        if (device_ && world_descriptor_set_layout_ && functions_.destroy_descriptor_set_layout) {
            functions_.destroy_descriptor_set_layout(device_, world_descriptor_set_layout_, nullptr);
        }
        world_descriptor_set_layout_ = VK_NULL_HANDLE;
    }

    void create_ui_descriptor_resources() {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = 1;
        layout_info.pBindings = &binding;
        check_vk(functions_.create_descriptor_set_layout(
            device_, &layout_info, nullptr, &ui_descriptor_set_layout_), "vkCreateDescriptorSetLayout(ui)");

        VkDescriptorPoolSize pool_size{};
        pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_size.descriptorCount = 1;
        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.maxSets = 1;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &pool_size;
        check_vk(functions_.create_descriptor_pool(
            device_, &pool_info, nullptr, &ui_descriptor_pool_), "vkCreateDescriptorPool(ui)");

        VkDescriptorSetAllocateInfo allocate_info{};
        allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocate_info.descriptorPool = ui_descriptor_pool_;
        allocate_info.descriptorSetCount = 1;
        allocate_info.pSetLayouts = &ui_descriptor_set_layout_;
        check_vk(functions_.allocate_descriptor_sets(
            device_, &allocate_info, &ui_descriptor_set_), "vkAllocateDescriptorSets(ui)");

        VkSamplerCreateInfo sampler_info{};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.anisotropyEnable = VK_FALSE;
        sampler_info.compareEnable = VK_FALSE;
        sampler_info.minLod = 0.0f;
        sampler_info.maxLod = 0.0f;
        sampler_info.borderColor = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
        sampler_info.unnormalizedCoordinates = VK_FALSE;
        check_vk(functions_.create_sampler(device_, &sampler_info, nullptr, &ui_sampler_), "vkCreateSampler(ui)");
    }

    void destroy_ui_descriptor_resources() {
        ui_descriptor_set_ = VK_NULL_HANDLE;
        if (device_ && ui_descriptor_pool_ && functions_.destroy_descriptor_pool) {
            functions_.destroy_descriptor_pool(device_, ui_descriptor_pool_, nullptr);
        }
        ui_descriptor_pool_ = VK_NULL_HANDLE;
        if (device_ && ui_sampler_ && functions_.destroy_sampler) {
            functions_.destroy_sampler(device_, ui_sampler_, nullptr);
        }
        ui_sampler_ = VK_NULL_HANDLE;
        if (device_ && ui_descriptor_set_layout_ && functions_.destroy_descriptor_set_layout) {
            functions_.destroy_descriptor_set_layout(device_, ui_descriptor_set_layout_, nullptr);
        }
        ui_descriptor_set_layout_ = VK_NULL_HANDLE;
    }

    void destroy_world_texture_array() {
        if (!device_) return;
        if (world_texture_view_ && functions_.destroy_image_view) {
            functions_.destroy_image_view(device_, world_texture_view_, nullptr);
        }
        world_texture_view_ = VK_NULL_HANDLE;
        if (world_texture_image_ && functions_.destroy_image) {
            functions_.destroy_image(device_, world_texture_image_, nullptr);
        }
        world_texture_image_ = VK_NULL_HANDLE;
        if (world_texture_memory_ && functions_.free_memory) {
            functions_.free_memory(device_, world_texture_memory_, nullptr);
        }
        world_texture_memory_ = VK_NULL_HANDLE;
        world_texture_width_ = 0;
        world_texture_height_ = 0;
        world_texture_layers_ = 0;
    }

    void destroy_ui_atlas_texture() {
        if (!device_) return;
        if (ui_atlas_view_ && functions_.destroy_image_view) functions_.destroy_image_view(device_, ui_atlas_view_, nullptr);
        ui_atlas_view_ = VK_NULL_HANDLE;
        if (ui_atlas_image_ && functions_.destroy_image) functions_.destroy_image(device_, ui_atlas_image_, nullptr);
        ui_atlas_image_ = VK_NULL_HANDLE;
        if (ui_atlas_memory_ && functions_.free_memory) functions_.free_memory(device_, ui_atlas_memory_, nullptr);
        ui_atlas_memory_ = VK_NULL_HANDLE;
        ui_atlas_width_ = 0;
        ui_atlas_height_ = 0;
        ui_atlas_revision_ = 0;
    }

    void upload_rgba8_array(
        std::uint32_t width,
        std::uint32_t height,
        std::uint32_t layers,
        const std::uint8_t* pixels,
        std::size_t pixel_bytes,
        VkImage& image,
        VkDeviceMemory& memory,
        VkImageView& view,
        std::string_view label
    ) {
        if (!pixels || width == 0 || height == 0 || layers == 0 || pixel_bytes == 0) {
            throw std::runtime_error(std::format("{} upload received invalid image data.", label));
        }
        const VkDeviceSize upload_bytes = static_cast<VkDeviceSize>(pixel_bytes);
        VkBuffer staging_buffer = VK_NULL_HANDLE;
        VkDeviceMemory staging_memory = VK_NULL_HANDLE;
        void* staging_mapped = nullptr;
        VkCommandBuffer upload_command = VK_NULL_HANDLE;
        try {
            create_buffer(upload_bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging_buffer, staging_memory, staging_mapped);
            std::memcpy(staging_mapped, pixels, pixel_bytes);

            VkImageCreateInfo image_info{};
            image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            image_info.imageType = VK_IMAGE_TYPE_2D;
            image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
            image_info.extent = {width, height, 1};
            image_info.mipLevels = 1;
            image_info.arrayLayers = layers;
            image_info.samples = VK_SAMPLE_COUNT_1_BIT;
            image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
            image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            check_vk(functions_.create_image(device_, &image_info, nullptr, &image), std::format("vkCreateImage({})", label));

            VkMemoryRequirements requirements{};
            functions_.get_image_memory_requirements(device_, image, &requirements);
            VkMemoryAllocateInfo allocate_info{};
            allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocate_info.allocationSize = requirements.size;
            allocate_info.memoryTypeIndex = find_memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            check_vk(functions_.allocate_memory(device_, &allocate_info, nullptr, &memory), std::format("vkAllocateMemory({})", label));
            check_vk(functions_.bind_image_memory(device_, image, memory, 0), std::format("vkBindImageMemory({})", label));

            VkCommandBufferAllocateInfo command_allocate{};
            command_allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            command_allocate.commandPool = upload_command_pool_;
            command_allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            command_allocate.commandBufferCount = 1;
            check_vk(functions_.allocate_command_buffers(device_, &command_allocate, &upload_command),
                std::format("vkAllocateCommandBuffers({})", label));

            VkCommandBufferBeginInfo begin_info{};
            begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            check_vk(functions_.begin_command_buffer(upload_command, &begin_info), std::format("vkBeginCommandBuffer({})", label));

            VkImageMemoryBarrier to_transfer{};
            to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            to_transfer.srcAccessMask = 0;
            to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_transfer.image = image;
            to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            to_transfer.subresourceRange.baseMipLevel = 0;
            to_transfer.subresourceRange.levelCount = 1;
            to_transfer.subresourceRange.baseArrayLayer = 0;
            to_transfer.subresourceRange.layerCount = layers;
            functions_.cmd_pipeline_barrier(upload_command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &to_transfer);

            VkBufferImageCopy copy{};
            copy.bufferOffset = 0;
            copy.bufferRowLength = 0;
            copy.bufferImageHeight = 0;
            copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.mipLevel = 0;
            copy.imageSubresource.baseArrayLayer = 0;
            copy.imageSubresource.layerCount = layers;
            copy.imageExtent = {width, height, 1};
            functions_.cmd_copy_buffer_to_image(
                upload_command, staging_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

            VkImageMemoryBarrier to_shader{};
            to_shader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            to_shader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            to_shader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            to_shader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            to_shader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            to_shader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_shader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_shader.image = image;
            to_shader.subresourceRange = to_transfer.subresourceRange;
            functions_.cmd_pipeline_barrier(upload_command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &to_shader);
            check_vk(functions_.end_command_buffer(upload_command), std::format("vkEndCommandBuffer({})", label));

            VkSubmitInfo submit{};
            submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &upload_command;
            check_vk(functions_.queue_submit(graphics_queue_, 1, &submit, VK_NULL_HANDLE), std::format("vkQueueSubmit({})", label));
            check_vk(functions_.device_wait_idle(device_), std::format("vkDeviceWaitIdle({})", label));

            VkImageViewCreateInfo view_info{};
            view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_info.image = image;
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
            view_info.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
            view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view_info.subresourceRange.baseMipLevel = 0;
            view_info.subresourceRange.levelCount = 1;
            view_info.subresourceRange.baseArrayLayer = 0;
            view_info.subresourceRange.layerCount = layers;
            check_vk(functions_.create_image_view(device_, &view_info, nullptr, &view), std::format("vkCreateImageView({})", label));
        } catch (...) {
            if (upload_command && functions_.free_command_buffers) {
                functions_.free_command_buffers(device_, upload_command_pool_, 1, &upload_command);
            }
            if (staging_mapped && staging_memory && functions_.unmap_memory) functions_.unmap_memory(device_, staging_memory);
            if (staging_buffer && functions_.destroy_buffer) functions_.destroy_buffer(device_, staging_buffer, nullptr);
            if (staging_memory && functions_.free_memory) functions_.free_memory(device_, staging_memory, nullptr);
            if (view && functions_.destroy_image_view) functions_.destroy_image_view(device_, view, nullptr);
            if (image && functions_.destroy_image) functions_.destroy_image(device_, image, nullptr);
            if (memory && functions_.free_memory) functions_.free_memory(device_, memory, nullptr);
            image = VK_NULL_HANDLE;
            memory = VK_NULL_HANDLE;
            view = VK_NULL_HANDLE;
            throw;
        }
        if (upload_command && functions_.free_command_buffers) {
            functions_.free_command_buffers(device_, upload_command_pool_, 1, &upload_command);
        }
        if (staging_mapped && staging_memory && functions_.unmap_memory) functions_.unmap_memory(device_, staging_memory);
        if (staging_buffer && functions_.destroy_buffer) functions_.destroy_buffer(device_, staging_buffer, nullptr);
        if (staging_memory && functions_.free_memory) functions_.free_memory(device_, staging_memory, nullptr);
    }

    void ensure_ui_atlas(const UiRenderPacket& packet) {
        if (!packet.atlas || !packet.atlas->valid()) return;
        if (ui_atlas_image_ && ui_atlas_revision_ == packet.atlas_revision
            && ui_atlas_width_ == packet.atlas->width && ui_atlas_height_ == packet.atlas->height) {
            return;
        }
        if (device_ && functions_.device_wait_idle) {
            check_vk(functions_.device_wait_idle(device_), "vkDeviceWaitIdle(ui atlas rebuild)");
        }
        destroy_ui_atlas_texture();
        upload_rgba8_array(
            packet.atlas->width,
            packet.atlas->height,
            1,
            packet.atlas->rgba8.data(),
            packet.atlas->rgba8.size(),
            ui_atlas_image_, ui_atlas_memory_, ui_atlas_view_, "ui atlas");

        VkDescriptorImageInfo descriptor_image{};
        descriptor_image.sampler = ui_sampler_;
        descriptor_image.imageView = ui_atlas_view_;
        descriptor_image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = ui_descriptor_set_;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &descriptor_image;
        functions_.update_descriptor_sets(device_, 1, &write, 0, nullptr);
        ui_atlas_width_ = packet.atlas->width;
        ui_atlas_height_ = packet.atlas->height;
        ui_atlas_revision_ = packet.atlas_revision;
        log::info(std::format("Vulkan runtime UI atlas uploaded: {}x{} RGBA8 | revision {}",
            ui_atlas_width_, ui_atlas_height_, ui_atlas_revision_));
    }

    void create_world_texture_array(const SectorWorld& world) {
        destroy_world_texture_array();

        std::uint32_t texture_width = 1;
        std::uint32_t texture_height = 1;
        for (const TextureData& texture : world.textures()) {
            if (texture.valid()) {
                texture_width = texture.width;
                texture_height = texture.height;
                break;
            }
        }

        VkPhysicalDeviceProperties properties{};
        functions_.get_physical_device_properties(physical_device_, &properties);
        if (texture_width > properties.limits.maxImageDimension2D || texture_height > properties.limits.maxImageDimension2D) {
            throw std::runtime_error(std::format(
                "World texture dimensions {}x{} exceed Vulkan maxImageDimension2D {}.",
                texture_width, texture_height, properties.limits.maxImageDimension2D));
        }

        const std::size_t layer_count_size = world.textures().size() + 1u;
        if (layer_count_size > properties.limits.maxImageArrayLayers
            || layer_count_size > static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)())) {
            throw std::runtime_error(std::format(
                "World requires {} Vulkan texture-array layers but the device supports {}.",
                layer_count_size, properties.limits.maxImageArrayLayers));
        }
        const std::uint32_t layer_count = static_cast<std::uint32_t>(layer_count_size);
        const std::size_t pixels_per_layer = static_cast<std::size_t>(texture_width)
            * static_cast<std::size_t>(texture_height) * 4u;
        if (pixels_per_layer == 0 || layer_count_size > (std::numeric_limits<std::size_t>::max)() / pixels_per_layer) {
            throw std::runtime_error("World texture-array byte size overflow.");
        }
        std::vector<std::uint8_t> layer_pixels(pixels_per_layer * layer_count_size, 255u);
        std::size_t resampled_layers = 0;
        std::size_t fallback_layers = 0;

        for (std::size_t texture_index = 0; texture_index < world.textures().size(); ++texture_index) {
            const TextureData& texture = world.textures()[texture_index];
            auto* destination = layer_pixels.data() + (texture_index + 1u) * pixels_per_layer;
            if (texture.valid() && texture.width == texture_width && texture.height == texture_height) {
                std::memcpy(destination, texture.rgba8.data(), pixels_per_layer);
                continue;
            }

            if (texture.valid() && texture.width > 0 && texture.height > 0) {
                // Vulkan currently uses one fixed-size sampled image array for world
                // textures. Keep arbitrary imported texture sizes usable by packing
                // them with nearest-neighbour resampling instead of replacing them
                // with a loud checkerboard. This is especially important in the
                // editor where catalog thumbnails/sheets can coexist with 64x64
                // world textures even when they are not directly rendered.
                for (std::uint32_t y = 0; y < texture_height; ++y) {
                    const std::uint32_t source_y = (static_cast<std::uint64_t>(y) * texture.height) / texture_height;
                    for (std::uint32_t x = 0; x < texture_width; ++x) {
                        const std::uint32_t source_x = (static_cast<std::uint64_t>(x) * texture.width) / texture_width;
                        const std::size_t source_pixel =
                            (static_cast<std::size_t>(source_y) * texture.width + source_x) * 4u;
                        const std::size_t destination_pixel =
                            (static_cast<std::size_t>(y) * texture_width + x) * 4u;
                        std::memcpy(destination + destination_pixel, texture.rgba8.data() + source_pixel, 4u);
                    }
                }
                ++resampled_layers;
                continue;
            }

            for (std::uint32_t y = 0; y < texture_height; ++y) {
                for (std::uint32_t x = 0; x < texture_width; ++x) {
                    const bool bright = ((x / 4u) + (y / 4u)) % 2u == 0u;
                    const std::size_t pixel = (static_cast<std::size_t>(y) * texture_width + x) * 4u;
                    destination[pixel + 0] = bright ? 255u : 25u;
                    destination[pixel + 1] = bright ? 0u : 25u;
                    destination[pixel + 2] = bright ? 255u : 25u;
                    destination[pixel + 3] = 255u;
                }
            }
            ++fallback_layers;
            log::warn(std::format(
                "Vulkan world texture '{}' is invalid; using checkerboard fallback.", texture.name));
        }

        const VkDeviceSize upload_bytes = static_cast<VkDeviceSize>(layer_pixels.size());
        VkBuffer staging_buffer = VK_NULL_HANDLE;
        VkDeviceMemory staging_memory = VK_NULL_HANDLE;
        void* staging_mapped = nullptr;
        VkCommandBuffer upload_command = VK_NULL_HANDLE;
        try {
            create_buffer(
                upload_bytes,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                staging_buffer,
                staging_memory,
                staging_mapped);
            std::memcpy(staging_mapped, layer_pixels.data(), layer_pixels.size());

            VkImageCreateInfo image_info{};
            image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            image_info.imageType = VK_IMAGE_TYPE_2D;
            image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
            image_info.extent = {texture_width, texture_height, 1};
            image_info.mipLevels = 1;
            image_info.arrayLayers = layer_count;
            image_info.samples = VK_SAMPLE_COUNT_1_BIT;
            image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
            image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            check_vk(functions_.create_image(device_, &image_info, nullptr, &world_texture_image_), "vkCreateImage(world textures)");

            VkMemoryRequirements image_requirements{};
            functions_.get_image_memory_requirements(device_, world_texture_image_, &image_requirements);
            VkMemoryAllocateInfo image_allocate{};
            image_allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            image_allocate.allocationSize = image_requirements.size;
            image_allocate.memoryTypeIndex = find_memory_type(
                image_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            check_vk(functions_.allocate_memory(
                device_, &image_allocate, nullptr, &world_texture_memory_), "vkAllocateMemory(world textures)");
            check_vk(functions_.bind_image_memory(
                device_, world_texture_image_, world_texture_memory_, 0), "vkBindImageMemory(world textures)");

            VkCommandBufferAllocateInfo command_allocate{};
            command_allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            command_allocate.commandPool = upload_command_pool_;
            command_allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            command_allocate.commandBufferCount = 1;
            check_vk(functions_.allocate_command_buffers(
                device_, &command_allocate, &upload_command), "vkAllocateCommandBuffers(world textures)");

            VkCommandBufferBeginInfo begin_info{};
            begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            check_vk(functions_.begin_command_buffer(upload_command, &begin_info), "vkBeginCommandBuffer(world textures)");

            VkImageMemoryBarrier to_transfer{};
            to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            to_transfer.srcAccessMask = 0;
            to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_transfer.image = world_texture_image_;
            to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            to_transfer.subresourceRange.baseMipLevel = 0;
            to_transfer.subresourceRange.levelCount = 1;
            to_transfer.subresourceRange.baseArrayLayer = 0;
            to_transfer.subresourceRange.layerCount = layer_count;
            functions_.cmd_pipeline_barrier(
                upload_command,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &to_transfer);

            VkBufferImageCopy copy{};
            copy.bufferOffset = 0;
            copy.bufferRowLength = 0;
            copy.bufferImageHeight = 0;
            copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.mipLevel = 0;
            copy.imageSubresource.baseArrayLayer = 0;
            copy.imageSubresource.layerCount = layer_count;
            copy.imageOffset = {0, 0, 0};
            copy.imageExtent = {texture_width, texture_height, 1};
            functions_.cmd_copy_buffer_to_image(
                upload_command, staging_buffer, world_texture_image_,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

            VkImageMemoryBarrier to_shader{};
            to_shader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            to_shader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            to_shader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            to_shader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            to_shader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            to_shader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_shader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_shader.image = world_texture_image_;
            to_shader.subresourceRange = to_transfer.subresourceRange;
            functions_.cmd_pipeline_barrier(
                upload_command,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &to_shader);
            check_vk(functions_.end_command_buffer(upload_command), "vkEndCommandBuffer(world textures)");

            VkSubmitInfo submit{};
            submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &upload_command;
            check_vk(functions_.queue_submit(graphics_queue_, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit(world textures)");
            check_vk(functions_.device_wait_idle(device_), "vkDeviceWaitIdle(world textures)");

            VkImageViewCreateInfo view_info{};
            view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_info.image = world_texture_image_;
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
            view_info.components = {
                VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
            view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view_info.subresourceRange.baseMipLevel = 0;
            view_info.subresourceRange.levelCount = 1;
            view_info.subresourceRange.baseArrayLayer = 0;
            view_info.subresourceRange.layerCount = layer_count;
            check_vk(functions_.create_image_view(
                device_, &view_info, nullptr, &world_texture_view_), "vkCreateImageView(world textures)");

            VkDescriptorImageInfo descriptor_image{};
            descriptor_image.sampler = world_texture_sampler_;
            descriptor_image.imageView = world_texture_view_;
            descriptor_image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            std::array<VkWriteDescriptorSet, kFramesInFlight * kMaxLightingViewsPerFrame> writes{};
            for (std::size_t index = 0; index < writes.size(); ++index) {
                writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[index].dstSet = world_descriptor_sets_[index];
                writes[index].dstBinding = 0;
                writes[index].dstArrayElement = 0;
                writes[index].descriptorCount = 1;
                writes[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[index].pImageInfo = &descriptor_image;
            }
            functions_.update_descriptor_sets(
                device_, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);

            world_texture_width_ = texture_width;
            world_texture_height_ = texture_height;
            world_texture_layers_ = layer_count;
            log::info(std::format(
                "Vulkan world textures uploaded: {}x{} RGBA8 | {} layer(s) | {} resampled | {} fallback",
                texture_width, texture_height, layer_count, resampled_layers, fallback_layers));
        } catch (...) {
            if (upload_command && functions_.free_command_buffers) {
                functions_.free_command_buffers(device_, upload_command_pool_, 1, &upload_command);
                upload_command = VK_NULL_HANDLE;
            }
            if (staging_mapped && staging_memory && functions_.unmap_memory) {
                functions_.unmap_memory(device_, staging_memory);
                staging_mapped = nullptr;
            }
            if (staging_buffer && functions_.destroy_buffer) functions_.destroy_buffer(device_, staging_buffer, nullptr);
            if (staging_memory && functions_.free_memory) functions_.free_memory(device_, staging_memory, nullptr);
            destroy_world_texture_array();
            throw;
        }

        if (upload_command && functions_.free_command_buffers) {
            functions_.free_command_buffers(device_, upload_command_pool_, 1, &upload_command);
        }
        if (staging_mapped && staging_memory && functions_.unmap_memory) {
            functions_.unmap_memory(device_, staging_memory);
        }
        if (staging_buffer && functions_.destroy_buffer) functions_.destroy_buffer(device_, staging_buffer, nullptr);
        if (staging_memory && functions_.free_memory) functions_.free_memory(device_, staging_memory, nullptr);
    }

    VkFormat choose_depth_format() const {
        constexpr std::array<VkFormat, 3> candidates{
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_D24_UNORM_S8_UINT,
        };
        for (VkFormat format : candidates) {
            VkFormatProperties properties{};
            functions_.get_physical_device_format_properties(physical_device_, format, &properties);
            if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
                return format;
            }
        }
        throw std::runtime_error("Vulkan device exposes no supported depth attachment format.");
    }

    void create_depth_resources() {
        depth_format_ = choose_depth_format();
        depth_images_.resize(swapchain_images_.size(), VK_NULL_HANDLE);
        depth_memories_.resize(swapchain_images_.size(), VK_NULL_HANDLE);
        depth_image_views_.resize(swapchain_images_.size(), VK_NULL_HANDLE);

        for (std::size_t index = 0; index < swapchain_images_.size(); ++index) {
            VkImageCreateInfo image_info{};
            image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            image_info.imageType = VK_IMAGE_TYPE_2D;
            image_info.format = depth_format_;
            image_info.extent = {swapchain_extent_.width, swapchain_extent_.height, 1};
            image_info.mipLevels = 1;
            image_info.arrayLayers = 1;
            image_info.samples = VK_SAMPLE_COUNT_1_BIT;
            image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
            image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            check_vk(functions_.create_image(device_, &image_info, nullptr, &depth_images_[index]), "vkCreateImage(depth)");

            VkMemoryRequirements requirements{};
            functions_.get_image_memory_requirements(device_, depth_images_[index], &requirements);
            VkMemoryAllocateInfo allocate_info{};
            allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocate_info.allocationSize = requirements.size;
            allocate_info.memoryTypeIndex = find_memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            check_vk(functions_.allocate_memory(device_, &allocate_info, nullptr, &depth_memories_[index]), "vkAllocateMemory(depth)");
            check_vk(functions_.bind_image_memory(device_, depth_images_[index], depth_memories_[index], 0), "vkBindImageMemory(depth)");

            VkImageViewCreateInfo view_info{};
            view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_info.image = depth_images_[index];
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view_info.format = depth_format_;
            view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            view_info.subresourceRange.baseMipLevel = 0;
            view_info.subresourceRange.levelCount = 1;
            view_info.subresourceRange.baseArrayLayer = 0;
            view_info.subresourceRange.layerCount = 1;
            check_vk(functions_.create_image_view(device_, &view_info, nullptr, &depth_image_views_[index]), "vkCreateImageView(depth)");
        }
    }

    VkShaderModule create_shader_module(const std::uint32_t* words, std::size_t word_count) const {
        VkShaderModuleCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        create_info.codeSize = word_count * sizeof(std::uint32_t);
        create_info.pCode = words;
        VkShaderModule module = VK_NULL_HANDLE;
        check_vk(functions_.create_shader_module(device_, &create_info, nullptr, &module), "vkCreateShaderModule");
        return module;
    }

    void create_graphics_pipelines() {
        const VkShaderModule world_vertex_shader = create_shader_module(
            vulkan_lighting_shaders::kVertex.data(), vulkan_lighting_shaders::kVertex.size());
        VkShaderModule world_fragment_shader = VK_NULL_HANDLE;
        VkShaderModule ui_vertex_shader = VK_NULL_HANDLE;
        VkShaderModule ui_fragment_shader = VK_NULL_HANDLE;
        try {
            world_fragment_shader = create_shader_module(
                vulkan_lighting_shaders::kFragment.data(), vulkan_lighting_shaders::kFragment.size());
            ui_vertex_shader = create_shader_module(
                vulkan_bootstrap_shaders::kVertex.data(), vulkan_bootstrap_shaders::kVertex.size());
            ui_fragment_shader = create_shader_module(
                vulkan_bootstrap_shaders::kFragment.data(), vulkan_bootstrap_shaders::kFragment.size());

            VkPipelineLayoutCreateInfo world_layout_info{};
            world_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            world_layout_info.setLayoutCount = 1;
            world_layout_info.pSetLayouts = &world_descriptor_set_layout_;
            check_vk(functions_.create_pipeline_layout(
                device_, &world_layout_info, nullptr, &pipeline_layout_), "vkCreatePipelineLayout(world)");

            VkPipelineLayoutCreateInfo ui_layout_info{};
            ui_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            ui_layout_info.setLayoutCount = 1;
            ui_layout_info.pSetLayouts = &ui_descriptor_set_layout_;
            check_vk(functions_.create_pipeline_layout(
                device_, &ui_layout_info, nullptr, &ui_pipeline_layout_), "vkCreatePipelineLayout(ui)");

            VkVertexInputBindingDescription binding{};
            binding.binding = 0;
            binding.stride = sizeof(BootstrapVertex);
            binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            const std::array<VkVertexInputAttributeDescription, 7> world_attributes{{
                {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<std::uint32_t>(offsetof(BootstrapVertex, clip_position))},
                {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<std::uint32_t>(offsetof(BootstrapVertex, color))},
                {2, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<std::uint32_t>(offsetof(BootstrapVertex, uv))},
                {3, 0, VK_FORMAT_R32_SFLOAT, static_cast<std::uint32_t>(offsetof(BootstrapVertex, texture_layer))},
                {4, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<std::uint32_t>(offsetof(BootstrapVertex, world_position))},
                {5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<std::uint32_t>(offsetof(BootstrapVertex, emission))},
                {6, 0, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<std::uint32_t>(offsetof(BootstrapVertex, material_params))},
            }};
            const std::array<VkVertexInputAttributeDescription, 4> ui_attributes{{
                world_attributes[0], world_attributes[1], world_attributes[2], world_attributes[3],
            }};

            VkPipelineInputAssemblyStateCreateInfo input_assembly{};
            input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            input_assembly.primitiveRestartEnable = VK_FALSE;

            VkPipelineViewportStateCreateInfo viewport_state{};
            viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewport_state.viewportCount = 1;
            viewport_state.scissorCount = 1;

            VkPipelineRasterizationStateCreateInfo rasterizer{};
            rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizer.depthClampEnable = VK_FALSE;
            rasterizer.rasterizerDiscardEnable = VK_FALSE;
            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizer.cullMode = VK_CULL_MODE_NONE;
            rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rasterizer.depthBiasEnable = VK_FALSE;
            rasterizer.lineWidth = 1.0f;

            VkPipelineMultisampleStateCreateInfo multisample{};
            multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            multisample.sampleShadingEnable = VK_FALSE;

            constexpr std::array<VkDynamicState, 2> dynamic_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic_state{};
            dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamic_state.dynamicStateCount = static_cast<std::uint32_t>(dynamic_states.size());
            dynamic_state.pDynamicStates = dynamic_states.data();

            auto create_pipeline = [&](const std::array<VkPipelineShaderStageCreateInfo, 2>& shader_stages,
                                       const VkVertexInputAttributeDescription* attributes,
                                       std::uint32_t attribute_count,
                                       VkPipelineLayout layout,
                                       bool blend_enabled,
                                       bool depth_test,
                                       bool depth_write,
                                       VkPipeline& destination,
                                       std::string_view label) {
                VkPipelineVertexInputStateCreateInfo vertex_input{};
                vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
                vertex_input.vertexBindingDescriptionCount = 1;
                vertex_input.pVertexBindingDescriptions = &binding;
                vertex_input.vertexAttributeDescriptionCount = attribute_count;
                vertex_input.pVertexAttributeDescriptions = attributes;

                VkPipelineDepthStencilStateCreateInfo depth_stencil{};
                depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
                depth_stencil.depthTestEnable = depth_test ? VK_TRUE : VK_FALSE;
                depth_stencil.depthWriteEnable = depth_write ? VK_TRUE : VK_FALSE;
                depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;
                depth_stencil.depthBoundsTestEnable = VK_FALSE;
                depth_stencil.stencilTestEnable = VK_FALSE;

                VkPipelineColorBlendAttachmentState color_attachment{};
                color_attachment.blendEnable = blend_enabled ? VK_TRUE : VK_FALSE;
                color_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                color_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                color_attachment.colorBlendOp = VK_BLEND_OP_ADD;
                color_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                color_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                color_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
                color_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                    | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                VkPipelineColorBlendStateCreateInfo color_blend{};
                color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
                color_blend.logicOpEnable = VK_FALSE;
                color_blend.attachmentCount = 1;
                color_blend.pAttachments = &color_attachment;

                VkGraphicsPipelineCreateInfo pipeline_info{};
                pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
                pipeline_info.stageCount = static_cast<std::uint32_t>(shader_stages.size());
                pipeline_info.pStages = shader_stages.data();
                pipeline_info.pVertexInputState = &vertex_input;
                pipeline_info.pInputAssemblyState = &input_assembly;
                pipeline_info.pViewportState = &viewport_state;
                pipeline_info.pRasterizationState = &rasterizer;
                pipeline_info.pMultisampleState = &multisample;
                pipeline_info.pDepthStencilState = &depth_stencil;
                pipeline_info.pColorBlendState = &color_blend;
                pipeline_info.pDynamicState = &dynamic_state;
                pipeline_info.layout = layout;
                pipeline_info.renderPass = render_pass_;
                pipeline_info.subpass = 0;
                check_vk(functions_.create_graphics_pipelines(
                    device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &destination), label);
            };

            std::array<VkPipelineShaderStageCreateInfo, 2> world_stages{};
            world_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            world_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            world_stages[0].module = world_vertex_shader;
            world_stages[0].pName = "main";
            world_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            world_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            world_stages[1].module = world_fragment_shader;
            world_stages[1].pName = "main";

            std::array<VkPipelineShaderStageCreateInfo, 2> ui_stages{};
            ui_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            ui_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            ui_stages[0].module = ui_vertex_shader;
            ui_stages[0].pName = "main";
            ui_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            ui_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            ui_stages[1].module = ui_fragment_shader;
            ui_stages[1].pName = "main";

            create_pipeline(world_stages, world_attributes.data(), static_cast<std::uint32_t>(world_attributes.size()),
                pipeline_layout_, false, true, true, graphics_pipeline_, "vkCreateGraphicsPipelines(world/mesh)");
            create_pipeline(world_stages, world_attributes.data(), static_cast<std::uint32_t>(world_attributes.size()),
                pipeline_layout_, true, true, false, sprite_pipeline_, "vkCreateGraphicsPipelines(sprite)");
            create_pipeline(ui_stages, ui_attributes.data(), static_cast<std::uint32_t>(ui_attributes.size()),
                ui_pipeline_layout_, true, false, false, ui_pipeline_, "vkCreateGraphicsPipelines(ui)");
        } catch (...) {
            if (ui_fragment_shader) functions_.destroy_shader_module(device_, ui_fragment_shader, nullptr);
            if (ui_vertex_shader) functions_.destroy_shader_module(device_, ui_vertex_shader, nullptr);
            if (world_fragment_shader) functions_.destroy_shader_module(device_, world_fragment_shader, nullptr);
            functions_.destroy_shader_module(device_, world_vertex_shader, nullptr);
            throw;
        }
        functions_.destroy_shader_module(device_, ui_fragment_shader, nullptr);
        functions_.destroy_shader_module(device_, ui_vertex_shader, nullptr);
        functions_.destroy_shader_module(device_, world_fragment_shader, nullptr);
        functions_.destroy_shader_module(device_, world_vertex_shader, nullptr);
    }

    void create_sync_objects() {
        VkSemaphoreCreateInfo semaphore_info{};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (std::size_t i = 0; i < kFramesInFlight; ++i) {
            check_vk(functions_.create_semaphore(device_, &semaphore_info, nullptr, &image_available_semaphores_[i]),
                "vkCreateSemaphore(image available)");
            check_vk(functions_.create_semaphore(device_, &semaphore_info, nullptr, &render_finished_semaphores_[i]),
                "vkCreateSemaphore(render finished)");
            check_vk(functions_.create_fence(device_, &fence_info, nullptr, &in_flight_fences_[i]),
                "vkCreateFence");
        }
    }

    void destroy_sync_objects() {
        if (!device_) return;
        for (std::size_t i = 0; i < kFramesInFlight; ++i) {
            if (image_available_semaphores_[i] && functions_.destroy_semaphore) {
                functions_.destroy_semaphore(device_, image_available_semaphores_[i], nullptr);
            }
            if (render_finished_semaphores_[i] && functions_.destroy_semaphore) {
                functions_.destroy_semaphore(device_, render_finished_semaphores_[i], nullptr);
            }
            if (in_flight_fences_[i] && functions_.destroy_fence) {
                functions_.destroy_fence(device_, in_flight_fences_[i], nullptr);
            }
            image_available_semaphores_[i] = VK_NULL_HANDLE;
            render_finished_semaphores_[i] = VK_NULL_HANDLE;
            in_flight_fences_[i] = VK_NULL_HANDLE;
        }
    }

    VkSurfaceFormatKHR choose_surface_format(const std::vector<VkSurfaceFormatKHR>& formats) const {
        // D3D12's reference backbuffer is R8G8B8A8_UNORM. Prefer an UNORM Vulkan
        // swapchain as well so shader output is presented with the same transfer
        // behavior instead of receiving an extra automatic linear->sRGB encode.
        // The old SRGB preference made Vulkan substantially brighter than D3D12
        // even when both backends used the same authored light values.
        const auto bgra_unorm = std::find_if(formats.begin(), formats.end(), [](const VkSurfaceFormatKHR& format) {
            return format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
        if (bgra_unorm != formats.end()) return *bgra_unorm;

        const auto rgba_unorm = std::find_if(formats.begin(), formats.end(), [](const VkSurfaceFormatKHR& format) {
            return format.format == VK_FORMAT_R8G8B8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
        if (rgba_unorm != formats.end()) return *rgba_unorm;

        const auto bgra_srgb = std::find_if(formats.begin(), formats.end(), [](const VkSurfaceFormatKHR& format) {
            return format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
        if (bgra_srgb != formats.end()) return *bgra_srgb;

        const auto rgba_srgb = std::find_if(formats.begin(), formats.end(), [](const VkSurfaceFormatKHR& format) {
            return format.format == VK_FORMAT_R8G8B8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
        if (rgba_srgb != formats.end()) return *rgba_srgb;
        return formats.front();
    }

    VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR>& modes) const {
        if (vsync_enabled_) return VK_PRESENT_MODE_FIFO_KHR;
        if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != modes.end()) {
            return VK_PRESENT_MODE_MAILBOX_KHR;
        }
        if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end()) {
            return VK_PRESENT_MODE_IMMEDIATE_KHR;
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR& capabilities) const {
        if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
            return capabilities.currentExtent;
        }

        int pixel_width = pending_width_;
        int pixel_height = pending_height_;
        if ((pixel_width <= 0 || pixel_height <= 0) && window_) {
            (void)SDL_GetWindowSizeInPixels(window_, &pixel_width, &pixel_height);
        }
        if (pixel_width <= 0 || pixel_height <= 0) return {0, 0};

        VkExtent2D extent{
            static_cast<std::uint32_t>(pixel_width),
            static_cast<std::uint32_t>(pixel_height)
        };
        extent.width = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        return extent;
    }

    VkCompositeAlphaFlagBitsKHR choose_composite_alpha(VkCompositeAlphaFlagsKHR supported) const {
        constexpr std::array<VkCompositeAlphaFlagBitsKHR, 4> preferences{
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        };
        for (VkCompositeAlphaFlagBitsKHR value : preferences) {
            if ((supported & value) != 0) return value;
        }
        return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    }

    bool recreate_swapchain() {
        if (!device_ || !physical_device_ || !surface_) return false;

        int pixel_width = 0;
        int pixel_height = 0;
        if (!SDL_GetWindowSizeInPixels(window_, &pixel_width, &pixel_height) || pixel_width <= 0 || pixel_height <= 0) {
            pending_width_ = std::max(pixel_width, 0);
            pending_height_ = std::max(pixel_height, 0);
            swapchain_recreate_pending_ = true;
            return false;
        }
        pending_width_ = pixel_width;
        pending_height_ = pixel_height;

        try {
            // Render passes and graphics pipelines are extent-independent in
            // this backend. Keep them alive across ordinary window resizes so
            // external overlay integrations (notably the Vulkan editor ImGui
            // backend) retain a valid compatible render pass. Only rebuild the
            // pipeline objects when the surface format itself changes.
            const SwapchainSupport support = query_swapchain_support(physical_device_);
            if (support.formats.empty() || support.present_modes.empty()) {
                throw std::runtime_error("Vulkan surface has no usable formats or presentation modes.");
            }
            const VkFormat next_format = choose_surface_format(support.formats).format;
            const bool rebuild_pipeline_objects = !render_pass_ || next_format != swapchain_format_;

            check_vk(functions_.device_wait_idle(device_), "vkDeviceWaitIdle(swapchain recreation)");
            destroy_swapchain_resources(rebuild_pipeline_objects);
            create_swapchain_resources(rebuild_pipeline_objects);
            swapchain_recreate_pending_ = false;
            return true;
        } catch (const std::exception& ex) {
            log::error(std::format("Vulkan swapchain recreation failed: {}", ex.what()));
            swapchain_recreate_pending_ = true;
            return false;
        }
    }

    void create_swapchain_resources(bool rebuild_pipeline_objects = true) {
        const SwapchainSupport support = query_swapchain_support(physical_device_);
        if (support.formats.empty() || support.present_modes.empty()) {
            throw std::runtime_error("Vulkan surface has no usable formats or presentation modes.");
        }

        const VkSurfaceFormatKHR surface_format = choose_surface_format(support.formats);
        if (!rebuild_pipeline_objects && render_pass_ && surface_format.format != swapchain_format_) {
            throw std::runtime_error("Vulkan surface format changed while preserving swapchain pipeline objects.");
        }
        const VkPresentModeKHR present_mode = choose_present_mode(support.present_modes);
        const VkExtent2D extent = choose_extent(support.capabilities);
        if (extent.width == 0 || extent.height == 0) {
            swapchain_recreate_pending_ = true;
            return;
        }

        swapchain_min_image_count_ = std::max(2u, support.capabilities.minImageCount);
        std::uint32_t image_count = support.capabilities.minImageCount + 1;
        if (support.capabilities.maxImageCount > 0 && image_count > support.capabilities.maxImageCount) {
            image_count = support.capabilities.maxImageCount;
        }
        swapchain_min_image_count_ = std::min(swapchain_min_image_count_, image_count);

        VkSwapchainCreateInfoKHR create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        create_info.surface = surface_;
        create_info.minImageCount = image_count;
        create_info.imageFormat = surface_format.format;
        create_info.imageColorSpace = surface_format.colorSpace;
        create_info.imageExtent = extent;
        create_info.imageArrayLayers = 1;
        create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        const std::array<std::uint32_t, 2> queue_family_indices{
            queue_families_.graphics.value(), queue_families_.present.value()};
        if (queue_family_indices[0] != queue_family_indices[1]) {
            create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            create_info.queueFamilyIndexCount = static_cast<std::uint32_t>(queue_family_indices.size());
            create_info.pQueueFamilyIndices = queue_family_indices.data();
        } else {
            create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        create_info.preTransform = support.capabilities.currentTransform;
        create_info.compositeAlpha = choose_composite_alpha(support.capabilities.supportedCompositeAlpha);
        create_info.presentMode = present_mode;
        create_info.clipped = VK_TRUE;
        create_info.oldSwapchain = VK_NULL_HANDLE;

        check_vk(functions_.create_swapchain(device_, &create_info, nullptr, &swapchain_), "vkCreateSwapchainKHR");
        swapchain_format_ = surface_format.format;
        swapchain_extent_ = extent;
        const char* swapchain_format_name = "other";
        switch (swapchain_format_) {
            case VK_FORMAT_B8G8R8A8_UNORM: swapchain_format_name = "BGRA8_UNORM"; break;
            case VK_FORMAT_R8G8B8A8_UNORM: swapchain_format_name = "RGBA8_UNORM"; break;
            case VK_FORMAT_B8G8R8A8_SRGB: swapchain_format_name = "BGRA8_SRGB"; break;
            case VK_FORMAT_R8G8B8A8_SRGB: swapchain_format_name = "RGBA8_SRGB"; break;
            default: break;
        }
        log::info(std::format("Vulkan swapchain: {}x{} | {} | present mode {}",
            extent.width, extent.height, swapchain_format_name, static_cast<int>(present_mode)));
        width_ = static_cast<int>(extent.width);
        height_ = static_cast<int>(extent.height);

        std::uint32_t actual_image_count = 0;
        check_vk(functions_.get_swapchain_images(device_, swapchain_, &actual_image_count, nullptr),
            "vkGetSwapchainImagesKHR(count)");
        swapchain_images_.resize(actual_image_count);
        check_vk(functions_.get_swapchain_images(device_, swapchain_, &actual_image_count, swapchain_images_.data()),
            "vkGetSwapchainImagesKHR(list)");
        swapchain_images_.resize(actual_image_count);

        create_swapchain_image_views();
        create_depth_resources();
        if (rebuild_pipeline_objects || !render_pass_) {
            create_render_pass();
            create_graphics_pipelines();
        }
        create_framebuffers();
        swapchain_recreate_pending_ = false;
    }

    void create_swapchain_image_views() {
        swapchain_image_views_.reserve(swapchain_images_.size());
        for (VkImage image : swapchain_images_) {
            VkImageViewCreateInfo view_info{};
            view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_info.image = image;
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view_info.format = swapchain_format_;
            view_info.components = {
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY};
            view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view_info.subresourceRange.baseMipLevel = 0;
            view_info.subresourceRange.levelCount = 1;
            view_info.subresourceRange.baseArrayLayer = 0;
            view_info.subresourceRange.layerCount = 1;

            VkImageView view = VK_NULL_HANDLE;
            check_vk(functions_.create_image_view(device_, &view_info, nullptr, &view), "vkCreateImageView");
            swapchain_image_views_.push_back(view);
        }
    }

    void create_render_pass() {
        VkAttachmentDescription color_attachment{};
        color_attachment.format = swapchain_format_;
        color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentDescription depth_attachment{};
        depth_attachment.format = depth_format_;
        depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference color_reference{};
        color_reference.attachment = 0;
        color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference depth_reference{};
        depth_reference.attachment = 1;
        depth_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_reference;
        subpass.pDepthStencilAttachment = &depth_reference;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
            | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
            | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
            | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        const std::array<VkAttachmentDescription, 2> attachments{color_attachment, depth_attachment};
        VkRenderPassCreateInfo render_pass_info{};
        render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        render_pass_info.attachmentCount = static_cast<std::uint32_t>(attachments.size());
        render_pass_info.pAttachments = attachments.data();
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;
        render_pass_info.dependencyCount = 1;
        render_pass_info.pDependencies = &dependency;
        check_vk(functions_.create_render_pass(device_, &render_pass_info, nullptr, &render_pass_), "vkCreateRenderPass");
    }

    void create_framebuffers() {
        swapchain_framebuffers_.reserve(swapchain_image_views_.size());
        if (depth_image_views_.size() != swapchain_image_views_.size()) {
            throw std::runtime_error("Vulkan depth attachment count does not match swapchain image count.");
        }
        for (std::size_t index = 0; index < swapchain_image_views_.size(); ++index) {
            const std::array<VkImageView, 2> attachments{
                swapchain_image_views_[index],
                depth_image_views_[index],
            };
            VkFramebufferCreateInfo framebuffer_info{};
            framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebuffer_info.renderPass = render_pass_;
            framebuffer_info.attachmentCount = static_cast<std::uint32_t>(attachments.size());
            framebuffer_info.pAttachments = attachments.data();
            framebuffer_info.width = swapchain_extent_.width;
            framebuffer_info.height = swapchain_extent_.height;
            framebuffer_info.layers = 1;

            VkFramebuffer framebuffer = VK_NULL_HANDLE;
            check_vk(functions_.create_framebuffer(device_, &framebuffer_info, nullptr, &framebuffer), "vkCreateFramebuffer");
            swapchain_framebuffers_.push_back(framebuffer);
        }
    }

    void destroy_swapchain_resources(bool destroy_pipeline_objects = true) {
        if (!device_) return;
        for (VkFramebuffer framebuffer : swapchain_framebuffers_) {
            if (framebuffer && functions_.destroy_framebuffer) {
                functions_.destroy_framebuffer(device_, framebuffer, nullptr);
            }
        }
        swapchain_framebuffers_.clear();

        if (destroy_pipeline_objects) {
            if (graphics_pipeline_ && functions_.destroy_pipeline) {
                functions_.destroy_pipeline(device_, graphics_pipeline_, nullptr);
            }
            graphics_pipeline_ = VK_NULL_HANDLE;
            if (sprite_pipeline_ && functions_.destroy_pipeline) {
                functions_.destroy_pipeline(device_, sprite_pipeline_, nullptr);
            }
            sprite_pipeline_ = VK_NULL_HANDLE;
            if (ui_pipeline_ && functions_.destroy_pipeline) {
                functions_.destroy_pipeline(device_, ui_pipeline_, nullptr);
            }
            ui_pipeline_ = VK_NULL_HANDLE;
            if (pipeline_layout_ && functions_.destroy_pipeline_layout) {
                functions_.destroy_pipeline_layout(device_, pipeline_layout_, nullptr);
            }
            pipeline_layout_ = VK_NULL_HANDLE;
            if (ui_pipeline_layout_ && functions_.destroy_pipeline_layout) {
                functions_.destroy_pipeline_layout(device_, ui_pipeline_layout_, nullptr);
            }
            ui_pipeline_layout_ = VK_NULL_HANDLE;

            if (render_pass_ && functions_.destroy_render_pass) {
                functions_.destroy_render_pass(device_, render_pass_, nullptr);
            }
            render_pass_ = VK_NULL_HANDLE;
        }

        for (VkImageView view : depth_image_views_) {
            if (view && functions_.destroy_image_view) functions_.destroy_image_view(device_, view, nullptr);
        }
        depth_image_views_.clear();
        for (VkImage image : depth_images_) {
            if (image && functions_.destroy_image) functions_.destroy_image(device_, image, nullptr);
        }
        depth_images_.clear();
        for (VkDeviceMemory memory : depth_memories_) {
            if (memory && functions_.free_memory) functions_.free_memory(device_, memory, nullptr);
        }
        depth_memories_.clear();
        if (destroy_pipeline_objects) depth_format_ = VK_FORMAT_UNDEFINED;

        for (VkImageView view : swapchain_image_views_) {
            if (view && functions_.destroy_image_view) {
                functions_.destroy_image_view(device_, view, nullptr);
            }
        }
        swapchain_image_views_.clear();
        swapchain_images_.clear();

        if (swapchain_ && functions_.destroy_swapchain) {
            functions_.destroy_swapchain(device_, swapchain_, nullptr);
        }
        swapchain_ = VK_NULL_HANDLE;
        if (destroy_pipeline_objects) swapchain_format_ = VK_FORMAT_UNDEFINED;
        swapchain_extent_ = {};
        width_ = 0;
        height_ = 0;
    }

    void query_capabilities() {
        VkPhysicalDeviceProperties properties{};
        functions_.get_physical_device_properties(physical_device_, &properties);
        capabilities_.adapter_name = properties.deviceName;
        capabilities_.feature_level_major = static_cast<int>(VK_API_VERSION_MAJOR(properties.apiVersion));
        capabilities_.feature_level_minor = static_cast<int>(VK_API_VERSION_MINOR(properties.apiVersion));

        VkPhysicalDeviceMemoryProperties memory{};
        functions_.get_physical_device_memory_properties(physical_device_, &memory);
        std::uint64_t dedicated = 0;
        for (std::uint32_t i = 0; i < memory.memoryHeapCount; ++i) {
            if ((memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0) {
                dedicated += memory.memoryHeaps[i].size;
            }
        }
        capabilities_.dedicated_video_memory_bytes = dedicated;
        capabilities_.hardware_ray_tracing = false;
        capabilities_.variable_rate_shading = false;
        capabilities_.mesh_shaders = false;
    }

    void recover_current_frame_fence() {
        if (!device_ || !functions_.destroy_fence || !functions_.create_fence) return;
        const std::size_t frame = current_frame_;
        if (in_flight_fences_[frame]) {
            functions_.destroy_fence(device_, in_flight_fences_[frame], nullptr);
            in_flight_fences_[frame] = VK_NULL_HANDLE;
        }
        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        const VkResult result = functions_.create_fence(device_, &fence_info, nullptr, &in_flight_fences_[frame]);
        if (result != VK_SUCCESS) {
            log::error(std::format(
                "Vulkan fence recovery failed with {} ({}).",
                vk_result_name(result), static_cast<int>(result)));
        }
    }

    SDL_Window* window_ = nullptr;
    VulkanFunctions functions_{};

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    QueueFamilies queue_families_{};
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    VkQueue present_queue_ = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    std::uint32_t swapchain_min_image_count_ = 2;
    VkFormat swapchain_format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchain_extent_{};
    std::vector<VkImage> swapchain_images_;
    std::vector<VkImageView> swapchain_image_views_;
    VkFormat depth_format_ = VK_FORMAT_UNDEFINED;
    std::vector<VkImage> depth_images_;
    std::vector<VkDeviceMemory> depth_memories_;
    std::vector<VkImageView> depth_image_views_;
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout world_descriptor_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool world_descriptor_pool_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kFramesInFlight * kMaxLightingViewsPerFrame> world_descriptor_sets_{};
    VkBuffer lighting_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory lighting_memory_ = VK_NULL_HANDLE;
    void* lighting_mapped_ = nullptr;
    VkDeviceSize lighting_stride_ = 0;
    std::size_t lighting_view_cursor_ = 0;
    VkSampler world_texture_sampler_ = VK_NULL_HANDLE;
    VkImage world_texture_image_ = VK_NULL_HANDLE;
    VkDeviceMemory world_texture_memory_ = VK_NULL_HANDLE;
    VkImageView world_texture_view_ = VK_NULL_HANDLE;
    std::uint32_t world_texture_width_ = 0;
    std::uint32_t world_texture_height_ = 0;
    std::uint32_t world_texture_layers_ = 0;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout ui_pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline graphics_pipeline_ = VK_NULL_HANDLE;
    VkPipeline sprite_pipeline_ = VK_NULL_HANDLE;
    VkPipeline ui_pipeline_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> swapchain_framebuffers_;

    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    VkCommandPool upload_command_pool_ = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, kFramesInFlight> command_buffers_{};
    std::array<VkSemaphore, kFramesInFlight> image_available_semaphores_{};
    std::array<VkSemaphore, kFramesInFlight> render_finished_semaphores_{};
    std::array<VkFence, kFramesInFlight> in_flight_fences_{};
    std::array<FrameGeometry, kFramesInFlight> frame_geometry_{};

    const SectorWorld* cached_world_ = nullptr;
    std::uint64_t cached_world_revision_ = 0;
    SectorMesh world_mesh_{};
    std::array<SectorMesh, 4> primitive_meshes_ = build_primitive_meshes();
    std::vector<BootstrapVertex> scene_vertices_;
    std::vector<std::uint32_t> scene_indices_;
    std::vector<DrawBatch> scene_batches_;
    std::vector<BootstrapVertex> ui_vertices_;
    bool geometry_capacity_warning_emitted_ = false;

    VkDescriptorSetLayout ui_descriptor_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool ui_descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet ui_descriptor_set_ = VK_NULL_HANDLE;
    VkSampler ui_sampler_ = VK_NULL_HANDLE;
    VkImage ui_atlas_image_ = VK_NULL_HANDLE;
    VkDeviceMemory ui_atlas_memory_ = VK_NULL_HANDLE;
    VkImageView ui_atlas_view_ = VK_NULL_HANDLE;
    std::uint32_t ui_atlas_width_ = 0;
    std::uint32_t ui_atlas_height_ = 0;
    std::uint64_t ui_atlas_revision_ = 0;

    std::size_t current_frame_ = 0;
    std::uint32_t image_index_ = 0;
    int width_ = 0;
    int height_ = 0;
    int pending_width_ = 0;
    int pending_height_ = 0;
    bool frame_open_ = false;
    bool swapchain_recreate_pending_ = false;
    bool vsync_enabled_ = true;
    std::array<float, 4> current_clear_color_{0.018f, 0.024f, 0.035f, 1.0f};

    RendererCapabilities capabilities_{};
    RenderFrameStats frame_stats_{};
};

} // namespace

VulkanNativeAccess* vulkan_native_access(RenderBackend* backend) {
    return dynamic_cast<VulkanNativeAccess*>(backend);
}

const VulkanNativeAccess* vulkan_native_access(const RenderBackend* backend) {
    return dynamic_cast<const VulkanNativeAccess*>(backend);
}

std::unique_ptr<RenderBackend> create_vulkan_render_backend() {
    return std::make_unique<VulkanRenderer>();
}

} // namespace vespera
