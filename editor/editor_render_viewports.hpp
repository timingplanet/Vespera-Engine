#pragma once

#include <optional>
#include <vespera/render/render_backend.hpp>

struct SDL_Window;

namespace vespera::editor {
struct EditorState;
std::optional<vespera::RenderViewport> scene_view_3d_pixel_viewport(const EditorState& state, SDL_Window* window);
std::optional<vespera::RenderViewport> game_view_pixel_viewport(const EditorState& state, SDL_Window* window);
}
