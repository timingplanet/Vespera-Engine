#include "editor_inspector.hpp"

#include "editor_asset_interactions.hpp"
#include "editor_assets.hpp"
#include "editor_console.hpp"
#include "editor_history.hpp"
#include "editor_managed.hpp"
#include "editor_prefabs.hpp"
#include "editor_project_session.hpp"
#include "editor_scene_3d.hpp"
#include "editor_scene_commands.hpp"
#include "editor_selection.hpp"
#include "editor_state.hpp"
#include "editor_style.hpp"
#include "editor_ui_authoring.hpp"
#include "rml_source_editor.hpp"

#include <vespera/assets/audio_clip_asset.hpp>
#include <vespera/assets/font_asset.hpp>
#include <vespera/assets/material_asset.hpp>
#include <vespera/assets/sprite_clip_asset.hpp>
#include <vespera/assets/sprite_sheet_asset.hpp>
#include <vespera/assets/texture_importer.hpp>
#include <vespera/render/material.hpp>
#include <vespera/scene/component_access.hpp>
#include <vespera/scene/scene_hierarchy.hpp>
#include <vespera/ui/ui_io.hpp>
#include <vespera/ui/ui_render.hpp>
#include <vespera/world/sector_world.hpp>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cstddef>
#include <cfloat>
#include <filesystem>
#include <format>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace vespera::editor {

bool material_combo(const char* label, vespera::MaterialId& id, const vespera::SectorWorld& world) {
    const auto& materials = world.materials();
    const char* preview = "<none>";
    if (id != vespera::kInvalidMaterial && id < materials.size()) {
        preview = materials[id].name.c_str();
    }

    bool changed = false;
    if (ImGui::BeginCombo(label, preview)) {
        const bool none_selected = id == vespera::kInvalidMaterial;
        if (ImGui::Selectable("<none>", none_selected)) {
            id = vespera::kInvalidMaterial;
            changed = true;
        }
        for (std::size_t i = 0; i < materials.size(); ++i) {
            const bool selected = id == static_cast<vespera::MaterialId>(i);
            if (ImGui::Selectable(materials[i].name.c_str(), selected)) {
                id = static_cast<vespera::MaterialId>(i);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool texture_combo(const char* label, vespera::TextureId& id, const vespera::SectorWorld& world) {
    const auto& textures = world.textures();
    const char* preview = "<none>";
    if (id != vespera::kInvalidTexture && id < textures.size()) {
        preview = textures[id].name.c_str();
    }

    bool changed = false;
    if (ImGui::BeginCombo(label, preview)) {
        const bool none_selected = id == vespera::kInvalidTexture;
        if (ImGui::Selectable("<none>", none_selected)) {
            id = vespera::kInvalidTexture;
            changed = true;
        }
        for (std::size_t i = 0; i < textures.size(); ++i) {
            const bool selected = id == static_cast<vespera::TextureId>(i);
            if (ImGui::Selectable(textures[i].name.c_str(), selected)) {
                id = static_cast<vespera::TextureId>(i);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool sprite_clip_combo(const char* label, std::string& clip_name, const vespera::Scene& scene) {
    const char* preview = clip_name.empty() ? "<static>" : clip_name.c_str();
    bool changed = false;
    if (ImGui::BeginCombo(label, preview)) {
        if (ImGui::Selectable("<static>", clip_name.empty())) {
            clip_name.clear();
            changed = true;
        }
        for (const auto& clip : scene.sprite_clips) {
            const bool selected = clip.name == clip_name;
            const std::string label_text = std::format(
                "{}  ({} dirs x {} frames @ {:.1f} fps)",
                clip.name,
                clip.direction_count,
                clip.frame_count,
                clip.frames_per_second
            );
            if (ImGui::Selectable(label_text.c_str(), selected)) {
                clip_name = clip.name;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}











void draw_inspector(EditorState& state) {
    ImGui::Begin("Inspector");
    draw_asset_move_popup(state);

    if (state.selection.kind == SelectionKind::Asset) {
        const auto* record = state.asset_catalog.find_by_id(state.selection.asset_id);
        if (!record) {
            state.selection = {};
            ImGui::TextDisabled("Selected asset is no longer present in the catalog.");
            ImGui::End();
            return;
        }

        const auto& palette = editor_ui_palette();
        draw_panel_heading("Asset", vespera::asset_kind_name(record->kind));
        ImGui::BeginChild("AssetSummaryCard", ImVec2(0.0f, 112.0f), true);
        ImGui::SetWindowFontScale(1.08f);
        ImGui::TextUnformatted(record->display_name.c_str());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::TextColored(palette.info, "%s", vespera::asset_kind_name(record->kind).data());
        ImGui::Spacing();
        ImGui::TextDisabled("Path");
        ImGui::SameLine(72.0f); ImGui::TextWrapped("%s", record->relative_path.generic_string().c_str());
        ImGui::TextDisabled("Importer");
        ImGui::SameLine(72.0f); ImGui::TextUnformatted(record->importer.c_str());
        ImGui::TextDisabled("State");
        ImGui::SameLine(72.0f); ImGui::TextColored(palette.success, "%s", vespera::asset_import_state_name(record->import_state).data());
        ImGui::EndChild();
        ImGui::Spacing();
        ImGui::TextDisabled("Source size");
        ImGui::Text("%llu bytes", static_cast<unsigned long long>(record->source_size));

        if (record->kind == vespera::AssetKind::Texture) {
            auto& cache = state.asset_inspection_cache;
            if (cache.asset_id != record->asset_id || cache.source_hash != record->source_hash) {
                cache = {};
                cache.asset_id = record->asset_id;
                cache.source_hash = record->source_hash;
                cache.texture_attempted = true;
                const auto imported = vespera::import_texture(record->absolute_path, record->display_name);
                cache.texture_decoded = static_cast<bool>(imported);
                cache.message = imported.message;
                if (imported) {
                    cache.texture_width = imported.texture.width;
                    cache.texture_height = imported.texture.height;
                    cache.decoded_bytes = imported.texture.rgba8.size();
                }
            }
            ImGui::SeparatorText("Texture");
            if (cache.texture_decoded) {
                ImGui::Text("Dimensions: %u x %u", cache.texture_width, cache.texture_height);
                ImGui::Text("Decoded: RGBA8 (%zu bytes)", cache.decoded_bytes);
            } else {
                ImGui::TextDisabled("Preview unavailable");
                ImGui::TextWrapped("%s", cache.message.c_str());
            }

            auto& edit = state.texture_import_edit;
            if (edit.asset_id != record->asset_id) {
                edit.asset_id = record->asset_id;
                edit.settings = record->texture_settings;
            }
            ImGui::SeparatorText("Import Settings");
            ImGui::TextDisabled("Import settings are stored with this asset.");
            const char* usage_items[] = {"World", "Sprite", "UI", "Data"};
            const char* filter_items[] = {"Nearest", "Linear"};
            const char* wrap_items[] = {"Repeat", "Clamp"};
            const char* color_items[] = {"sRGB", "Linear"};
            const char* alpha_items[] = {"Auto", "Opaque", "Cutout", "Blend"};
            const char* mip_items[] = {"Auto", "On", "Off"};
            int usage = static_cast<int>(edit.settings.usage);
            int filter = static_cast<int>(edit.settings.filter);
            int wrap_u = static_cast<int>(edit.settings.wrap_u);
            int wrap_v = static_cast<int>(edit.settings.wrap_v);
            int color_space = static_cast<int>(edit.settings.color_space);
            int alpha = static_cast<int>(edit.settings.alpha_mode);
            int mipmaps = static_cast<int>(edit.settings.mipmaps);
            if (ImGui::Combo("Usage", &usage, usage_items, IM_ARRAYSIZE(usage_items))) edit.settings.usage = static_cast<vespera::TextureUsage>(usage);
            if (ImGui::Combo("Filter", &filter, filter_items, IM_ARRAYSIZE(filter_items))) edit.settings.filter = static_cast<vespera::TextureFilter>(filter);
            if (ImGui::Combo("Wrap U", &wrap_u, wrap_items, IM_ARRAYSIZE(wrap_items))) edit.settings.wrap_u = static_cast<vespera::TextureWrap>(wrap_u);
            if (ImGui::Combo("Wrap V", &wrap_v, wrap_items, IM_ARRAYSIZE(wrap_items))) edit.settings.wrap_v = static_cast<vespera::TextureWrap>(wrap_v);
            if (ImGui::Combo("Color Space", &color_space, color_items, IM_ARRAYSIZE(color_items))) edit.settings.color_space = static_cast<vespera::TextureColorSpace>(color_space);
            if (ImGui::Combo("Alpha", &alpha, alpha_items, IM_ARRAYSIZE(alpha_items))) edit.settings.alpha_mode = static_cast<vespera::TextureAlphaMode>(alpha);
            if (ImGui::Combo("Mipmaps", &mipmaps, mip_items, IM_ARRAYSIZE(mip_items))) edit.settings.mipmaps = static_cast<vespera::TextureMipmapMode>(mipmaps);
            ImGui::InputInt("Max Size", &edit.settings.max_size);
            edit.settings.max_size = (std::max)(0, edit.settings.max_size);
            const bool settings_dirty = !(edit.settings == record->texture_settings) || !record->texture_settings_authored;
            ImGui::BeginDisabled(!settings_dirty);
            if (ImGui::Button("Apply Import Settings")) {
                std::string error;
                if (state.asset_catalog.save_texture_import_settings(record->asset_id, edit.settings, &error)) {
                    const auto saved_path = record->relative_path.generic_string();
                    push_console(state, ConsoleEntry::Level::Info, "Texture import settings saved: " + saved_path);
                    refresh_asset_catalog(state, false, true);
                    ImGui::End();
                    return;
                } else {
                    push_console(state, ConsoleEntry::Level::Error, "Texture import settings failed: " + error);
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Revert Settings")) edit.settings = record->texture_settings;
            if (!record->texture_settings_authored) ImGui::TextDisabled("Using default import settings until Apply is pressed.");
        }

        if (record->kind == vespera::AssetKind::Font) {
            ImGui::SeparatorText("Font Source");
            const auto inspected = vespera::inspect_font_asset(record->absolute_path);
            if (inspected) {
                ImGui::Text("Container: %s", vespera::font_container_name(inspected.font.container));
                ImGui::Text("SFNT tables: %u", inspected.font.table_count);
                ImGui::TextDisabled("Font source validated successfully.");
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.42f, 1.0f), "Invalid font source");
                ImGui::TextWrapped("%s", inspected.message.c_str());
            }
        }

        if (record->kind == vespera::AssetKind::RmlDocument
            || record->kind == vespera::AssetKind::RmlStyleSheet) {
            ImGui::SeparatorText("RmlUi Source");
            const auto dependencies = state.asset_catalog.dependencies_of(record->asset_id);
            std::size_t broken = 0;
            for (const auto* dependency : dependencies) if (dependency && !dependency->resolved) ++broken;
            ImGui::Text("Dependencies: %zu | Broken: %zu", dependencies.size(), broken);
            ImGui::TextDisabled("Local RML/RCSS references are updated when assets are moved in the Project panel.");
            if (ImGui::Button("Open RML / RCSS Source", ImVec2(-1.0f, 0.0f))) {
                std::string error;
                if (!vespera::editor::open_rml_source_editor(
                        state.rml_source_editor, record->absolute_path, record->asset_id, &error)) {
                    push_console(state, ConsoleEntry::Level::Error, "RmlUi source editor: " + error);
                }
            }
            for (const auto* dependency : dependencies) {
                if (!dependency || dependency->resolved) continue;
                ImGui::BulletText("Broken %s: %s", dependency->reason.c_str(), dependency->reference.c_str());
            }
        }

        if (record->kind == vespera::AssetKind::UiDocument) {
            ImGui::SeparatorText("Runtime UI Document");
            vespera::UiDocument ui_document;
            const auto loaded_ui = vespera::load_ui_document(ui_document, record->absolute_path);
            if (!loaded_ui) {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.42f, 1.0f), "Invalid .slui document");
                ImGui::TextWrapped("%s", loaded_ui.message.c_str());
            } else {
                const auto layout = vespera::resolve_ui_layout(ui_document, 1280.0f, 720.0f);
                std::size_t canvas_count = 0, button_count = 0, image_count = 0, text_count = 0, widget_count = 0;
                for (const auto& node : ui_document.nodes()) {
                    if (node.type == vespera::UiNodeType::Canvas) ++canvas_count;
                    else if (node.type == vespera::UiNodeType::Button) ++button_count;
                    else if (node.type == vespera::UiNodeType::Image) ++image_count;
                    else if (node.type == vespera::UiNodeType::Text) ++text_count;
                    else ++widget_count;
                }
                ImGui::Text("Nodes: %zu | Canvas: %zu | Text: %zu | Image: %zu | Button: %zu | Other: %zu",
                    ui_document.nodes().size(), canvas_count, text_count, image_count, button_count, widget_count);
                ImGui::Text("1280x720 layout: %zu resolved | %zu warning(s)", layout.nodes.size(), layout.warnings.size());
                ImGui::TextDisabled("Legacy .slui document. New project UI should use RML/RCSS.");
                if (ImGui::Button("Open UI Authoring", ImVec2(-1.0f, 0.0f))) open_ui_authoring(state, *record);
                for (const auto& warning : layout.warnings) ImGui::BulletText("%s", warning.c_str());
            }
        }

        if (record->kind == vespera::AssetKind::Material) {
            auto& edit = state.material_asset_edit;
            if (edit.asset_id != record->asset_id || edit.source_hash != record->source_hash) {
                edit = {};
                edit.asset_id = record->asset_id;
                edit.source_hash = record->source_hash;
                const auto loaded = vespera::load_material_asset(record->absolute_path);
                edit.loaded = static_cast<bool>(loaded);
                edit.message = loaded.message;
                if (loaded) edit.material = loaded.material;
            }
            ImGui::SeparatorText("Material");
            if (!edit.loaded) {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.42f, 1.0f), "Invalid Material asset");
                ImGui::TextWrapped("%s", edit.message.c_str());
            } else {
                ImGui::InputText("Name", &edit.material.name);
                const char* shaders[] = {"Vespera/Lit", "Vespera/Unlit"};
                int shader_index = edit.material.properties.shader == vespera::BuiltinMaterialShader::Unlit ? 1 : 0;
                if (ImGui::Combo("Shader", &shader_index, shaders, IM_ARRAYSIZE(shaders))) {
                    edit.material.properties.shader = shader_index == 1
                        ? vespera::BuiltinMaterialShader::Unlit : vespera::BuiltinMaterialShader::Lit;
                }

                const auto base_resolution = state.asset_catalog.resolve_reference(edit.material.base_texture);
                std::string base_label = "None";
                if (base_resolution && base_resolution.record->kind == vespera::AssetKind::Texture)
                    base_label = base_resolution.record->display_name;
                else if (!edit.material.base_texture.path.empty()) base_label = edit.material.base_texture.path.filename().string();
                ImGui::TextUnformatted("Base Texture");
                ImGui::SameLine(108.0f);
                if (ImGui::Button((base_label + "##MaterialBaseTexture").c_str(), ImVec2(-28.0f, 0.0f))) {
                    if (base_resolution && base_resolution.record->kind == vespera::AssetKind::Texture) {
                        state.selection = {SelectionKind::Asset, 0, kNoSubSelection, vespera::kInvalidSceneObjectId, kNoSubSelection, base_resolution.record->asset_id};
                    }
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("VESPERA_PROJECT_ASSET")) {
                        if (const auto* texture = asset_record_from_payload(state, payload); texture && texture->kind == vespera::AssetKind::Texture) {
                            edit.material.base_texture = {texture->asset_id, texture->relative_path};
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("X##ClearMaterialTexture")) edit.material.base_texture = {};

                ImGui::ColorEdit4("Base Color", edit.material.properties.base_color.data());
                ImGui::ColorEdit3("Emission Color", edit.material.properties.emission_color.data());
                ImGui::DragFloat("Emission Strength", &edit.material.properties.emission_strength, 0.05f, 0.0f, 32.0f, "%.2f");
                edit.material.properties.emission_strength = std::max(0.0f, edit.material.properties.emission_strength);
                ImGui::SliderFloat("Alpha Cutoff", &edit.material.properties.alpha_cutoff, 0.0f, 1.0f, "%.2f");
                ImGui::TextDisabled("Lit uses Vespera point lighting. Unlit ignores scene lights. Emission is additive.");

                if (ImGui::Button("Apply Material", ImVec2(132.0f, 0.0f))) {
                    const auto saved = vespera::save_material_asset(record->absolute_path, edit.material);
                    if (saved) {
                        append_command_audit(state, vespera::editor::EditorCommandKind::SaveMaterialAsset,
                            "Save Material Asset", true, state.current_state_id, state.current_state_id,
                            vespera::kInvalidSceneObjectId, record->asset_id);
                        push_console(state, ConsoleEntry::Level::Info, "Material saved: " + record->relative_path.generic_string());
                        state.material_asset_edit = {};
                        refresh_asset_catalog(state, false, true);
                        ImGui::End();
                        return;
                    }
                    push_console(state, ConsoleEntry::Level::Error, "Material save failed: " + saved.message);
                }
                ImGui::SameLine();
                if (ImGui::Button("Revert Material")) state.material_asset_edit = {};
            }
        }

        if (record->kind == vespera::AssetKind::SpriteClip) {
            ImGui::SeparatorText("Sprite Clip");
            const auto loaded = vespera::load_sprite_clip_asset(record->absolute_path, state.asset_catalog, state.scene.world);
            if (loaded) {
                ImGui::Text("%u direction(s) x %u frame(s)", loaded.clip.direction_count, loaded.clip.frame_count);
                ImGui::Text("Playback: %.2f fps | %s", loaded.clip.frames_per_second, loaded.clip.loop ? "loop" : "once");
                if (ImGui::Button("Apply / Replace in Scene")) {
                    auto before = capture_snapshot(state);
                    bool replaced = false;
                    for (auto& clip : state.scene.sprite_clips) {
                        if (clip.name == loaded.clip.name) { clip = loaded.clip; replaced = true; break; }
                    }
                    if (!replaced) state.scene.sprite_clips.push_back(loaded.clip);
                    record_immediate_edit(state, std::move(before), replaced ? "Replace Sprite Clip from Asset" : "Add Sprite Clip from Asset");
                }
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.42f, 1.0f), "Invalid sprite clip asset");
                ImGui::TextWrapped("%s", loaded.message.c_str());
            }
        }

        if (record->kind == vespera::AssetKind::SpriteSheet) {
            ImGui::SeparatorText("Sprite Sheet");
            vespera::SectorWorld scratch_world;
            const auto preview = vespera::load_sprite_sheet_asset(record->absolute_path, state.asset_catalog, scratch_world);
            if (preview) {
                ImGui::Text("Sheet: %u x %u", preview.sheet_width, preview.sheet_height);
                ImGui::Text("Frames: %u direction(s) x %u frame(s) | %u x %u each",
                    preview.clip.direction_count, preview.clip.frame_count, preview.frame_width, preview.frame_height);
                ImGui::Text("Playback: %.2f fps | %s", preview.clip.frames_per_second, preview.clip.loop ? "loop" : "once");
                ImGui::TextDisabled("Source: %s | %s", preview.source_path.generic_string().c_str(),
                    preview.source_resolved_by_id ? "asset ID" : "path");
                if (preview.source_fallback_stale) ImGui::TextColored(ImVec4(1.0f, 0.68f, 0.25f, 1.0f), "Saved source path is outdated; the asset link still resolves.");
                ImGui::TextDisabled("Source references remain linked when assets are moved in the Project panel.");
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.42f, 1.0f), "Invalid sprite sheet asset");
                ImGui::TextWrapped("%s", preview.message.c_str());
            }
        }

        if (record->kind == vespera::AssetKind::AudioClip) {
            ImGui::SeparatorText("Audio Clip");
            const auto loaded = vespera::load_audio_clip_asset(record->absolute_path, state.asset_catalog);
            if (loaded) {
                ImGui::Text("Source: %s", loaded.clip.source_path.generic_string().c_str());
                ImGui::SameLine(); ImGui::TextDisabled("(%s)", loaded.clip.source_resolved_by_id ? "asset ID" : "path");
                if (loaded.clip.source_fallback_stale) ImGui::TextColored(ImVec4(1.0f, 0.68f, 0.25f, 1.0f), "Saved source path is outdated; the asset link still resolves.");
                ImGui::Text("Volume: %.2f | %s", loaded.clip.volume, loaded.clip.loop ? "loop" : "one-shot");
                ImGui::Text("Spatial: %s", loaded.clip.spatial ? "yes" : "no");
                if (loaded.clip.spatial) ImGui::Text("Distance: %.2f - %.2f", loaded.clip.min_distance, loaded.clip.max_distance);
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.42f, 1.0f), "Invalid audio clip asset");
                ImGui::TextWrapped("%s", loaded.message.c_str());
            }
        }

        ImGui::SeparatorText("Dependencies");
        const auto dependencies = state.asset_catalog.dependencies_of(record->asset_id);
        if (dependencies.empty()) ImGui::TextDisabled("No tracked dependencies.");
        for (const auto* dependency : dependencies) {
            if (dependency->resolved) {
                const auto* target = state.asset_catalog.find_by_id(dependency->target_asset_id);
                const char* mode = dependency->resolved_by_id ? "asset ID" : "path";
                ImGui::BulletText("%s -> %s  [%s]", dependency->reason.c_str(),
                    target ? target->relative_path.generic_string().c_str() : dependency->reference.c_str(), mode);
                if (dependency->stale_fallback_path) {
                    ImGui::Indent();
                    ImGui::TextColored(ImVec4(1.0f, 0.68f, 0.25f, 1.0f), "Saved path outdated: %s", dependency->reference.c_str());
                    ImGui::Unindent();
                }
            } else {
                const std::string missing = dependency->reference.empty() ? dependency->requested_asset_id : dependency->reference;
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.36f, 1.0f), "Missing: %s (%s)", missing.c_str(), dependency->reason.c_str());
            }
        }
        const auto dependents = state.asset_catalog.dependents_of(record->asset_id);
        if (!dependents.empty()) {
            ImGui::TextDisabled("Used by %zu asset(s)", dependents.size());
            for (const auto* dependency : dependents) {
                const auto* source = state.asset_catalog.find_by_id(dependency->source_asset_id);
                if (source) ImGui::BulletText("%s", source->relative_path.generic_string().c_str());
            }
        }

        ImGui::SeparatorText("Asset ID");
        ImGui::TextWrapped("%s", record->asset_id.c_str());
        if (ImGui::TreeNodeEx("Technical Details", ImGuiTreeNodeFlags_None)) {
            ImGui::TextDisabled("Content hash: %s", record->source_hash.c_str());
            ImGui::TreePop();
        }

        if (ImGui::Button("Copy Asset ID")) ImGui::SetClipboardText(record->asset_id.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Copy Path")) {
            const auto path_text = record->relative_path.generic_string();
            ImGui::SetClipboardText(path_text.c_str());
        }

        ImGui::SameLine();
        if (ImGui::Button("Move / Rename...")) request_asset_move(state, *record);

        if (record->kind == vespera::AssetKind::Scene) {
            ImGui::Separator();
            if (ImGui::Button("Open Scene", ImVec2(-1.0f, 0.0f))) {
                if (state.dirty) {
                    state.pending_action = PendingAction::OpenScene;
                    state.pending_open_path = record->absolute_path;
                    state.request_unsaved_popup = true;
                } else {
                    open_scene(state, record->absolute_path);
                }
            }
        } else if (record->kind == vespera::AssetKind::EntityPrefab) {
            ImGui::Separator();
            if (ImGui::Button("Instantiate Prefab", ImVec2(-1.0f, 0.0f))) {
                command_instantiate_prefab(state, record->absolute_path);
            }
        }
        if (state.project_loaded) {
            ImGui::SeparatorText("Build inclusion");
            const auto relative = record->relative_path.lexically_normal();
            std::optional<std::size_t> included_index;
            for (std::size_t i = 0; i < state.project.build_includes.size(); ++i) {
                const bool id_match = i < state.project.build_include_asset_ids.size()
                    && !state.project.build_include_asset_ids[i].empty()
                    && state.project.build_include_asset_ids[i] == record->asset_id;
                const bool path_match = state.project.build_includes[i].lexically_normal() == relative;
                if (id_match || path_match) { included_index = i; break; }
            }
            if (!included_index) {
                if (ImGui::Button("Include in Standalone Build", ImVec2(-1.0f, 0.0f))) {
                    state.project.build_includes.push_back(relative);
                    state.project.build_include_asset_ids.push_back(record->asset_id);
                    push_console(state, ConsoleEntry::Level::Info, "Build include added: " + relative.generic_string() + " (Save Project to persist)");
                }
            } else {
                if (ImGui::Button("Remove Explicit Build Include", ImVec2(-1.0f, 0.0f))) {
                    const auto index = *included_index;
                    state.project.build_includes.erase(state.project.build_includes.begin() + static_cast<std::ptrdiff_t>(index));
                    if (index < state.project.build_include_asset_ids.size()) {
                        state.project.build_include_asset_ids.erase(state.project.build_include_asset_ids.begin() + static_cast<std::ptrdiff_t>(index));
                    }
                    push_console(state, ConsoleEntry::Level::Info, "Build include removed: " + relative.generic_string() + " (Save Project to persist)");
                }
            }
            ImGui::TextDisabled("Startup-scene dependencies are included automatically.");
            if (record->kind == vespera::AssetKind::Texture) {
                const bool current_icon = state.project.game_icon_asset_id == record->asset_id;
                if (!current_icon && ImGui::Button("Use as Project Game Icon", ImVec2(-1.0f, 0.0f))) {
                    state.project.game_icon = relative;
                    state.project.game_icon_asset_id = record->asset_id;
                    push_console(state, ConsoleEntry::Level::Info, "Project game icon set: " + relative.generic_string() + " (Save Project to persist)");
                } else if (current_icon) {
                    ImGui::TextDisabled("This texture is the current project game icon.");
                }
            }
        }
        if (ImGui::Button("Refresh / Reimport", ImVec2(-1.0f, 0.0f))) {
            refresh_asset_catalog(state, true, true);
        }

        ImGui::End();
        return;
    }

    if (state.selection.kind == SelectionKind::None) {
        ImGui::Spacing();
        ImGui::TextDisabled("No selection");
        ImGui::TextWrapped("Select an entity, sector, camera, material, sprite clip, or project asset to inspect and edit its properties here.");
        ImGui::End();
        return;
    }

    if (state.selection.kind == SelectionKind::Camera) {
        ImGui::TextUnformatted("Camera");
        ImGui::Separator();
        auto& camera = state.scene.camera;

        float pos[3]{camera.position.x, camera.position.y, camera.position.z};
        auto before = capture_snapshot(state);
        const ImGuiID position_id = ImGui::GetID("Position");
        const bool changed_position = ImGui::DragFloat3("Position", pos, 0.05f);
        if (changed_position) camera.position = {pos[0], pos[1], pos[2]};
        track_item_edit(state, std::move(before), "Edit camera position", position_id, changed_position);

        float yaw = camera.yaw * kEditorRadiansToDegrees;
        before = capture_snapshot(state);
        const ImGuiID yaw_id = ImGui::GetID("Yaw");
        const bool changed_yaw = ImGui::DragFloat("Yaw", &yaw, 0.5f, -360.0f, 360.0f, "%.1f deg");
        if (changed_yaw) camera.yaw = yaw * kEditorDegreesToRadians;
        track_item_edit(state, std::move(before), "Edit camera yaw", yaw_id, changed_yaw);

        float pitch = camera.pitch * kEditorRadiansToDegrees;
        before = capture_snapshot(state);
        const ImGuiID pitch_id = ImGui::GetID("Pitch");
        const bool changed_pitch = ImGui::DragFloat("Pitch", &pitch, 0.5f, -89.0f, 89.0f, "%.1f deg");
        if (changed_pitch) camera.pitch = std::clamp(pitch, -89.0f, 89.0f) * kEditorDegreesToRadians;
        track_item_edit(state, std::move(before), "Edit camera pitch", pitch_id, changed_pitch);

        before = capture_snapshot(state);
        const ImGuiID fov_id = ImGui::GetID("Vertical FOV");
        const bool changed_fov = ImGui::DragFloat("Vertical FOV", &camera.vertical_fov_degrees, 0.25f, 30.0f, 130.0f, "%.1f deg");
        track_item_edit(state, std::move(before), "Edit camera FOV", fov_id, changed_fov);
        ImGui::End();
        return;
    }

    if (state.selection.kind == SelectionKind::Sector) {
        if (state.selection.index >= state.scene.world.sectors().size()) {
            state.selection = {};
            ImGui::End();
            return;
        }

        const std::size_t sector_index = state.selection.index;
        vespera::Sector edited = state.scene.world.sectors()[sector_index];
        draw_panel_heading("Sector", std::format("#{}", sector_index));
        ImGui::Spacing();

        bool invalid_name = false;
        if (ImGui::BeginTable("SectorProperties", 2,
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_NoPadOuterX)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 112.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("Name");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            std::string proposed_name = edited.name;
            auto before = capture_snapshot(state);
            const ImGuiID sector_name_id = ImGui::GetID("##SectorName");
            const bool requested_name_change = ImGui::InputText("##SectorName", &proposed_name);
            bool applied_name_change = false;
            if (requested_name_change && !proposed_name.empty()
                && !sector_name_exists(state.scene, proposed_name, sector_index)) {
                edited.name = proposed_name;
                applied_name_change = state.scene.world.set_sector(sector_index, edited);
            }
            track_item_edit(state, std::move(before), "Rename sector", sector_name_id, applied_name_change);
            invalid_name = requested_name_change && !applied_name_change;

            edited = state.scene.world.sectors()[sector_index];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("Floor height");
            ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-FLT_MIN);
            before = capture_snapshot(state);
            const ImGuiID floor_height_id = ImGui::GetID("##SectorFloorHeight");
            bool changed = ImGui::DragFloat("##SectorFloorHeight", &edited.floor_height, 0.05f);
            if (changed) {
                if (edited.ceiling_height < edited.floor_height + 0.05f) edited.ceiling_height = edited.floor_height + 0.05f;
                state.scene.world.set_sector(sector_index, edited);
            }
            track_item_edit(state, std::move(before), "Edit sector floor height", floor_height_id, changed);

            edited = state.scene.world.sectors()[sector_index];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("Ceiling height");
            ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-FLT_MIN);
            before = capture_snapshot(state);
            const ImGuiID ceiling_height_id = ImGui::GetID("##SectorCeilingHeight");
            changed = ImGui::DragFloat("##SectorCeilingHeight", &edited.ceiling_height, 0.05f);
            if (changed) {
                if (edited.ceiling_height < edited.floor_height + 0.05f) edited.ceiling_height = edited.floor_height + 0.05f;
                state.scene.world.set_sector(sector_index, edited);
            }
            track_item_edit(state, std::move(before), "Edit sector ceiling height", ceiling_height_id, changed);

            edited = state.scene.world.sectors()[sector_index];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("Floor material");
            ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-FLT_MIN);
            before = capture_snapshot(state);
            if (material_combo("##SectorFloorMaterial", edited.floor_material, state.scene.world)) {
                state.scene.world.set_sector(sector_index, edited);
                record_immediate_edit(state, std::move(before), "Change floor material");
            }

            edited = state.scene.world.sectors()[sector_index];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("Ceiling material");
            ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-FLT_MIN);
            before = capture_snapshot(state);
            if (material_combo("##SectorCeilingMaterial", edited.ceiling_material, state.scene.world)) {
                state.scene.world.set_sector(sector_index, edited);
                record_immediate_edit(state, std::move(before), "Change ceiling material");
            }

            edited = state.scene.world.sectors()[sector_index];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("Wall material");
            ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-FLT_MIN);
            before = capture_snapshot(state);
            if (material_combo("##SectorWallMaterial", edited.wall_material, state.scene.world)) {
                state.scene.world.set_sector(sector_index, edited);
                record_immediate_edit(state, std::move(before), "Change wall material");
            }

            ImGui::EndTable();
        }
        if (invalid_name) {
            ImGui::TextColored(editor_ui_palette().danger, "Sector names must be non-empty and unique.");
        }

        const auto& sector = state.scene.world.sectors()[sector_index];
        ImGui::SeparatorText("Geometry");
        ImGui::TextWrapped("Drag vertex handles in Sector. Click a wall edge to edit its material or portal target. Coincident vertices in neighboring sectors stay welded.");
        if (state.selection.sub_index != kNoSubSelection && state.selection.sub_index < sector.vertices.size()) {
            const auto& vertex = sector.vertices[state.selection.sub_index];
            ImGui::Text("Selected vertex %zu: X %.3f  Z %.3f", state.selection.sub_index, vertex.x, vertex.z);
        }
        if (ImGui::BeginTable("vertices", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 28.0f);
            ImGui::TableSetupColumn("X");
            ImGui::TableSetupColumn("Z");
            ImGui::TableHeadersRow();
            for (std::size_t i = 0; i < sector.vertices.size(); ++i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const std::string row_label = std::to_string(i) + "##vertexrow" + std::to_string(i);
                if (ImGui::Selectable(row_label.c_str(), state.selection.sub_index == i, ImGuiSelectableFlags_SpanAllColumns)) {
                    state.selection.sub_index = i;
                    state.selection.side_index = kNoSubSelection;
                }
                ImGui::TableSetColumnIndex(1); ImGui::Text("%.3f", sector.vertices[i].x);
                ImGui::TableSetColumnIndex(2); ImGui::Text("%.3f", sector.vertices[i].z);
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Sides / Portals");
        if (ImGui::BeginTable("sector_sides", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Side", ImGuiTableColumnFlags_WidthFixed, 42.0f);
            ImGui::TableSetupColumn("Material");
            ImGui::TableSetupColumn("Target");
            ImGui::TableHeadersRow();
            for (std::size_t side_i = 0; side_i < sector.sides.size(); ++side_i) {
                const auto& side = sector.sides[side_i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const std::string side_label = std::to_string(side_i) + "##siderow" + std::to_string(side_i);
                if (ImGui::Selectable(side_label.c_str(), state.selection.side_index == side_i, ImGuiSelectableFlags_SpanAllColumns)) {
                    state.selection.side_index = side_i;
                    state.selection.sub_index = kNoSubSelection;
                }
                ImGui::TableSetColumnIndex(1);
                if (side.material != vespera::kInvalidMaterial && side.material < state.scene.world.materials().size()) {
                    ImGui::TextUnformatted(state.scene.world.materials()[side.material].name.c_str());
                } else {
                    ImGui::TextDisabled("<default>");
                }
                ImGui::TableSetColumnIndex(2);
                if (side.adjacent_sector >= 0 && side.adjacent_sector < static_cast<int>(state.scene.world.sectors().size())) {
                    ImGui::TextUnformatted(state.scene.world.sectors()[static_cast<std::size_t>(side.adjacent_sector)].name.c_str());
                } else {
                    ImGui::TextDisabled("Solid");
                }
            }
            ImGui::EndTable();
        }

        if (state.selection.side_index < state.scene.world.sectors()[sector_index].sides.size()) {
            const std::size_t side_index = state.selection.side_index;
            const auto& current_sector = state.scene.world.sectors()[sector_index];
            const auto a = current_sector.vertices[side_index];
            const auto b = current_sector.vertices[(side_index + 1u) % current_sector.vertices.size()];
            ImGui::Text("Selected side %zu: (%.2f, %.2f) -> (%.2f, %.2f)", side_index, a.x, a.z, b.x, b.z);

            edited = current_sector;
            auto before = capture_snapshot(state);
            if (material_combo("Side Material", edited.sides[side_index].material, state.scene.world)) {
                state.scene.world.set_sector(sector_index, edited);
                record_immediate_edit(state, std::move(before), "Change sector side material");
            }

            const auto& portal_sector = state.scene.world.sectors()[sector_index];
            const int current_target = portal_sector.sides[side_index].adjacent_sector;
            const char* portal_preview = "<solid wall>";
            std::string portal_preview_storage;
            if (current_target >= 0 && current_target < static_cast<int>(state.scene.world.sectors().size())) {
                portal_preview_storage = state.scene.world.sectors()[static_cast<std::size_t>(current_target)].name;
                portal_preview = portal_preview_storage.c_str();
            }
            int requested_target = current_target;
            bool requested_target_change = false;
            if (ImGui::BeginCombo("Portal Target", portal_preview)) {
                if (ImGui::Selectable("<solid wall>", current_target < 0)) {
                    requested_target = -1;
                    requested_target_change = requested_target != current_target;
                }
                for (std::size_t target_i = 0; target_i < state.scene.world.sectors().size(); ++target_i) {
                    if (target_i == sector_index) continue;
                    const bool selected_target = current_target == static_cast<int>(target_i);
                    if (ImGui::Selectable(state.scene.world.sectors()[target_i].name.c_str(), selected_target)) {
                        requested_target = static_cast<int>(target_i);
                        requested_target_change = requested_target != current_target;
                    }
                }
                ImGui::EndCombo();
            }
            if (requested_target_change) {
                command_set_sector_portal_target(state, sector_index, side_index, requested_target);
            }

            const auto& after_sector = state.scene.world.sectors()[sector_index];
            const int after_target = after_sector.sides[side_index].adjacent_sector;
            if (after_target >= 0 && after_target < static_cast<int>(state.scene.world.sectors().size())) {
                const auto matching = vespera::find_matching_sector_side(
                    state.scene.world, sector_index, side_index, static_cast<std::size_t>(after_target));
                if (!matching) {
                    ImGui::TextColored(ImVec4(1.0f, 0.62f, 0.25f, 1.0f), "No matching shared edge in target sector.");
                } else {
                    const auto& target = state.scene.world.sectors()[static_cast<std::size_t>(after_target)];
                    const bool reciprocal = *matching < target.sides.size()
                        && target.sides[*matching].adjacent_sector == static_cast<int>(sector_index);
                    if (reciprocal) {
                        ImGui::TextDisabled("Reciprocal portal linked to target side %zu.", *matching);
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.62f, 0.25f, 1.0f), "Matching edge exists, but target is not reciprocal.");
                    }
                }
            }
        }

        int portal_count = 0;
        for (const auto& side : state.scene.world.sectors()[sector_index].sides) {
            if (side.adjacent_sector >= 0) ++portal_count;
        }
        ImGui::Text("Portal edges: %d", portal_count);

        ImGui::Separator();
        if (ImGui::Button("Duplicate", ImVec2(110.0f, 0.0f))) {
            command_duplicate_selection(state);
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete", ImVec2(110.0f, 0.0f))) {
            command_delete_selection(state);
        }
        ImGui::End();
        return;
    }

    if (state.selection.kind == SelectionKind::Material) {
        if (state.selection.index >= state.scene.world.materials().size()) {
            state.selection = {};
            ImGui::End();
            return;
        }

        const std::size_t material_index = state.selection.index;
        vespera::WorldMaterial material = state.scene.world.materials()[material_index];
        ImGui::Text("Material %zu", material_index);
        ImGui::Separator();

        std::string proposed_name = material.name;
        auto before = capture_snapshot(state);
        const ImGuiID material_name_id = ImGui::GetID("Name");
        const bool requested_name_change = ImGui::InputText("Name", &proposed_name);
        bool applied_name_change = false;
        if (requested_name_change && !proposed_name.empty()
            && !material_name_exists(state.scene, proposed_name, material_index)) {
            material.name = proposed_name;
            applied_name_change = state.scene.world.set_material(material_index, material);
        }
        track_item_edit(state, std::move(before), "Rename material", material_name_id, applied_name_change);
        if (requested_name_change && !applied_name_change) {
            ImGui::TextColored(ImVec4(1.0f, 0.48f, 0.32f, 1.0f), "Material names must be non-empty and unique.");
        }

        material = state.scene.world.materials()[material_index];
        before = capture_snapshot(state);
        const ImGuiID material_color_id = ImGui::GetID("Tint");
        const bool changed_color = ImGui::ColorEdit4("Tint", material.color.data());
        if (changed_color) state.scene.world.set_material(material_index, material);
        track_item_edit(state, std::move(before), "Edit material tint", material_color_id, changed_color);

        material = state.scene.world.materials()[material_index];
        before = capture_snapshot(state);
        if (texture_combo("Texture", material.texture, state.scene.world)) {
            state.scene.world.set_material(material_index, material);
            record_immediate_edit(state, std::move(before), "Change material texture");
        }

        material = state.scene.world.materials()[material_index];
        float uv[2]{material.uv_scale.x, material.uv_scale.z};
        before = capture_snapshot(state);
        const ImGuiID uv_id = ImGui::GetID("UV Scale");
        const bool changed_uv = ImGui::DragFloat2("UV Scale", uv, 0.025f, -16.0f, 16.0f, "%.3f");
        if (changed_uv) {
            material.uv_scale = {uv[0], uv[1]};
            state.scene.world.set_material(material_index, material);
        }
        track_item_edit(state, std::move(before), "Edit material UV scale", uv_id, changed_uv);

        ImGui::Separator();
        ImGui::TextWrapped("Deleting a material clears direct references to it and remaps higher material ids safely.");
        if (ImGui::Button("Duplicate", ImVec2(110.0f, 0.0f))) {
            command_duplicate_selection(state);
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete", ImVec2(110.0f, 0.0f))) {
            command_delete_selection(state);
        }
        ImGui::End();
        return;
    }

    if (state.selection.kind == SelectionKind::SpriteClip) {
        if (state.selection.index >= state.scene.sprite_clips.size()) {
            state.selection = {};
            ImGui::End();
            return;
        }

        auto& clip = state.scene.sprite_clips[state.selection.index];
        ImGui::Text("Sprite Clip %zu", state.selection.index);
        ImGui::Separator();

        std::string proposed_name = clip.name;
        auto before = capture_snapshot(state);
        const ImGuiID clip_name_id = ImGui::GetID("Name");
        const bool requested_name_change = ImGui::InputText("Name", &proposed_name);
        bool applied_name_change = false;
        if (requested_name_change && !proposed_name.empty()
            && !clip_name_exists(state.scene, proposed_name, state.selection.index)) {
            const std::string old_name = clip.name;
            clip.name = proposed_name;
            for (auto& entity : state.scene.entities) {
                if (entity.sprite_renderer && entity.sprite_renderer->animation_clip == old_name) {
                    entity.sprite_renderer->animation_clip = clip.name;
                }
            }
            applied_name_change = true;
        }
        track_item_edit(state, std::move(before), "Rename sprite clip", clip_name_id, applied_name_change);
        if (requested_name_change && !applied_name_change) {
            ImGui::TextColored(ImVec4(1.0f, 0.48f, 0.32f, 1.0f), "Clip names must be non-empty and unique.");
        }

        int direction_choice = clip.direction_count == 8u ? 2 : (clip.direction_count == 4u ? 1 : 0);
        static const char* direction_labels[] = {"1 direction", "4 directions", "8 directions"};
        before = capture_snapshot(state);
        if (ImGui::Combo("Directions", &direction_choice, direction_labels, 3)) {
            const std::uint32_t directions = direction_choice == 2 ? 8u : (direction_choice == 1 ? 4u : 1u);
            resize_clip_frames(clip, directions, clip.frame_count, default_sprite_texture(state.scene));
            record_immediate_edit(state, std::move(before), "Change sprite clip direction count");
        }

        int frame_count = static_cast<int>(clip.frame_count);
        before = capture_snapshot(state);
        const ImGuiID frame_count_id = ImGui::GetID("Frames");
        const bool changed_frames = ImGui::DragInt("Frames", &frame_count, 0.1f, 1, 16);
        if (changed_frames) {
            frame_count = std::clamp(frame_count, 1, 16);
            resize_clip_frames(clip, clip.direction_count, static_cast<std::uint32_t>(frame_count), default_sprite_texture(state.scene));
        }
        track_item_edit(state, std::move(before), "Change sprite clip frame count", frame_count_id, changed_frames);

        before = capture_snapshot(state);
        const ImGuiID fps_id = ImGui::GetID("Frames Per Second");
        const bool changed_fps = ImGui::DragFloat("Frames Per Second", &clip.frames_per_second, 0.1f, 0.0f, 120.0f, "%.2f");
        if (changed_fps) clip.frames_per_second = std::max(clip.frames_per_second, 0.0f);
        track_item_edit(state, std::move(before), "Change sprite clip FPS", fps_id, changed_fps);

        before = capture_snapshot(state);
        const bool changed_loop = ImGui::Checkbox("Loop", &clip.loop);
        if (changed_loop) {
            record_immediate_edit(state, std::move(before), "Toggle sprite clip looping");
        }

        ImGui::SeparatorText("Frames");
        ImGui::TextWrapped("Direction 0 is the actor front. Higher directions rotate clockwise around the actor.");
        for (std::uint32_t direction = 0; direction < clip.direction_count; ++direction) {
            ImGui::PushID(static_cast<int>(direction));
            const std::string header = std::format("Direction {}", direction);
            if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                for (std::uint32_t frame = 0; frame < clip.frame_count; ++frame) {
                    const std::size_t texture_index = static_cast<std::size_t>(direction) * clip.frame_count + frame;
                    if (texture_index >= clip.textures.size()) continue;
                    ImGui::PushID(static_cast<int>(frame));
                    const std::string label = std::format("Frame {}", frame);
                    before = capture_snapshot(state);
                    if (texture_combo(label.c_str(), clip.textures[texture_index], state.scene.world)) {
                        record_immediate_edit(state, std::move(before), "Change sprite clip frame texture");
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }

        ImGui::Separator();
        if (ImGui::Button("Duplicate", ImVec2(110.0f, 0.0f))) {
            command_duplicate_selection(state);
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete", ImVec2(110.0f, 0.0f))) {
            command_delete_selection(state);
        }
        ImGui::End();
        return;
    }

    if (state.selection.kind == SelectionKind::Entity) {
        const auto resolved_entity_index = selected_entity_index(state);
        if (!resolved_entity_index) {
            state.selection = {};
            state.selected_entity_ids.clear();
            ImGui::End();
            return;
        }
        state.selection.index = *resolved_entity_index;
        auto& entity = state.scene.entities[*resolved_entity_index];
        const auto& palette = editor_ui_palette();
        draw_panel_heading(entity.name, std::format("Entity  |  ID {}", static_cast<unsigned long long>(entity.id)));
        if (state.selected_entity_ids.size() > 1) {
            ImGui::TextColored(palette.warning, "%zu entities selected", state.selected_entity_ids.size());
            ImGui::TextDisabled("Inspector edits the primary entity; Scene gizmos transform the group.");
        }
        ImGui::Separator();

        auto before = capture_snapshot(state);
        const bool changed_enabled = ImGui::Checkbox("Enabled", &entity.enabled);
        if (changed_enabled) record_immediate_edit(state, std::move(before), "Toggle entity enabled");

        if (ImGui::BeginTable("##EntityIdentity", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_NoPadOuterX)) {
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 58.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.0f);

            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("Name");
            ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-FLT_MIN);
            before = capture_snapshot(state);
            const ImGuiID entity_name_id = ImGui::GetID("##EntityName");
            const bool changed_name = ImGui::InputText("##EntityName", &entity.name);
            track_item_edit(state, std::move(before), "Rename entity", entity_name_id, changed_name);

            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("Tag");
            ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-FLT_MIN);
            before = capture_snapshot(state);
            const ImGuiID entity_tag_id = ImGui::GetID("##EntityTag");
            const bool changed_tag = ImGui::InputText("##EntityTag", &entity.tag);
            track_item_edit(state, std::move(before), "Edit entity tag", entity_tag_id, changed_tag);

            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("Layer");
            ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-FLT_MIN);
            before = capture_snapshot(state);
            const ImGuiID entity_layer_id = ImGui::GetID("##EntityLayer");
            const bool changed_layer = ImGui::InputText("##EntityLayer", &entity.layer);
            track_item_edit(state, std::move(before), "Edit entity layer", entity_layer_id, changed_layer);

            ImGui::EndTable();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("Tag: semantic gameplay query\nLayer: broad system grouping");
        }

        ImGui::SeparatorText("Hierarchy");
        if (entity.parent_id == vespera::kInvalidSceneObjectId) {
            ImGui::TextDisabled("Parent: <root>");
        } else if (const auto* parent = state.scene.find_entity(entity.parent_id)) {
            ImGui::Text("Parent: %s", parent->name.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Unparent")) command_reparent_entity(state, entity.id, vespera::kInvalidSceneObjectId);
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.38f, 1.0f), "Parent: missing ID %llu", static_cast<unsigned long long>(entity.parent_id));
        }
        ImGui::TextDisabled("Drag entities in Hierarchy to parent/reparent while preserving world position.");

        ImGui::SeparatorText("Prefab");
        if (entity.prefab_source.empty()) {
            ImGui::TextDisabled("Unpacked entity");
            if (ImGui::Button("Create Prefab from Entity")) {
                command_create_prefab_from_selected(state);
            }
        } else {
            ImGui::TextWrapped("Source: %s", entity.prefab_source.path.generic_string().c_str());
            if (!entity.prefab_source.asset_id.empty()) ImGui::TextDisabled("Asset ID: %s", entity.prefab_source.asset_id.c_str());
            ImGui::TextColored(ImVec4(0.52f, 0.78f, 0.66f, 1.0f), "Linked prefab instance");
            if (ImGui::Button("Select Source", ImVec2(110.0f, 0.0f))) {
                const auto resolved = state.asset_catalog.resolve_reference(entity.prefab_source);
                if (resolved && resolved.record) {
                    state.selection = {SelectionKind::Asset};
                    state.selection.asset_id = resolved.record->asset_id;
                    state.selected_entity_ids.clear();
                    state.asset_browser_folder = resolved.record->relative_path.parent_path();
                } else {
                    push_console(state, ConsoleEntry::Level::Warning, "Prefab source could not be resolved in the current asset catalog.");
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Apply to Prefab", ImVec2(120.0f, 0.0f))) {
                command_apply_selected_to_prefab(state);
            }
            ImGui::SameLine();
            if (ImGui::Button("Revert", ImVec2(90.0f, 0.0f))) {
                command_revert_selected_from_prefab(state);
            }
            ImGui::SameLine();
            if (ImGui::Button("Unpack", ImVec2(90.0f, 0.0f))) {
                command_unpack_selected_prefab(state);
            }
            ImGui::TextDisabled("Apply updates the prefab from this entity; Revert restores the prefab values.");
        }

        ImGui::SeparatorText(entity.parent_id == vespera::kInvalidSceneObjectId ? "Transform" : "Local Transform");
        ImGui::TextDisabled("%s  |  %s space  |  snap %s",
            scene_tool_name(state.scene_view_3d.tool),
            state.scene_view_3d.local_space ? "Local" : "Global",
            state.scene_view_3d.snap_enabled ? "on" : "off");
        auto& transform = entity.transform;

        float pos[3]{transform.position.x, transform.position.y, transform.position.z};
        float rotation[3]{
            transform.rotation.x * kEditorRadiansToDegrees,
            transform.rotation.y * kEditorRadiansToDegrees,
            transform.rotation.z * kEditorRadiansToDegrees
        };
        float scale[3]{transform.scale.x, transform.scale.y, transform.scale.z};

        if (ImGui::BeginTable("##TransformProperties", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_NoPadOuterX)) {
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 58.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.0f);

            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("Position");
            ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-FLT_MIN);
            before = capture_snapshot(state);
            const ImGuiID position_id = ImGui::GetID("##EntityPosition");
            const bool changed_position = ImGui::DragFloat3("##EntityPosition", pos, 0.05f);
            if (changed_position) transform.position = {pos[0], pos[1], pos[2]};
            track_item_edit(state, std::move(before), "Edit entity position", position_id, changed_position);

            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("Rotation");
            ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-FLT_MIN);
            before = capture_snapshot(state);
            const ImGuiID rotation_id = ImGui::GetID("##EntityRotation");
            const bool changed_rotation = ImGui::DragFloat3("##EntityRotation", rotation, 1.0f, -3600.0f, 3600.0f, "%.1f deg");
            if (changed_rotation) {
                transform.rotation = {
                    rotation[0] * kEditorDegreesToRadians,
                    rotation[1] * kEditorDegreesToRadians,
                    rotation[2] * kEditorDegreesToRadians
                };
            }
            track_item_edit(state, std::move(before), "Edit entity rotation", rotation_id, changed_rotation);

            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("Scale");
            ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-FLT_MIN);
            before = capture_snapshot(state);
            const ImGuiID scale_id = ImGui::GetID("##EntityScale");
            const bool changed_scale = ImGui::DragFloat3("##EntityScale", scale, 0.025f, 0.01f, 100.0f, "%.3f");
            if (changed_scale) {
                transform.scale = {
                    std::max(scale[0], 0.01f),
                    std::max(scale[1], 0.01f),
                    std::max(scale[2], 0.01f)
                };
            }
            track_item_edit(state, std::move(before), "Edit entity scale", scale_id, changed_scale);

            ImGui::EndTable();
        }

        if (entity.parent_id != vespera::kInvalidSceneObjectId) {
            const auto world = editor_world_transform(state, entity);
            ImGui::TextDisabled("World: P %.2f %.2f %.2f | R %.1f %.1f %.1f deg | S %.2f %.2f %.2f",
                world.position.x, world.position.y, world.position.z,
                world.rotation.x * kEditorRadiansToDegrees, world.rotation.y * kEditorRadiansToDegrees, world.rotation.z * kEditorRadiansToDegrees,
                world.scale.x, world.scale.y, world.scale.z);
        }
        ImGui::TextDisabled("Transform is the required component on every entity.");
        ImGui::SeparatorText("Components");

        if (ImGui::Button("Add Component...")) {
            ImGui::OpenPopup("add_component_popup");
        }
        if (ImGui::BeginPopup("add_component_popup")) {
            for (const auto& info : vespera::kBuiltinComponentTypes) {
                if (!info.removable || entity.has_component(info.type)) continue;
                if (ImGui::MenuItem(info.display_name.data())) {
                    before = capture_snapshot(state);
                    if (entity.add_component(info.type)) {
                        if (info.type == vespera::BuiltinComponentType::SpriteRenderer) {
                            auto& sprite = *entity.sprite_renderer;
                            sprite.texture = default_sprite_texture(state.scene);
                            sprite.size = {1.0f, 1.5f};
                        }
                        record_immediate_edit(state, std::move(before),
                            std::string("Add ") + std::string(info.display_name) + " component");
                    }
                }
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("C# Script")) {
                if (state.managed_metadata.loaded && !state.managed_metadata.scripts.empty()) {
                    for (const auto& meta : state.managed_metadata.scripts) {
                        if (ImGui::MenuItem(meta.class_name.c_str())) {
                            before = capture_snapshot(state);
                            entity.add_managed_script(meta.class_name);
                            record_immediate_edit(state, std::move(before), "Add C# Script");
                        }
                    }
                } else if (ImGui::MenuItem("Manual / unresolved type")) {
                    before = capture_snapshot(state); entity.add_managed_script();
                    record_immediate_edit(state, std::move(before), "Add C# Script");
                }
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }

        bool optional_component_present = entity.sprite_renderer.has_value() || entity.mesh_renderer.has_value() || entity.cylinder_collider.has_value() || entity.point_light.has_value() || !entity.managed_scripts.empty();
        if (!optional_component_present) {
            ImGui::TextDisabled("No optional components attached.");
        }

        if (entity.sprite_renderer) {
            auto& sprite = *entity.sprite_renderer;
            if (ImGui::TreeNodeEx("Sprite Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextDisabled("Directional facing uses Transform rotation Y.");

                float size[2]{sprite.size.x, sprite.size.z};
                before = capture_snapshot(state);
                const ImGuiID sprite_size_id = ImGui::GetID("Size");
                const bool changed_size = ImGui::DragFloat2("Size", size, 0.025f, 0.05f, 100.0f);
                if (changed_size) sprite.size = {std::max(size[0], 0.05f), std::max(size[1], 0.05f)};
                track_item_edit(state, std::move(before), "Edit Sprite Renderer size", sprite_size_id, changed_size);

                before = capture_snapshot(state);
                bool changed_sprite_texture = texture_combo("Fallback Texture", sprite.texture, state.scene.world);
                changed_sprite_texture = accept_texture_asset_drop(state, sprite.texture, "Sprite Renderer") || changed_sprite_texture;
                if (changed_sprite_texture) {
                    record_immediate_edit(state, std::move(before), "Change Sprite Renderer texture");
                }

                before = capture_snapshot(state);
                if (sprite_clip_combo("Clip", sprite.animation_clip, state.scene)) {
                    record_immediate_edit(state, std::move(before), "Change Sprite Renderer animation clip");
                }

                before = capture_snapshot(state);
                const ImGuiID speed_id = ImGui::GetID("Animation Speed");
                const bool changed_speed = ImGui::DragFloat("Animation Speed", &sprite.animation_speed, 0.025f, 0.0f, 10.0f, "%.2fx");
                if (changed_speed) sprite.animation_speed = std::max(sprite.animation_speed, 0.0f);
                track_item_edit(state, std::move(before), "Edit Sprite Renderer animation speed", speed_id, changed_speed);

                before = capture_snapshot(state);
                const ImGuiID offset_id = ImGui::GetID("Time Offset");
                const bool changed_offset = ImGui::DragFloat("Time Offset", &sprite.animation_time_offset, 0.01f, -60.0f, 60.0f, "%.2f s");
                track_item_edit(state, std::move(before), "Edit Sprite Renderer animation time offset", offset_id, changed_offset);

                before = capture_snapshot(state);
                const bool changed_paused = ImGui::Checkbox("Pause Animation", &sprite.animation_paused);
                if (changed_paused) record_immediate_edit(state, std::move(before), "Toggle Sprite Renderer animation pause");

                if (!sprite.animation_clip.empty()) {
                    if (const auto* clip = state.scene.find_sprite_clip(sprite.animation_clip)) {
                        ImGui::TextDisabled("%u directions  |  %u frames  |  %.2f fps  |  %s",
                            clip->direction_count, clip->frame_count, clip->frames_per_second, clip->loop ? "loop" : "once");
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "Missing clip: %s", sprite.animation_clip.c_str());
                    }
                } else {
                    ImGui::TextDisabled("Static sprite uses the selected texture directly.");
                }

                before = capture_snapshot(state);
                const ImGuiID tint_id = ImGui::GetID("Tint");
                const bool changed_tint = ImGui::ColorEdit4("Tint", sprite.color.data());
                track_item_edit(state, std::move(before), "Edit Sprite Renderer tint", tint_id, changed_tint);

                if (ImGui::Button("Remove Sprite Renderer")) {
                    before = capture_snapshot(state);
                    entity.remove_component(vespera::BuiltinComponentType::SpriteRenderer);
                    record_immediate_edit(state, std::move(before), "Remove Sprite Renderer component");
                }
                ImGui::TreePop();
            }
        }

        if (entity.mesh_renderer) {
            auto& mesh = *entity.mesh_renderer;
            if (ImGui::TreeNodeEx("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextDisabled("Built-in blockout primitive. Transform Scale controls dimensions.");
                const char* primitives[] = {"Cube", "Plane", "Cylinder", "Sphere"};
                int primitive = static_cast<int>(mesh.primitive);
                before = capture_snapshot(state);
                if (ImGui::Combo("Primitive", &primitive, primitives, 4)) {
                    mesh.primitive = static_cast<vespera::PrimitiveMeshType>(std::clamp(primitive,0,3));
                    record_immediate_edit(state, std::move(before), "Change Mesh Renderer primitive");
                }
                before = capture_snapshot(state);
                const auto material_resolution = state.asset_catalog.resolve_reference(mesh.material);
                std::string material_label = "None";
                if (material_resolution && material_resolution.record->kind == vespera::AssetKind::Material)
                    material_label = material_resolution.record->display_name;
                else if (!mesh.material.path.empty()) material_label = mesh.material.path.filename().string();
                ImGui::TextUnformatted("Material");
                ImGui::SameLine(92.0f);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Button((material_label + "##MeshMaterial").c_str(), ImVec2(-28.0f, 0.0f))) {
                    if (material_resolution && material_resolution.record->kind == vespera::AssetKind::Material) {
                        state.selection = {SelectionKind::Asset, 0, kNoSubSelection, vespera::kInvalidSceneObjectId, kNoSubSelection, material_resolution.record->asset_id};
                    }
                }
                const bool dropped_material = accept_material_asset_drop(state, mesh);
                ImGui::SameLine();
                if (ImGui::SmallButton("X##ClearMeshMaterial")) {
                    mesh.material = {};
                    mesh.material_resolved = false;
                    mesh.resolved_material_texture = vespera::kInvalidTexture;
                    record_immediate_edit(state, std::move(before), "Clear Mesh Renderer material");
                } else if (dropped_material) {
                    record_immediate_edit(state, std::move(before), "Assign Mesh Renderer material");
                }
                if (mesh.material_resolved) {
                    ImGui::TextDisabled("Shader: %s", vespera::builtin_material_shader_name(mesh.resolved_material.shader).data());
                } else if (!mesh.material.empty()) {
                    ImGui::TextColored(ImVec4(1.0f,0.55f,0.42f,1.0f), "Material reference is unresolved.");
                }

                before = capture_snapshot(state);
                bool changed_mesh_texture = texture_combo("Fallback Texture", mesh.texture, state.scene.world);
                changed_mesh_texture = accept_texture_asset_drop(state, mesh.texture, "Mesh Renderer") || changed_mesh_texture;
                if (changed_mesh_texture) {
                    record_immediate_edit(state, std::move(before), "Change Mesh Renderer texture");
                }
                before = capture_snapshot(state);
                const ImGuiID mesh_tint_id = ImGui::GetID("Mesh Tint");
                const bool changed_mesh_tint = ImGui::ColorEdit4("Instance Tint", mesh.color.data());
                track_item_edit(state, std::move(before), "Edit Mesh Renderer tint", mesh_tint_id, changed_mesh_tint);
                if (ImGui::Button("Remove Mesh Renderer")) {
                    before = capture_snapshot(state); entity.remove_component(vespera::BuiltinComponentType::MeshRenderer);
                    record_immediate_edit(state, std::move(before), "Remove Mesh Renderer component");
                }
                ImGui::TreePop();
            }
        }

        if (entity.cylinder_collider) {
            auto& collider = *entity.cylinder_collider;
            if (ImGui::TreeNodeEx("Cylinder Collider", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextDisabled("2.5D X/Z collision primitive; triggers do not block movement.");

                before = capture_snapshot(state);
                const ImGuiID radius_id = ImGui::GetID("Radius");
                const bool changed_radius = ImGui::DragFloat("Radius", &collider.radius, 0.025f, 0.01f, 100.0f, "%.3f");
                if (changed_radius) collider.radius = std::max(collider.radius, 0.01f);
                track_item_edit(state, std::move(before), "Edit Cylinder Collider radius", radius_id, changed_radius);

                before = capture_snapshot(state);
                const ImGuiID height_id = ImGui::GetID("Height");
                const bool changed_height = ImGui::DragFloat("Height", &collider.height, 0.025f, 0.01f, 100.0f, "%.3f");
                if (changed_height) collider.height = std::max(collider.height, 0.01f);
                track_item_edit(state, std::move(before), "Edit Cylinder Collider height", height_id, changed_height);

                float center[3]{collider.center.x, collider.center.y, collider.center.z};
                before = capture_snapshot(state);
                const ImGuiID center_id = ImGui::GetID("Center");
                const bool changed_center = ImGui::DragFloat3("Center", center, 0.025f);
                if (changed_center) collider.center = {center[0], center[1], center[2]};
                track_item_edit(state, std::move(before), "Edit Cylinder Collider center", center_id, changed_center);

                before = capture_snapshot(state);
                const bool changed_trigger = ImGui::Checkbox("Is Trigger", &collider.is_trigger);
                if (changed_trigger) record_immediate_edit(state, std::move(before), "Toggle Cylinder Collider trigger");

                if (ImGui::Button("Remove Cylinder Collider")) {
                    before = capture_snapshot(state);
                    entity.remove_component(vespera::BuiltinComponentType::CylinderCollider);
                    record_immediate_edit(state, std::move(before), "Remove Cylinder Collider component");
                }
                ImGui::TreePop();
            }
        }

        if (entity.point_light) {
            auto& light = *entity.point_light;
            if (ImGui::TreeNodeEx("Point Light", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextDisabled("Unshadowed point light with radial falloff.");

                before = capture_snapshot(state);
                const ImGuiID light_color_id = ImGui::GetID("Light Color");
                const bool changed_light_color = ImGui::ColorEdit4("Color", light.color.data());
                track_item_edit(state, std::move(before), "Edit Point Light color", light_color_id, changed_light_color);

                before = capture_snapshot(state);
                const ImGuiID intensity_id = ImGui::GetID("Light Intensity");
                const bool changed_intensity = ImGui::DragFloat("Intensity", &light.intensity, 0.025f, 0.0f, 16.0f, "%.2f");
                if (changed_intensity) light.intensity = std::max(light.intensity, 0.0f);
                track_item_edit(state, std::move(before), "Edit Point Light intensity", intensity_id, changed_intensity);

                before = capture_snapshot(state);
                const ImGuiID light_radius_id = ImGui::GetID("Light Radius");
                const bool changed_light_radius = ImGui::DragFloat("Radius", &light.radius, 0.05f, 0.05f, 100.0f, "%.2f");
                if (changed_light_radius) light.radius = std::max(light.radius, 0.05f);
                track_item_edit(state, std::move(before), "Edit Point Light radius", light_radius_id, changed_light_radius);

                if (ImGui::Button("Remove Point Light")) {
                    before = capture_snapshot(state);
                    entity.remove_component(vespera::BuiltinComponentType::PointLight);
                    record_immediate_edit(state, std::move(before), "Remove Point Light component");
                }
                ImGui::TreePop();
            }
        }

        for (std::size_t script_index = 0; script_index < entity.managed_scripts.size();) {
            auto& script = entity.managed_scripts[script_index];
            ImGui::PushID(static_cast<int>(script_index) + 700000);
            const std::string header = std::format("C# Script {}##managed", script_index + 1);
            bool remove_script = false;
            bool move_script_up = false;
            bool move_script_down = false;
            bool duplicate_script = false;
            if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                if (state.managed_metadata.loaded && !state.managed_metadata.scripts.empty()) {
                    const char* preview = script.class_name.empty() ? "<select component>" : script.class_name.c_str();
                    if (ImGui::BeginCombo("Component Type", preview)) {
                        for (const auto& candidate : state.managed_metadata.scripts) {
                            const bool selected = candidate.class_name == script.class_name;
                            if (ImGui::Selectable(candidate.class_name.c_str(), selected) && !selected) {
                                before = capture_snapshot(state);
                                script.class_name = candidate.class_name;
                                record_immediate_edit(state, std::move(before), "Select C# component type");
                            }
                            if (selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }
                before = capture_snapshot(state);
                const ImGuiID class_id = ImGui::GetID("Class Name");
                const bool changed_class = ImGui::InputText("Class Name", &script.class_name);
                track_item_edit(state, std::move(before), "Edit C# script class", class_id, changed_class);
                ImGui::TextDisabled("Class Name stays editable if the current C# type cannot be found.");
                before = capture_snapshot(state);
                const bool changed_script_enabled = ImGui::Checkbox("Script Enabled", &script.enabled);
                if (changed_script_enabled) record_immediate_edit(state, std::move(before), "Toggle C# script");

                const auto* meta = state.managed_metadata.find(script.class_name);
                if (meta) {
                    ImGui::SeparatorText("Exposed Fields");
                    for (const auto& field_meta : meta->fields) {
                        ImGui::PushID(field_meta.name.c_str());
                        auto matches_field = [&](const auto& stored) {
                            if (stored.field_name == field_meta.name) return true;
                            return std::find(field_meta.aliases.begin(), field_meta.aliases.end(), stored.field_name) != field_meta.aliases.end();
                        };
                        auto it = std::find_if(script.fields.begin(), script.fields.end(), matches_field);
                        bool overridden = it != script.fields.end();
                        if (ImGui::Checkbox("##override", &overridden)) {
                            before = capture_snapshot(state);
                            if (overridden) script.fields.push_back({field_meta.name, field_meta.type, default_managed_value(field_meta)});
                            else if (it != script.fields.end()) script.fields.erase(it);
                            record_immediate_edit(state, std::move(before), std::string(overridden ? "Override " : "Reset ") + field_meta.display_name);
                            it = std::find_if(script.fields.begin(), script.fields.end(), matches_field);
                        }
                        ImGui::SameLine(); ImGui::TextUnformatted(field_meta.display_name.c_str());
                        if (it != script.fields.end() && it->field_name != field_meta.name) {
                            ImGui::SameLine();
                            ImGui::TextDisabled("(previous field name: %s)", it->field_name.c_str());
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Migrate Name")) {
                                before = capture_snapshot(state);
                                it->field_name = field_meta.name;
                                it->type_name = field_meta.type;
                                record_immediate_edit(state, std::move(before), "Migrate C# serialized field name");
                            }
                        }
                        if (field_meta.type == "unsupported") { ImGui::SameLine(); ImGui::TextDisabled("(unsupported: %s)", field_meta.clr_type.c_str()); }
                        else if (it != script.fields.end()) {
                            bool changed = false;
                            if (field_meta.type == "bool") {
                                before = capture_snapshot(state);
                                const ImGuiID value_id = ImGui::GetID("Value");
                                bool value = it->serialized_value == "true" || it->serialized_value == "1";
                                changed = ImGui::Checkbox("Value", &value);
                                if (changed) it->serialized_value = value ? "true" : "false";
                                track_item_edit(state, std::move(before), "Edit exposed C# field", value_id, changed);
                            } else if (field_meta.type == "int") {
                                int value = 0; std::istringstream(it->serialized_value) >> value;
                                if (field_meta.has_range) {
                                    const int min_value = static_cast<int>(field_meta.range_min);
                                    const int max_value = static_cast<int>(field_meta.range_max);
                                    before = capture_snapshot(state);
                                    ImGui::SetNextItemWidth((std::max)(120.0f, ImGui::GetContentRegionAvail().x - 105.0f));
                                    const ImGuiID slider_id = ImGui::GetID("##range_slider");
                                    const bool slider_changed = ImGui::SliderInt("##range_slider", &value, min_value, max_value);
                                    if (slider_changed) it->serialized_value = std::to_string(value);
                                    track_item_edit(state, std::move(before), "Edit exposed C# field", slider_id, slider_changed);
                                    ImGui::SameLine();
                                    int precise = value;
                                    before = capture_snapshot(state);
                                    ImGui::SetNextItemWidth(96.0f);
                                    const ImGuiID precise_id = ImGui::GetID("##range_precise");
                                    const bool precise_changed = ImGui::InputInt("##range_precise", &precise, 0, 0);
                                    if (precise_changed) { precise = std::clamp(precise, min_value, max_value); it->serialized_value = std::to_string(precise); }
                                    track_item_edit(state, std::move(before), "Enter precise exposed C# field value", precise_id, precise_changed);
                                    changed = slider_changed || precise_changed;
                                } else {
                                    before = capture_snapshot(state);
                                    const ImGuiID value_id = ImGui::GetID("Value");
                                    changed = ImGui::InputInt("Value", &value);
                                    if (changed) it->serialized_value = std::to_string(value);
                                    track_item_edit(state, std::move(before), "Edit exposed C# field", value_id, changed);
                                }
                            } else if (field_meta.type == "float") {
                                float value = 0.0f; std::istringstream(it->serialized_value) >> value;
                                if (field_meta.has_range) {
                                    before = capture_snapshot(state);
                                    ImGui::SetNextItemWidth((std::max)(120.0f, ImGui::GetContentRegionAvail().x - 105.0f));
                                    const ImGuiID slider_id = ImGui::GetID("##range_slider");
                                    const bool slider_changed = ImGui::SliderFloat("##range_slider", &value, field_meta.range_min, field_meta.range_max, "%.3f");
                                    if (slider_changed) it->serialized_value = std::format("{}", value);
                                    track_item_edit(state, std::move(before), "Edit exposed C# field", slider_id, slider_changed);
                                    ImGui::SameLine();
                                    float precise = value;
                                    before = capture_snapshot(state);
                                    ImGui::SetNextItemWidth(96.0f);
                                    const ImGuiID precise_id = ImGui::GetID("##range_precise");
                                    const bool precise_changed = ImGui::InputFloat("##range_precise", &precise, 0.0f, 0.0f, "%.6g");
                                    if (precise_changed) { precise = std::clamp(precise, field_meta.range_min, field_meta.range_max); it->serialized_value = std::format("{}", precise); }
                                    track_item_edit(state, std::move(before), "Enter precise exposed C# field value", precise_id, precise_changed);
                                    changed = slider_changed || precise_changed;
                                } else {
                                    before = capture_snapshot(state);
                                    const ImGuiID value_id = ImGui::GetID("Value");
                                    changed = ImGui::DragFloat("Value", &value, 0.025f);
                                    if (changed) it->serialized_value = std::format("{}", value);
                                    track_item_edit(state, std::move(before), "Edit exposed C# field", value_id, changed);
                                }
                            } else if (field_meta.type == "enum") {
                                const char* preview_value = it->serialized_value.empty() ? "<select>" : it->serialized_value.c_str();
                                if (ImGui::BeginCombo("Value", preview_value)) {
                                    for (const auto& enum_value : field_meta.enum_values) {
                                        const bool selected = enum_value == it->serialized_value;
                                        if (ImGui::Selectable(enum_value.c_str(), selected) && !selected) {
                                            before = capture_snapshot(state);
                                            it->serialized_value = enum_value;
                                            record_immediate_edit(state, std::move(before), "Edit exposed C# enum field");
                                            changed = true;
                                        }
                                        if (selected) ImGui::SetItemDefaultFocus();
                                    }
                                    ImGui::EndCombo();
                                }
                            } else if (field_meta.type == "vec2") {
                                float value[2]{0.0f,0.0f}; std::istringstream parse(it->serialized_value); parse >> value[0] >> value[1];
                                before = capture_snapshot(state);
                                const ImGuiID value_id = ImGui::GetID("Value");
                                changed = ImGui::DragFloat2("Value", value, 0.025f);
                                if (changed) it->serialized_value = std::format("{} {}", value[0], value[1]);
                                track_item_edit(state, std::move(before), "Edit exposed C# field", value_id, changed);
                            } else if (field_meta.type == "vec3") {
                                float value[3]{0.0f,0.0f,0.0f}; std::istringstream parse(it->serialized_value); parse >> value[0] >> value[1] >> value[2];
                                before = capture_snapshot(state);
                                const ImGuiID value_id = ImGui::GetID("Value");
                                changed = ImGui::DragFloat3("Value", value, 0.025f);
                                if (changed) it->serialized_value = std::format("{} {} {}", value[0], value[1], value[2]);
                                track_item_edit(state, std::move(before), "Edit exposed C# field", value_id, changed);
                            } else if (field_meta.type == "color") {
                                float value[4]{1.0f,1.0f,1.0f,1.0f}; std::istringstream parse(it->serialized_value); parse >> value[0] >> value[1] >> value[2] >> value[3];
                                before = capture_snapshot(state);
                                const ImGuiID value_id = ImGui::GetID("Value");
                                changed = ImGui::ColorEdit4("Value", value);
                                if (changed) it->serialized_value = std::format("{} {} {} {}", value[0], value[1], value[2], value[3]);
                                track_item_edit(state, std::move(before), "Edit exposed C# field", value_id, changed);
                            } else {
                                before = capture_snapshot(state);
                                const ImGuiID value_id = ImGui::GetID("Value");
                                changed = ImGui::InputText("Value", &it->serialized_value);
                                track_item_edit(state, std::move(before), "Edit exposed C# field", value_id, changed);
                            }
                            ImGui::TextDisabled("%s%s", field_meta.type.c_str(), field_meta.has_range ? " / slider + precise entry" : "");
                        } else { ImGui::SameLine(); ImGui::TextDisabled("<code default>"); }
                        if (!field_meta.tooltip.empty()) {
                            ImGui::SameLine();
                            ImGui::TextDisabled("(?)");
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", field_meta.tooltip.c_str());
                        }
                        ImGui::PopID();
                    }
                    bool unresolved_header = false;
                    for (auto& stored : script.fields) {
                        const bool known = std::any_of(meta->fields.begin(), meta->fields.end(), [&](const auto& f){
                            return f.name == stored.field_name
                                || std::find(f.aliases.begin(), f.aliases.end(), stored.field_name) != f.aliases.end();
                        });
                        if (known) continue;
                        if (!unresolved_header) { ImGui::SeparatorText("Unresolved Overrides"); unresolved_header = true; }
                        ImGui::PushID(stored.field_name.c_str());
                        ImGui::TextDisabled("%s (%s)", stored.field_name.c_str(), stored.type_name.c_str());
                        before = capture_snapshot(state);
                        const ImGuiID raw_id = ImGui::GetID("Raw Value");
                        const bool changed_raw = ImGui::InputText("Raw Value", &stored.serialized_value);
                        track_item_edit(state, std::move(before), "Edit unresolved C# field", raw_id, changed_raw);
                        ImGui::PopID();
                    }
                } else {
                    ImGui::TextColored(ImVec4(1.0f,0.65f,0.25f,1.0f), "Type not found in current C# metadata; serialized data is preserved.");
                    for (auto& stored : script.fields) {
                        ImGui::PushID(stored.field_name.c_str());
                        ImGui::TextDisabled("%s (%s)", stored.field_name.c_str(), stored.type_name.c_str());
                        before = capture_snapshot(state);
                        const ImGuiID raw_id = ImGui::GetID("Raw Value");
                        const bool changed_raw = ImGui::InputText("Raw Value", &stored.serialized_value);
                        track_item_edit(state, std::move(before), "Edit unresolved C# field", raw_id, changed_raw);
                        ImGui::PopID();
                    }
                }
                if (!script.fields.empty()) {
                    if (ImGui::Button("Reset All Overrides")) {
                        before = capture_snapshot(state);
                        script.fields.clear();
                        record_immediate_edit(state, std::move(before), "Reset C# script overrides");
                    }
                    ImGui::SameLine();
                }
                ImGui::SeparatorText("Attachment");
                ImGui::TextDisabled("Execution order follows the attachment order on this Entity.");
                ImGui::BeginDisabled(script_index == 0);
                if (ImGui::Button("Move Up")) move_script_up = true;
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(script_index + 1 >= entity.managed_scripts.size());
                if (ImGui::Button("Move Down")) move_script_down = true;
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button("Duplicate")) duplicate_script = true;
                ImGui::SameLine();
                if (ImGui::Button("Remove C# Script")) remove_script = true;
                ImGui::TreePop();
            }
            ImGui::PopID();
            if (remove_script) {
                before = capture_snapshot(state);
                entity.remove_managed_script(script_index);
                record_immediate_edit(state, std::move(before), "Remove C# Script");
            } else if (duplicate_script) {
                before = capture_snapshot(state);
                entity.managed_scripts.insert(entity.managed_scripts.begin() + static_cast<std::ptrdiff_t>(script_index + 1), script);
                record_immediate_edit(state, std::move(before), "Duplicate C# Script");
                break;
            } else if (move_script_up && script_index > 0) {
                before = capture_snapshot(state);
                std::swap(entity.managed_scripts[script_index], entity.managed_scripts[script_index - 1]);
                record_immediate_edit(state, std::move(before), "Move C# Script up");
                break;
            } else if (move_script_down && script_index + 1 < entity.managed_scripts.size()) {
                before = capture_snapshot(state);
                std::swap(entity.managed_scripts[script_index], entity.managed_scripts[script_index + 1]);
                record_immediate_edit(state, std::move(before), "Move C# Script down");
                break;
            } else ++script_index;
        }

        ImGui::Separator();
        if (ImGui::Button("Duplicate", ImVec2(110.0f, 0.0f))) command_duplicate_selection(state);
        ImGui::SameLine();
        if (ImGui::Button("Delete", ImVec2(110.0f, 0.0f))) command_delete_selection(state);
        ImGui::End();
    }

}


} // namespace vespera::editor
