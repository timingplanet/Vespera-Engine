#include "editor_frame_ui.hpp"

#include "editor_assets.hpp"
#include "editor_build_pipeline.hpp"
#include "editor_build_window.hpp"
#include "editor_console_panel.hpp"
#include "editor_game_view.hpp"
#include "editor_hierarchy.hpp"
#include "editor_inspector.hpp"
#include "editor_menu.hpp"
#include "editor_project_panel.hpp"
#include "editor_scene_3d_panel.hpp"
#include "editor_sector_view.hpp"
#include "editor_selection.hpp"
#include "editor_session_dialogs.hpp"
#include "editor_state.hpp"
#include "editor_ui_authoring.hpp"
#include "editor_workspace.hpp"

namespace vespera::editor {

void draw_editor_ui_frame(EditorState& state, bool& running, bool interactive) {
    repair_selection(state);
    if (interactive) update_editor_build_job(state);

    running = draw_menu(state) && running;
    if (interactive
        && state.pending_action == PendingAction::Exit
        && !state.dirty
        && !state.request_unsaved_popup) {
        execute_pending_action(state, running);
    }

    draw_main_dockspace(state);
    if (interactive) handle_shortcuts(state);
    draw_path_popups(state, running);
    draw_hierarchy(state);
    draw_scene_view(state);
    draw_scene_view_3d(state);
    draw_game_view(state);
    draw_ui_authoring(state);
    draw_rml_source_authoring(state);
    draw_inspector(state);
    draw_assets(state);
    draw_console(state);
    draw_build_game_window(state);
}

} // namespace vespera::editor
