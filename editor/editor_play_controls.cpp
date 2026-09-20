#include "editor_play_controls.hpp"
#include "editor_console.hpp"
#include "editor_history.hpp"
#include "play_runtime.hpp"
#include <vespera/assets/texture_importer.hpp>
#include <vespera/runtime/player_project.hpp>
#include <vespera/ui/ui_io.hpp>
#include <vespera/ui/rmlui_surface.hpp>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <algorithm>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
namespace vespera::editor {

bool editor_is_playing(const EditorState& state) {
    return state.play_state != EditorPlayState::Editing;
}
void set_game_input_capture(EditorState& state, bool captured) {
    if (!editor_is_playing(state)) captured = false;

    SDL_Window* window = nullptr;
    if (state.game_view.capture_window_id != 0) {
        window = SDL_GetWindowFromID(static_cast<SDL_WindowID>(state.game_view.capture_window_id));
    }
    if (!window) window = SDL_GetKeyboardFocus();
    if (!window) window = SDL_GetMouseFocus();

    if (!captured) {
        // Release is intentionally idempotent. Focus-loss can arrive after SDL
        // has already cleared keyboard focus, and the editor state can become
        // desynchronized from the OS if we return early merely because the flag
        // is false. Always disable relative mode on the remembered window when
        // one is available.
        if (window && !SDL_SetWindowRelativeMouseMode(window, false)) {
            push_console(state, ConsoleEntry::Level::Warning,
                std::format("Could not release Game input capture: {}", SDL_GetError()));
        }
        state.game_view.input_captured = false;
        state.game_view.capture_window_id = 0;
        return;
    }

    if (state.game_view.input_captured) return;
    if (!window) {
        push_console(state, ConsoleEntry::Level::Warning,
            "Could not enable Game input capture because the editor window does not currently have input focus.");
        state.game_view.input_captured = false;
        state.game_view.capture_window_id = 0;
        return;
    }
    if (!SDL_SetWindowRelativeMouseMode(window, true)) {
        push_console(state, ConsoleEntry::Level::Warning,
            std::format("Could not enable Game input capture: {}", SDL_GetError()));
        state.game_view.input_captured = false;
        state.game_view.capture_window_id = 0;
        return;
    }

    state.game_view.input_captured = true;
    state.game_view.capture_window_id = static_cast<std::uint32_t>(SDL_GetWindowID(window));
}
void feed_play_runtime_input(EditorState& state) {
    if (!state.play_runtime || !editor_is_playing(state)) return;
    auto& runtime = *state.play_runtime;
    runtime.begin_input_frame();
    const bool captured = state.game_view.input_captured;
    const bool rml_ui_input = state.play_rml_ui_loaded
        && state.game_view.visible
        && state.game_view.focused
        && !captured;

    int key_count = 0;
    const bool* keys = SDL_GetKeyboardState(&key_count);
    const auto raw_key_down = [&](SDL_Scancode scancode) {
        const int index = static_cast<int>(scancode);
        return keys && index >= 0 && index < key_count && keys[index];
    };
    const auto gameplay_key_down = [&](SDL_Scancode scancode) {
        return captured && raw_key_down(scancode);
    };
    const auto ui_key_down = [&](SDL_Scancode scancode) {
        return (captured || rml_ui_input) && raw_key_down(scancode);
    };

    runtime.set_key(vespera::Key::W, gameplay_key_down(SDL_SCANCODE_W));
    runtime.set_key(vespera::Key::A, gameplay_key_down(SDL_SCANCODE_A));
    runtime.set_key(vespera::Key::S, gameplay_key_down(SDL_SCANCODE_S));
    runtime.set_key(vespera::Key::D, gameplay_key_down(SDL_SCANCODE_D));
    runtime.set_key(vespera::Key::LeftShift,
        gameplay_key_down(SDL_SCANCODE_LSHIFT) || gameplay_key_down(SDL_SCANCODE_RSHIFT));
    runtime.set_key(vespera::Key::Space, gameplay_key_down(SDL_SCANCODE_SPACE));
    runtime.set_key(vespera::Key::L, gameplay_key_down(SDL_SCANCODE_L));

    // Navigation/editing keys remain available while the Game view is focused
    // with a project RML surface active, even before first-person capture.
    runtime.set_key(vespera::Key::Enter, ui_key_down(SDL_SCANCODE_RETURN));
    runtime.set_key(vespera::Key::Tab, ui_key_down(SDL_SCANCODE_TAB));
    runtime.set_key(vespera::Key::Backspace, ui_key_down(SDL_SCANCODE_BACKSPACE));
    runtime.set_key(vespera::Key::Up, ui_key_down(SDL_SCANCODE_UP));
    runtime.set_key(vespera::Key::Down, ui_key_down(SDL_SCANCODE_DOWN));
    runtime.set_key(vespera::Key::Left, ui_key_down(SDL_SCANCODE_LEFT));
    runtime.set_key(vespera::Key::Right, ui_key_down(SDL_SCANCODE_RIGHT));

    if (captured) {
        float mouse_x = 0.0f;
        float mouse_y = 0.0f;
        SDL_GetRelativeMouseState(&mouse_x, &mouse_y);
        runtime.add_mouse_delta(mouse_x, mouse_y);
    } else if (state.play_rml_ui_loaded && state.game_view.visible) {
        const ImGuiIO& io = ImGui::GetIO();
        const float view_w = std::max(1.0f, state.game_view.content_max.x - state.game_view.content_min.x);
        const float view_h = std::max(1.0f, state.game_view.content_max.y - state.game_view.content_min.y);
        const float local_x = (io.MousePos.x - state.game_view.content_min.x) / view_w;
        const float local_y = (io.MousePos.y - state.game_view.content_min.y) / view_h;
        if (state.game_view.hovered) {
            runtime.set_mouse_position(
                std::clamp(local_x, 0.0f, 1.0f) * static_cast<float>(std::max(1, state.play_rml_view_width)),
                std::clamp(local_y, 0.0f, 1.0f) * static_cast<float>(std::max(1, state.play_rml_view_height)));
        } else {
            runtime.set_mouse_position(-10000.0f, -10000.0f);
        }
        runtime.set_mouse_button(vespera::MouseButton::Left, state.game_view.hovered && io.MouseDown[ImGuiMouseButton_Left]);
        runtime.set_mouse_button(vespera::MouseButton::Right, state.game_view.hovered && io.MouseDown[ImGuiMouseButton_Right]);
        runtime.set_mouse_button(vespera::MouseButton::Middle, state.game_view.hovered && io.MouseDown[ImGuiMouseButton_Middle]);
        if (rml_ui_input && !state.game_view_text_input.empty()) {
            runtime.add_text_input(state.game_view_text_input);
            state.game_view_text_input.clear();
        }
        // Backspace is delivered through InputSystem for RmlUi; the legacy flag
        // is only consumed by the .slui compatibility renderer.
        state.game_view_backspace_pending = false;
    }
}
void start_play_mode(EditorState& state) {
    if (editor_is_playing(state)) return;
    if (!state.project_loaded && state.scene_path.empty()) {
        push_console(state, ConsoleEntry::Level::Warning,
            "Play Mode requires either an open Vespera project or an explicitly opened scene. The empty editor scene will not be run.");
        return;
    }
    state.last_runtime_events.clear();
    state.last_runtime_event_sequence = 0;
    state.last_runtime_assembly_generation = 0;
    commit_active_edit(state);
    PlayEditBackup backup;
    backup.scene = state.scene;
    backup.selection = state.selection;
    backup.selected_entity_ids = state.selected_entity_ids;
    backup.hierarchy_anchor_id = state.hierarchy_anchor_id;
    backup.undo_stack = state.undo_stack;
    backup.redo_stack = state.redo_stack;
    backup.current_state_id = state.current_state_id;
    backup.saved_state_id = state.saved_state_id;
    backup.next_state_id = state.next_state_id;
    backup.dirty = state.dirty;
    state.play_edit_backup = std::move(backup);
    state.play_scene = state.scene;
    state.play_time_seconds = 0.0;
    state.play_last_wall_seconds = -1.0;
    state.play_state = EditorPlayState::Playing;
    state.game_view.focus_pending = true;
    state.play_ui_loaded = false;
    state.play_ui_cache.clear();
    state.play_ui_document.clear();
    if (state.play_rml_ui) {
        state.play_rml_ui->shutdown();
        state.play_rml_ui.reset();
    }
    state.play_rml_ui_loaded = false;
    state.play_rml_view_width = std::max(1, state.project_loaded ? state.project.window_width : 1280);
    state.play_rml_view_height = std::max(1, state.project_loaded ? state.project.window_height : 720);

    if (state.project_loaded) {
        // Match standalone vespera_player: project startup RML is the preferred
        // runtime UI. This closes the old Editor-Play-only .slui path for new projects.
        const auto rml_selection = vespera::select_runtime_rml_document(state.project, state.asset_catalog);
        if (rml_selection.document) {
            std::vector<std::filesystem::path> font_paths;
            for (const auto* font_asset : state.asset_catalog.records_of_kind(vespera::AssetKind::Font)) {
                if (font_asset) font_paths.push_back(font_asset->absolute_path);
            }
            auto rml = std::make_unique<vespera::RmlUiSurface>();
            const auto loaded = rml->initialize(
                rml_selection.document->absolute_path,
                state.play_rml_view_width,
                state.play_rml_view_height,
                font_paths);
            if (loaded) {
                state.play_rml_ui_loaded = true;
                state.play_rml_ui = std::move(rml);
                push_console(state, ConsoleEntry::Level::Info,
                    "Play Mode UI: RmlUi - " + rml_selection.message);
            } else {
                push_console(state, ConsoleEntry::Level::Warning,
                    "Play Mode RML UI failed to initialize: " + loaded.message);
            }
        }

        // Compatibility path for the reference QA project and older projects that
        // have no startup RML. Do not grow this path with new authoring features.
        if (!state.play_rml_ui_loaded) {
            const vespera::AssetRecord* ui_asset = state.asset_catalog.find("ui/reference_hud.slui");
            if (!ui_asset) {
                const auto ui_records = state.asset_catalog.records_of_kind(vespera::AssetKind::UiDocument);
                if (!ui_records.empty()) ui_asset = ui_records.front();
            }
            if (ui_asset) {
                const auto loaded = vespera::load_ui_document(state.play_ui_document, ui_asset->absolute_path);
                if (loaded) {
                    state.play_ui_cache.prepare_images(state.play_ui_document,
                        [&](const vespera::AssetReference& reference) -> std::optional<vespera::TextureData> {
                            const auto resolved = state.asset_catalog.resolve_reference(reference);
                            if (!resolved || resolved.record->kind != vespera::AssetKind::Texture) return std::nullopt;
                            const auto imported = vespera::import_texture(resolved.record->absolute_path, resolved.record->display_name);
                            if (!imported) return std::nullopt;
                            return imported.texture;
                        },
                        [&](const vespera::AssetReference& reference) -> std::optional<std::filesystem::path> {
                            const auto resolved = state.asset_catalog.resolve_reference(reference);
                            if (!resolved || resolved.record->kind != vespera::AssetKind::Font) return std::nullopt;
                            return resolved.record->absolute_path;
                        });
                    state.play_ui_loaded = true;
                    push_console(state, ConsoleEntry::Level::Info,
                        std::format("Play Mode UI: .slui compatibility document '{}' ({} nodes, {} image warning(s))",
                            ui_asset->relative_path.generic_string(), state.play_ui_document.nodes().size(),
                            state.play_ui_cache.image_warnings().size()));
                } else {
                    push_console(state, ConsoleEntry::Level::Warning, "Play Mode .slui UI: " + loaded.message);
                }
            }
        }
    }
    state.play_runtime = std::make_unique<vespera::editor::PlayRuntime>();
    if (state.project_loaded) {
        state.play_runtime->start(
            state.play_scene, state.project, state.asset_catalog, state.scene_path,
            state.project.root_directory / ".vespera" / "managed",
            state.play_ui_loaded ? &state.play_ui_document : nullptr,
            state.play_rml_ui_loaded ? state.play_rml_ui.get() : nullptr,
            &state.performance);
        const auto& runtime_status = state.play_runtime->status();
        push_console(state,
            runtime_status.managed_ready ? ConsoleEntry::Level::Info : ConsoleEntry::Level::Warning,
            "Play Mode: " + runtime_status.message);
    } else {
        push_console(state, ConsoleEntry::Level::Warning,
            "Play Mode started without a loaded project; embedded C#/audio services are unavailable.");
    }
    append_command_audit(state, vespera::editor::EditorCommandKind::EnterPlayMode, "Enter Play Mode");
    push_console(state, ConsoleEntry::Level::Info,
        "Play Mode started from an isolated scene copy. Open Game and click the viewport to capture WASD + mouse; Escape releases input. Stop discards runtime changes.");
}
void stop_play_mode(EditorState& state) {
    if (!editor_is_playing(state)) return;
    set_game_input_capture(state, false);
    if (state.play_runtime) {
        state.play_runtime->stop();
        state.last_runtime_events = state.play_runtime->runtime_events_since(0);
        state.last_runtime_event_sequence = state.play_runtime->latest_runtime_event_sequence();
        state.last_runtime_assembly_generation = state.play_runtime->assembly_generation();
        state.play_runtime.reset();
    }
    if (state.play_edit_backup) {
        auto backup = std::move(*state.play_edit_backup);
        state.scene = std::move(backup.scene);
        state.selection = std::move(backup.selection);
        state.selected_entity_ids = std::move(backup.selected_entity_ids);
        state.hierarchy_anchor_id = backup.hierarchy_anchor_id;
        state.undo_stack = std::move(backup.undo_stack);
        state.redo_stack = std::move(backup.redo_stack);
        state.current_state_id = backup.current_state_id;
        state.saved_state_id = backup.saved_state_id;
        state.next_state_id = backup.next_state_id;
        state.dirty = backup.dirty;
    }
    state.play_edit_backup.reset();
    state.play_ui_loaded = false;
    state.play_ui_document.clear();
    state.play_ui_cache.clear();
    state.play_rml_ui_loaded = false;
    if (state.play_rml_ui) {
        state.play_rml_ui->shutdown();
        state.play_rml_ui.reset();
    }
    state.play_scene = {};
    state.play_time_seconds = 0.0;
    state.play_last_wall_seconds = -1.0;
    state.play_state = EditorPlayState::Editing;
    append_command_audit(state, vespera::editor::EditorCommandKind::ExitPlayMode, "Exit Play Mode");
    push_console(state, ConsoleEntry::Level::Info, "Play Mode stopped; edit scene restored.");
}
void toggle_play_pause(EditorState& state) {
    if (!editor_is_playing(state)) return;
    if (state.play_state == EditorPlayState::Paused) {
        state.play_state = EditorPlayState::Playing;
        state.play_last_wall_seconds = -1.0;
        if (state.play_runtime) state.play_runtime->set_paused(false);
        append_command_audit(state, vespera::editor::EditorCommandKind::ResumePlayMode, "Resume Play Mode");
    } else {
        state.play_state = EditorPlayState::Paused;
        if (state.play_runtime) state.play_runtime->set_paused(true);
        append_command_audit(state, vespera::editor::EditorCommandKind::PausePlayMode, "Pause Play Mode");
    }
}
void step_play_mode(EditorState& state) {
    if (!editor_is_playing(state)) return;
    state.play_state = EditorPlayState::Paused;
    if (state.play_runtime) state.play_runtime->set_paused(true);
    feed_play_runtime_input(state);
    if (state.play_runtime) state.play_runtime->update(state.play_scene, 1.0 / 60.0);
    state.play_time_seconds += 1.0 / 60.0;
    append_command_audit(state, vespera::editor::EditorCommandKind::StepPlayMode, "Step Play Mode");
}
void update_play_clock(EditorState& state, double wall_seconds) {
    if (!editor_is_playing(state)) return;
    if (state.play_state == EditorPlayState::Playing) {
        double dt = 0.0;
        if (state.play_last_wall_seconds >= 0.0) {
            dt = std::clamp(wall_seconds - state.play_last_wall_seconds, 0.0, 0.1);
            state.play_time_seconds += dt;
        }
        state.play_last_wall_seconds = wall_seconds;
        feed_play_runtime_input(state);
        if (state.play_runtime && dt > 0.0) state.play_runtime->update(state.play_scene, dt);
    } else {
        state.play_last_wall_seconds = wall_seconds;
        // Keep previous/current input snapshots coherent while paused so Resume
        // does not synthesize a burst of Pressed actions.
        feed_play_runtime_input(state);
    }
}

} // namespace vespera::editor
