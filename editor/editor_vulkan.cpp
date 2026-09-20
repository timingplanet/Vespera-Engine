#include "editor_vulkan.hpp"

#if defined(VESPERA_EDITOR_VULKAN)

#include "editor_frame_ui.hpp"
#include "editor_play_controls.hpp"
#include "editor_render_viewports.hpp"
#include "editor_state.hpp"
#include "editor_startup_splash.hpp"
#include "editor_window_identity.hpp"

#include <vespera/ui/ui_render.hpp>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <format>

namespace vespera::editor {
namespace {

void render_editor_runtime_ui(
    EditorState& state,
    vespera::RenderBackend& render_backend,
    const vespera::RenderViewport& viewport
) {
    if (!editor_is_playing(state)) return;

    if (state.play_rml_ui_loaded && state.play_rml_ui) {
        state.play_rml_view_width = std::max(1, viewport.width);
        state.play_rml_view_height = std::max(1, viewport.height);
        state.play_rml_ui->resize(state.play_rml_view_width, state.play_rml_view_height);
        const auto ui_packet = state.play_rml_ui->build_packet();
        render_backend.render_ui(ui_packet, &viewport);
        return;
    }
    if (!state.play_ui_loaded) return;

    vespera::UiPointerState ui_pointer{};
    const ImGuiIO& ui_io = ImGui::GetIO();
    const float view_w = std::max(1.0f, state.game_view.content_max.x - state.game_view.content_min.x);
    const float view_h = std::max(1.0f, state.game_view.content_max.y - state.game_view.content_min.y);
    if (state.game_view.hovered && !state.game_view.input_captured) {
        ui_pointer.available = true;
        ui_pointer.position = {
            std::clamp((ui_io.MousePos.x - state.game_view.content_min.x) / view_w, 0.0f, 1.0f)
                * static_cast<float>(viewport.width),
            std::clamp((ui_io.MousePos.y - state.game_view.content_min.y) / view_h, 0.0f, 1.0f)
                * static_cast<float>(viewport.height)
        };
        ui_pointer.primary_down = ui_io.MouseDown[ImGuiMouseButton_Left];
        ui_pointer.primary_pressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        ui_pointer.primary_released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    }

    vespera::UiNavigationState ui_navigation{};
    if (state.game_view.focused && !state.game_view.input_captured) {
        const bool tab = ImGui::IsKeyPressed(ImGuiKey_Tab, false);
        ui_navigation.focus_next = tab && !ui_io.KeyShift;
        ui_navigation.focus_previous = tab && ui_io.KeyShift;
        ui_navigation.activate_pressed = ImGui::IsKeyPressed(ImGuiKey_Enter, false);
    }

    vespera::UiTextInputState ui_text_input{};
    if (state.game_view.focused && !state.game_view.input_captured) {
        ui_text_input.text = state.game_view_text_input;
        ui_text_input.backspace = state.game_view_backspace_pending;
    }

    auto* ui_runtime = state.play_runtime ? &state.play_runtime->ui_runtime_state() : nullptr;
    const auto ui_packet = state.play_ui_cache.build_packet(
        state.play_ui_document,
        static_cast<float>(viewport.width),
        static_cast<float>(viewport.height),
        ui_pointer,
        ui_runtime,
        ui_navigation,
        ui_text_input);
    state.game_view_text_input.clear();
    state.game_view_backspace_pending = false;
    render_backend.render_ui(ui_packet, &viewport);
}

} // namespace

PFN_vkVoidFunction EditorVulkanHost::imgui_loader(const char* function_name, void* user_data) {
    auto* native = static_cast<vespera::VulkanNativeAccess*>(user_data);
    return native ? native->vulkan_load_function(function_name) : nullptr;
}

void EditorVulkanHost::imgui_check_vk_result(VkResult result) {
    if (result == VK_SUCCESS) return;
    std::fprintf(stderr, "[Vespera Editor Vulkan] VkResult = %d\n", static_cast<int>(result));
}

void EditorVulkanHost::imgui_preview_draw_callback(const ImDrawList*, const ImDrawCmd* command) {
    if (!command || !command->UserCallbackData) return;
    auto* context = static_cast<PreviewDrawContext*>(command->UserCallbackData);
    if (context->host) context->host->render_preview_callback(context->game_view);
}

void EditorVulkanHost::render_preview_callback(bool game_view) {
    if (!window_ || !render_backend_ || !state_ || !frame_in_progress_) return;
    EditorState& state = *state_;

    if (game_view) {
        const auto viewport = game_view_pixel_viewport(state, window_);
        if (!viewport) return;
        const bool playing = editor_is_playing(state);
        const vespera::Scene& game_scene = playing ? state.play_scene : state.scene;
        const double game_time = playing ? state.play_time_seconds : frame_total_seconds_;
        render_backend_->render_scene(game_scene, game_time, &game_scene.camera, &*viewport);
        render_editor_runtime_ui(state, *render_backend_, *viewport);
        return;
    }

    const auto viewport = scene_view_3d_pixel_viewport(state, window_);
    if (!viewport) return;
    render_backend_->render_scene(
        state.scene, frame_total_seconds_, &state.scene_view_3d.camera, &*viewport);
}

bool EditorVulkanHost::initialize_renderer(SDL_Window* window) {
    window_ = window;
    render_backend_ = vespera::create_render_backend(vespera::RenderBackendType::Vulkan);
    if (!render_backend_ || !render_backend_->initialize(window_)) {
        std::fprintf(stderr, "Vespera Vulkan editor renderer initialization failed.\n");
        shutdown_renderer();
        return false;
    }

    vulkan_ = vespera::vulkan_native_access(render_backend_.get());
    if (!vulkan_ || !vulkan_->vulkan_instance() || !vulkan_->vulkan_physical_device()
        || !vulkan_->vulkan_device() || !vulkan_->vulkan_graphics_queue()
        || !vulkan_->vulkan_render_pass()) {
        std::fprintf(stderr, "Vespera editor requires Vulkan native renderer access on this platform.\n");
        shutdown_renderer();
        return false;
    }

    int pixel_width = 0;
    int pixel_height = 0;
    if (SDL_GetWindowSizeInPixels(window_, &pixel_width, &pixel_height)) {
        render_backend_->resize(pixel_width, pixel_height);
    }
    // The renderer creates its initial swapchain during initialize(), so the
    // splash can be presented before Dear ImGui takes over the overlay pass.
    present_editor_startup_splash(*render_backend_);
    return true;
}

bool EditorVulkanHost::initialize_imgui_vulkan_backend() {
    if (!vulkan_) return false;

    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.ApiVersion = vulkan_->vulkan_api_version();
    init_info.Instance = vulkan_->vulkan_instance();
    init_info.PhysicalDevice = vulkan_->vulkan_physical_device();
    init_info.Device = vulkan_->vulkan_device();
    init_info.QueueFamily = vulkan_->vulkan_graphics_queue_family();
    init_info.Queue = vulkan_->vulkan_graphics_queue();
    init_info.DescriptorPool = VK_NULL_HANDLE;
    init_info.DescriptorPoolSize = 64;
    init_info.MinImageCount = std::max(2u, vulkan_->vulkan_min_image_count());
    init_info.ImageCount = std::max(init_info.MinImageCount, vulkan_->vulkan_image_count());
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.PipelineInfoMain.RenderPass = vulkan_->vulkan_render_pass();
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.UseDynamicRendering = false;
    init_info.Allocator = nullptr;
    init_info.CheckVkResultFn = &EditorVulkanHost::imgui_check_vk_result;
    init_info.MinAllocationSize = 1024 * 1024;

    if (!ImGui_ImplVulkan_Init(&init_info)) {
        std::fprintf(stderr, "ImGui Vulkan backend initialization failed.\n");
        return false;
    }
    imgui_vulkan_initialized_ = true;
    imgui_render_pass_ = init_info.PipelineInfoMain.RenderPass;
    imgui_min_image_count_ = init_info.MinImageCount;
    imgui_image_count_ = init_info.ImageCount;
    return true;
}

bool EditorVulkanHost::initialize_imgui(SDL_Window* window) {
    if (!render_backend_ || !vulkan_ || !window) return false;
    if (!ImGui_ImplVulkan_LoadFunctions(
            vulkan_->vulkan_api_version(), &EditorVulkanHost::imgui_loader, vulkan_)) {
        std::fprintf(stderr, "Dear ImGui could not load Vulkan functions from the Vespera renderer.\n");
        return false;
    }
    if (!ImGui_ImplSDL3_InitForVulkan(window)) {
        std::fprintf(stderr, "ImGui SDL3/Vulkan backend initialization failed.\n");
        return false;
    }
    imgui_sdl_initialized_ = true;
    if (!initialize_imgui_vulkan_backend()) {
        shutdown_imgui_backends();
        return false;
    }
    return true;
}

bool EditorVulkanHost::refresh_imgui_vulkan_backend_if_needed() {
    if (!vulkan_ || !imgui_vulkan_initialized_) return false;
    const VkRenderPass current_render_pass = vulkan_->vulkan_render_pass();
    const std::uint32_t current_min_image_count = std::max(2u, vulkan_->vulkan_min_image_count());
    const std::uint32_t current_image_count = std::max(current_min_image_count, vulkan_->vulkan_image_count());
    if (current_render_pass == imgui_render_pass_
        && current_min_image_count == imgui_min_image_count_
        && current_image_count == imgui_image_count_) {
        return true;
    }

    // A surface-format or swapchain-image-count transition invalidates state
    // cached inside Dear ImGui's Vulkan backend. Rebuild only the renderer
    // backend; the ImGui context and SDL platform backend remain intact.
    vulkan_->vulkan_wait_for_gpu();
    ImGui_ImplVulkan_Shutdown();
    imgui_vulkan_initialized_ = false;
    imgui_render_pass_ = VK_NULL_HANDLE;
    imgui_min_image_count_ = 0;
    imgui_image_count_ = 0;
    return initialize_imgui_vulkan_backend();
}

std::string EditorVulkanHost::renderer_name() const {
    return render_backend_ ? std::string(render_backend_->name()) : std::string{};
}

void EditorVulkanHost::resize(int pixel_width, int pixel_height) {
    if (render_backend_) render_backend_->resize(pixel_width, pixel_height);
}

bool EditorVulkanHost::attach_frame(
    EditorState& state,
    bool& running,
    std::chrono::steady_clock::time_point start_time
) {
    if (!window_ || !render_backend_ || !vulkan_) return false;
    scene_preview_context_ = {this, false};
    game_preview_context_ = {this, true};
    state.direct_render_previews = true;
    state.scene_view_3d.preview_texture = ImTextureID_Invalid;
    state.game_view.preview_texture = ImTextureID_Invalid;
    state.direct_scene_draw_callback = &EditorVulkanHost::imgui_preview_draw_callback;
    state.direct_scene_draw_user_data = &scene_preview_context_;
    state.direct_game_draw_callback = &EditorVulkanHost::imgui_preview_draw_callback;
    state.direct_game_draw_user_data = &game_preview_context_;
    state_ = &state;
    running_ = &running;
    start_time_ = start_time;
    return true;
}

void EditorVulkanHost::detach_frame() {
    if (state_) {
        state_->direct_render_previews = false;
        state_->direct_scene_draw_callback = nullptr;
        state_->direct_scene_draw_user_data = nullptr;
        state_->direct_game_draw_callback = nullptr;
        state_->direct_game_draw_user_data = nullptr;
    }
    state_ = nullptr;
    running_ = nullptr;
    frame_in_progress_ = false;
}

bool EditorVulkanHost::render_frame(bool interactive) {
    if (!window_ || !render_backend_ || !vulkan_ || !state_ || !running_) return false;
    if (frame_in_progress_) return *running_;
    frame_in_progress_ = true;

    EditorState& state = *state_;
    bool& running = *running_;
    const auto frame_begin = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const double total_seconds = std::chrono::duration<double>(now - start_time_).count();
    frame_total_seconds_ = total_seconds;
    const auto play_update_begin = std::chrono::steady_clock::now();
    update_play_clock(state, total_seconds);
    const auto play_update_end = std::chrono::steady_clock::now();
    state.play_update_ms = std::chrono::duration<double, std::milli>(play_update_end - play_update_begin).count();

    const vespera::RenderFrameConfig frame_config{{0.018f, 0.024f, 0.035f, 1.0f}};
    const auto render_begin = std::chrono::steady_clock::now();
    if (!render_backend_->begin_frame(frame_config)) {
        // Minimized/out-of-date swapchains are expected to skip frames. Keep
        // the editor alive and let SDL resize/expose events drive recreation.
        frame_in_progress_ = false;
        return running;
    }

    // begin_frame() is where the engine performs deferred swapchain recreation.
    // If that recreation changed render-pass compatibility or image count, end
    // one clear-only frame and rebuild the ImGui Vulkan renderer before drawing
    // UI through the new swapchain. Ordinary resizes preserve the render pass
    // and therefore do not take this path.
    const bool imgui_refresh_needed = vulkan_->vulkan_render_pass() != imgui_render_pass_
        || std::max(2u, vulkan_->vulkan_min_image_count()) != imgui_min_image_count_
        || std::max(std::max(2u, vulkan_->vulkan_min_image_count()), vulkan_->vulkan_image_count()) != imgui_image_count_;
    if (imgui_refresh_needed) {
        if (!render_backend_->end_frame() || !refresh_imgui_vulkan_backend_if_needed()) {
            std::fprintf(stderr, "Vespera Vulkan editor could not refresh the ImGui swapchain integration.\n");
            running = false;
        }
        frame_in_progress_ = false;
        return running;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    draw_editor_ui_frame(state, running, interactive);
    ImGui::Render();
    SDL_SetWindowTitle(window_, window_title(state).c_str());

    // Scene/Game rendering is injected into ImGui draw order by callbacks queued
    // from the corresponding panel. This ensures dock/window backgrounds are
    // drawn first and gizmos/status overlays are drawn after the 3D preview.
    const VkCommandBuffer command_buffer = vulkan_->vulkan_active_command_buffer();
    if (!command_buffer) {
        std::fprintf(stderr, "Vespera Vulkan editor overlay lost the active command buffer.\n");
        running = false;
        (void)render_backend_->end_frame();
    } else {
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer);
        if (!render_backend_->end_frame()) {
            std::fprintf(stderr, "Vespera Vulkan editor frame presentation failed.\n");
            running = false;
        }
    }

    const auto frame_end = std::chrono::steady_clock::now();
    const auto to_ms = [](auto duration) {
        return std::chrono::duration<double, std::milli>(duration).count();
    };
    state.performance.render = render_backend_->frame_stats();
    state.performance.record_frame(
        to_ms(frame_end - frame_begin),
        state.play_update_ms,
        to_ms(frame_end - render_begin));
    frame_in_progress_ = false;
    return running;
}

void EditorVulkanHost::shutdown_imgui_backends() {
    if (imgui_vulkan_initialized_) {
        if (vulkan_) vulkan_->vulkan_wait_for_gpu();
        ImGui_ImplVulkan_Shutdown();
        imgui_vulkan_initialized_ = false;
        imgui_render_pass_ = VK_NULL_HANDLE;
        imgui_min_image_count_ = 0;
        imgui_image_count_ = 0;
    }
    if (imgui_sdl_initialized_) {
        ImGui_ImplSDL3_Shutdown();
        imgui_sdl_initialized_ = false;
    }
}

void EditorVulkanHost::shutdown_renderer() {
    detach_frame();
    vulkan_ = nullptr;
    if (render_backend_) {
        render_backend_->shutdown();
        render_backend_.reset();
    }
    window_ = nullptr;
}

} // namespace vespera::editor

#endif
