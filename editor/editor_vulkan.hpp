#pragma once

#if defined(VESPERA_EDITOR_VULKAN)

#include <vespera/render/render_backend.hpp>
#include <vespera/render/vulkan/vulkan_native.hpp>

#include <SDL3/SDL.h>

#include <chrono>
#include <memory>
#include <string>

struct ImDrawList;
struct ImDrawCmd;

namespace vespera::editor {

struct EditorState;

// Editor host backed by the engine's Vulkan renderer. This is the native Linux
// editor renderer and an opt-in renderer on Windows, where D3D12 remains the
// default. Scene/Game previews are injected into Dear ImGui draw order as Vulkan
// callbacks, then the normal editor overlay continues on top in the same render pass.
class EditorVulkanHost {
public:
    bool initialize_renderer(SDL_Window* window);
    bool initialize_imgui(SDL_Window* window);

    [[nodiscard]] std::string renderer_name() const;
    void resize(int pixel_width, int pixel_height);

    bool attach_frame(EditorState& state, bool& running, std::chrono::steady_clock::time_point start_time);
    void detach_frame();
    bool render_frame(bool interactive);

    void shutdown_imgui_backends();
    void shutdown_renderer();

private:
    struct PreviewDrawContext {
        EditorVulkanHost* host = nullptr;
        bool game_view = false;
    };

    static PFN_vkVoidFunction imgui_loader(const char* function_name, void* user_data);
    static void imgui_check_vk_result(VkResult result);
    static void imgui_preview_draw_callback(const ImDrawList* draw_list, const ImDrawCmd* command);
    void render_preview_callback(bool game_view);
    bool initialize_imgui_vulkan_backend();
    bool refresh_imgui_vulkan_backend_if_needed();

    SDL_Window* window_ = nullptr;
    std::unique_ptr<vespera::RenderBackend> render_backend_;
    vespera::VulkanNativeAccess* vulkan_ = nullptr;
    EditorState* state_ = nullptr;
    bool* running_ = nullptr;
    std::chrono::steady_clock::time_point start_time_{};
    double frame_total_seconds_ = 0.0;
    PreviewDrawContext scene_preview_context_{};
    PreviewDrawContext game_preview_context_{};
    bool frame_in_progress_ = false;
    bool imgui_sdl_initialized_ = false;
    bool imgui_vulkan_initialized_ = false;
    VkRenderPass imgui_render_pass_ = VK_NULL_HANDLE;
    std::uint32_t imgui_min_image_count_ = 0;
    std::uint32_t imgui_image_count_ = 0;
};

} // namespace vespera::editor

#endif
