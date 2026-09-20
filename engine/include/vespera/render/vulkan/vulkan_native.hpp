#pragma once

#include <cstdint>

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES 1
#endif
#include <vulkan/vulkan.h>

namespace vespera {

class RenderBackend;

// Narrow bridge used by native editor overlays such as Dear ImGui. Runtime
// renderer ownership stays inside the Vulkan backend; callers may only borrow
// handles while the backend is initialized and an active command buffer only
// between begin_frame() and end_frame().
class VulkanNativeAccess {
public:
    virtual ~VulkanNativeAccess() = default;

    [[nodiscard]] virtual std::uint32_t vulkan_api_version() const = 0;
    [[nodiscard]] virtual VkInstance vulkan_instance() const = 0;
    [[nodiscard]] virtual VkPhysicalDevice vulkan_physical_device() const = 0;
    [[nodiscard]] virtual VkDevice vulkan_device() const = 0;
    [[nodiscard]] virtual std::uint32_t vulkan_graphics_queue_family() const = 0;
    [[nodiscard]] virtual VkQueue vulkan_graphics_queue() const = 0;
    [[nodiscard]] virtual VkRenderPass vulkan_render_pass() const = 0;
    [[nodiscard]] virtual VkCommandBuffer vulkan_active_command_buffer() const = 0;
    [[nodiscard]] virtual std::uint32_t vulkan_min_image_count() const = 0;
    [[nodiscard]] virtual std::uint32_t vulkan_image_count() const = 0;
    [[nodiscard]] virtual PFN_vkVoidFunction vulkan_load_function(const char* name) const = 0;
    virtual void vulkan_wait_for_gpu() = 0;
};

[[nodiscard]] VulkanNativeAccess* vulkan_native_access(RenderBackend* backend);
[[nodiscard]] const VulkanNativeAccess* vulkan_native_access(const RenderBackend* backend);

} // namespace vespera
