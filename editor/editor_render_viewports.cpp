#include "editor_render_viewports.hpp"

#include "editor_state.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace vespera::editor {

std::optional<vespera::RenderViewport> scene_view_3d_pixel_viewport(
    const EditorState& state,
    SDL_Window* window
) {
    const auto& view = state.scene_view_3d;
    if (!view.visible || !window) return std::nullopt;

    int logical_width = 0;
    int logical_height = 0;
    int pixel_width = 0;
    int pixel_height = 0;
    if (!SDL_GetWindowSize(window, &logical_width, &logical_height)
        || !SDL_GetWindowSizeInPixels(window, &pixel_width, &pixel_height)
        || logical_width <= 0 || logical_height <= 0 || pixel_width <= 0 || pixel_height <= 0) {
        return std::nullopt;
    }

    const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    const ImVec2 viewport_origin = main_viewport ? main_viewport->Pos : ImVec2{};
    const float scale_x = static_cast<float>(pixel_width) / static_cast<float>(logical_width);
    const float scale_y = static_cast<float>(pixel_height) / static_cast<float>(logical_height);

    const float local_min_x = view.content_min.x - viewport_origin.x;
    const float local_min_y = view.content_min.y - viewport_origin.y;
    const float local_max_x = view.content_max.x - viewport_origin.x;
    const float local_max_y = view.content_max.y - viewport_origin.y;

    int x0 = static_cast<int>(std::floor(local_min_x * scale_x));
    int y0 = static_cast<int>(std::floor(local_min_y * scale_y));
    int x1 = static_cast<int>(std::ceil(local_max_x * scale_x));
    int y1 = static_cast<int>(std::ceil(local_max_y * scale_y));

    x0 = std::clamp(x0, 0, pixel_width);
    y0 = std::clamp(y0, 0, pixel_height);
    x1 = std::clamp(x1, 0, pixel_width);
    y1 = std::clamp(y1, 0, pixel_height);
    if (x1 <= x0 || y1 <= y0) return std::nullopt;

    return vespera::RenderViewport{x0, y0, x1 - x0, y1 - y0};
}

std::optional<vespera::RenderViewport> game_view_pixel_viewport(
    const EditorState& state,
    SDL_Window* window
) {
    const auto& view = state.game_view;
    if (!view.visible || !window) return std::nullopt;

    int logical_width = 0;
    int logical_height = 0;
    int pixel_width = 0;
    int pixel_height = 0;
    if (!SDL_GetWindowSize(window, &logical_width, &logical_height)
        || !SDL_GetWindowSizeInPixels(window, &pixel_width, &pixel_height)
        || logical_width <= 0 || logical_height <= 0 || pixel_width <= 0 || pixel_height <= 0) {
        return std::nullopt;
    }

    const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    const ImVec2 viewport_origin = main_viewport ? main_viewport->Pos : ImVec2{};
    const float scale_x = static_cast<float>(pixel_width) / static_cast<float>(logical_width);
    const float scale_y = static_cast<float>(pixel_height) / static_cast<float>(logical_height);

    int x0 = static_cast<int>(std::floor((view.content_min.x - viewport_origin.x) * scale_x));
    int y0 = static_cast<int>(std::floor((view.content_min.y - viewport_origin.y) * scale_y));
    int x1 = static_cast<int>(std::ceil((view.content_max.x - viewport_origin.x) * scale_x));
    int y1 = static_cast<int>(std::ceil((view.content_max.y - viewport_origin.y) * scale_y));
    x0 = std::clamp(x0, 0, pixel_width);
    y0 = std::clamp(y0, 0, pixel_height);
    x1 = std::clamp(x1, 0, pixel_width);
    y1 = std::clamp(y1, 0, pixel_height);
    if (x1 <= x0 || y1 <= y0) return std::nullopt;
    return vespera::RenderViewport{x0, y0, x1 - x0, y1 - y0};
}


} // namespace vespera::editor
