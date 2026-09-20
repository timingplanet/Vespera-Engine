#pragma once

#include <array>
#include <string_view>

namespace vespera::editor {

struct AutomationToolDescriptor {
    std::string_view name;
    std::string_view description;
    bool mutating;
    bool undoable;
    bool allowed_during_play;
};

inline constexpr std::array<AutomationToolDescriptor, 60> kAutomationTools{{
    {"vespera_get_state", "Read the loaded project/scene, play state, dirty state and current selection.", false, false, true},
    {"vespera_list_entities", "List authored scene entities with ids, hierarchy and component summary.", false, false, true},
    {"vespera_get_entity", "Read one entity including local/world transform and component keys.", false, false, true},
    {"vespera_list_assets", "List project assets with stable IDs, kinds and project-relative paths.", false, false, true},
    {"vespera_get_build_manifest", "Read the deterministic transitive standalone-build asset closure and validation state.", false, false, true},
    {"vespera_export_project", "Export/package Debug, Development, or Release through Vespera's deterministic build closure.", true, false, false},
    {"vespera_get_console", "Read recent editor Console messages for automated QA and failure diagnosis.", false, false, true},
    {"vespera_get_command_log", "Read recent typed editor command audit records and transaction state ids.", false, false, true},
    {"vespera_list_capabilities", "Read automation tools plus registered editor-extension commands/API version.", false, false, true},
    {"vespera_select_entity", "Select one entity by stable scene object id.", false, false, false},
    {"vespera_create_entity", "Create an empty, sprite, trigger or point-light entity through editor transactions.", true, true, false},
    {"vespera_create_primitive", "Create a Cube, Plane, Cylinder or Sphere using the editor transaction/history path.", true, true, false},
    {"vespera_duplicate_entity", "Duplicate one entity through the same undoable editor path as Ctrl+D.", true, true, false},
    {"vespera_delete_entity", "Delete one entity (and descendants) through the undoable editor path.", true, true, false},
    {"vespera_reparent_entity", "Parent, reparent or unparent an entity while preserving world transform.", true, true, false},
    {"vespera_set_transform", "Set an entity transform in world or local space as one undoable editor command.", true, true, false},
    {"vespera_add_component", "Add a built-in component by stable semantic component key.", true, true, false},
    {"vespera_remove_component", "Remove a removable built-in component by stable semantic component key.", true, true, false},
    {"vespera_get_component_property", "Read one reflected built-in component property.", false, false, true},
    {"vespera_set_component_property", "Set one reflected built-in component property through an undoable transaction.", true, true, false},
    {"vespera_instantiate_prefab", "Instantiate a project prefab by stable asset id or path.", true, true, false},
    {"vespera_assign_material", "Assign a .slmat project Material to a Mesh Renderer by stable asset id or path.", true, true, false},
    {"vespera_create_material", "Create a new Lit/Unlit Material asset inside the project Assets tree.", true, false, false},
    {"vespera_refresh_assets", "Refresh or reimport project assets through the editor.", true, false, false},
    {"vespera_validate_scene", "Run Vespera scene validation and return warning/error counts.", false, false, true},
    {"vespera_undo", "Undo the latest editor transaction.", true, false, false},
    {"vespera_redo", "Redo the latest editor transaction.", true, false, false},
    {"vespera_play", "Control in-editor Play Mode: enter, stop, pause, resume or step.", true, false, true},
    {"vespera_save_scene", "Save the current edit scene through Vespera's verified guarded save path.", true, false, false},
    {"vespera_build_csharp", "Build project C# scripts; active Play Mode reloads a successful build automatically.", true, false, true},
    {"vespera_open_scene", "Open a project Scene asset through the editor's normal guarded scene-open path.", true, false, false},
    {"vespera_get_project_settings", "Read project settings used by Play Mode and game builds.", false, false, true},
    {"vespera_set_project_setting", "Change one supported project setting and optionally save it.", true, false, false},
    {"vespera_set_build_include", "Add or remove an asset from the project's explicit game-build includes and save the project.", true, false, false},
    {"vespera_check_asset_delete", "Check whether an asset can be deleted and return any project references that block deletion.", false, false, true},
    {"vespera_move_asset", "Move or rename an asset while preserving its identity and updating project references.", true, false, false},
    {"vespera_repair_asset_fallbacks", "Update outdated saved asset paths while preserving asset IDs.", true, false, false},
    {"vespera_create_ui_document", "Create a .slui UI document inside the project Assets folder.", true, false, false},
    {"vespera_get_ui_document", "Read one .slui document, layout summary and UI nodes.", false, false, true},
    {"vespera_add_ui_node", "Add a supported UI node (Panel/Text/Image/Button/Progress/Scroll/List/Grid/Tabs/Modal/Tooltip/TextInput) to a .slui document.", true, false, false},
    {"vespera_set_ui_node", "Set common hierarchy/layout/widget/text/image properties on one .slui node.", true, false, false},
    {"vespera_delete_ui_node", "Delete one UI node and its descendants from a .slui document.", true, false, false},
    {"vespera_reparent_ui_node", "Move a UI node under another node while validating hierarchy rules.", true, false, false},
    {"vespera_validate_ui_document", "Load and validate one .slui document and report layout/hierarchy warnings.", false, false, true},
    {"vespera_get_ui_layout", "Resolve a .slui document at a requested viewport size and return its layout and clipping state.", false, false, true},
    {"vespera_set_entity_metadata", "Set an entity's name, tag, layer, or enabled state with editor undo support.", true, true, false},
    {"vespera_reorder_entity", "Reorder one authored entity relative to another with editor undo support.", true, true, false},
    {"vespera_get_asset_dependencies", "Inspect dependencies and reverse dependents for one project asset.", false, false, true},
    {"vespera_check_project_integrity", "Run combined scene, project, asset, game-build, and UI-document validation.", false, false, true},
    {"vespera_read_csharp_source", "Read one validated project C# source file.", false, false, true},
    {"vespera_write_csharp_source", "Create or replace one project C# source file with safe backup/replacement handling.", true, false, false},
    {"vespera_attach_csharp_script", "Attach a managed script class to an Entity through undoable scene history.", true, true, false},
    {"vespera_run_qa_scenario", "Run a bounded repeatable editor stress scenario through normal command paths.", true, true, false},
    {"vespera_inject_input_action", "Inject or clear a named input action override in active Play Mode for repeatable testing.", true, false, true},
    {"vespera_pointer_event", "Inject pointer move/down/up/click into the active Play Mode UI without OS-level mouse synthesis.", true, false, true},
    {"vespera_ui_navigation", "Inject UI focus-next/focus-previous/activate navigation into active Play Mode.", true, false, true},
    {"vespera_text_input", "Inject text/backspace/clear through the native runtime TextInput path for the focused UI field.", true, false, true},
    {"vespera_get_ui_runtime_state", "Read active Play Mode UI hover/press/focus/click and focused TextInput state.", false, false, true},
    {"vespera_get_runtime_events", "Read structured managed lifecycle/trigger events with frame and assembly generation.", false, false, true},
    {"vespera_clear_runtime_events", "Clear buffered managed runtime QA events without changing authored state.", true, false, true},
}};

} // namespace vespera::editor
