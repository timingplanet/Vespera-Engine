#include "editor_project_panel.hpp"

#include "editor_asset_interactions.hpp"
#include "editor_assets.hpp"
#include "editor_build_pipeline.hpp"
#include "editor_console.hpp"
#include "editor_history.hpp"
#include "editor_managed.hpp"
#include "editor_play_controls.hpp"
#include "editor_prefabs.hpp"
#include "editor_project_session.hpp"
#include "editor_scene_commands.hpp"
#include "editor_selection.hpp"
#include "editor_state.hpp"
#include "editor_style.hpp"
#include "editor_ui_authoring.hpp"

#include <vespera/assets/asset_authoring.hpp>
#include <vespera/assets/build_manifest.hpp>
#include <vespera/assets/texture_importer.hpp>
#include <vespera/project/project.hpp>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace vespera::editor {

const AssetThumbnail* texture_asset_thumbnail(EditorState& state, const vespera::AssetRecord& record) {
    if (record.kind != vespera::AssetKind::Texture || record.asset_id.empty()) return nullptr;
    auto& thumbnail = state.asset_thumbnails[record.asset_id];
    if (thumbnail.source_hash != record.source_hash) {
        thumbnail = {};
        thumbnail.source_hash = record.source_hash;
    }
    if (!thumbnail.attempted) {
        thumbnail.attempted = true;
        const auto imported = vespera::import_texture(record.absolute_path, record.display_name);
        if (imported && imported.texture.valid()) {
            constexpr std::uint32_t max_side = 24;
            const float aspect = static_cast<float>(imported.texture.width) / static_cast<float>(imported.texture.height);
            thumbnail.width = aspect >= 1.0f ? max_side : std::max(1u, static_cast<std::uint32_t>(max_side * aspect));
            thumbnail.height = aspect >= 1.0f ? std::max(1u, static_cast<std::uint32_t>(max_side / aspect)) : max_side;
            thumbnail.pixels.resize(static_cast<std::size_t>(thumbnail.width) * thumbnail.height);
            for (std::uint32_t y = 0; y < thumbnail.height; ++y) {
                const std::uint32_t sy = std::min(imported.texture.height - 1u,
                    static_cast<std::uint32_t>((static_cast<std::uint64_t>(y) * imported.texture.height) / thumbnail.height));
                for (std::uint32_t x = 0; x < thumbnail.width; ++x) {
                    const std::uint32_t sx = std::min(imported.texture.width - 1u,
                        static_cast<std::uint32_t>((static_cast<std::uint64_t>(x) * imported.texture.width) / thumbnail.width));
                    const std::size_t src = (static_cast<std::size_t>(sy) * imported.texture.width + sx) * 4u;
                    thumbnail.pixels[static_cast<std::size_t>(y) * thumbnail.width + x] = IM_COL32(
                        imported.texture.rgba8[src], imported.texture.rgba8[src+1],
                        imported.texture.rgba8[src+2], imported.texture.rgba8[src+3]);
                }
            }
            thumbnail.valid = true;
        }
    }
    return thumbnail.valid ? &thumbnail : nullptr;
}

void draw_cpu_thumbnail(ImDrawList* draw, const AssetThumbnail& thumbnail, ImVec2 min, ImVec2 max) {
    if (!thumbnail.valid || thumbnail.width == 0 || thumbnail.height == 0) return;
    const float width = max.x - min.x;
    const float height = max.y - min.y;
    const float cell = std::max(1.0f, std::min(width / static_cast<float>(thumbnail.width), height / static_cast<float>(thumbnail.height)));
    const float draw_w = cell * thumbnail.width;
    const float draw_h = cell * thumbnail.height;
    const ImVec2 origin{min.x + (width - draw_w) * 0.5f, min.y + (height - draw_h) * 0.5f};
    const ImU32 checker_a = ImGui::GetColorU32(ImVec4(0.16f,0.17f,0.20f,1.0f));
    const ImU32 checker_b = ImGui::GetColorU32(ImVec4(0.24f,0.25f,0.28f,1.0f));
    const float checker = std::max(4.0f, cell * 4.0f);
    for (float y = min.y; y < max.y; y += checker) for (float x = min.x; x < max.x; x += checker) {
        const int parity = (static_cast<int>((x-min.x)/checker) + static_cast<int>((y-min.y)/checker)) & 1;
        draw->AddRectFilled({x,y},{std::min(x+checker,max.x),std::min(y+checker,max.y)}, parity ? checker_a : checker_b);
    }
    for (std::uint32_t y = 0; y < thumbnail.height; ++y) for (std::uint32_t x = 0; x < thumbnail.width; ++x) {
        const ImVec2 a{origin.x + x * cell, origin.y + y * cell};
        draw->AddRectFilled(a, {a.x + cell + 0.5f, a.y + cell + 0.5f}, thumbnail.pixels[static_cast<std::size_t>(y)*thumbnail.width+x]);
    }
}

void draw_assets(EditorState& state) {
    ImGui::Begin("Project");

    // Lightweight source watching. Metadata v2 stores source mtimes, so
    // unchanged assets take the no-hash fast path and this remains cheap for
    // ordinary project sizes while dependency edges are rebuilt from the refreshed source catalog.
    if (state.asset_auto_refresh && !state.assets_root.empty()) {
        const double now = ImGui::GetTime();
        if (now >= state.next_asset_refresh_time) {
            state.next_asset_refresh_time = now + 2.5;
            refresh_asset_catalog(state, false);
        }
    }

    const auto& palette = editor_ui_palette();
    if (state.project_loaded) {
        ImGui::TextColored(palette.accent, "%s", state.project.name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("/ %s", state.scene_path.empty() ? "no scene" : state.scene_path.filename().string().c_str());
    } else {
        ImGui::TextDisabled("Scene-only workspace");
    }

    if (ImGui::BeginTabBar("ProjectTabs", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem("Assets")) {
            // Unity-familiar Project rail: folders are primary; asset type filtering
            // lives in the browser toolbar rather than competing with the folder tree.
            ImGui::BeginChild("AssetSourceRail", ImVec2(176.0f, 0.0f), true);
            draw_panel_heading("Folders", std::format("{} assets", state.asset_catalog.records().size()));
            ImGui::Spacing();
            if (ImGui::Selectable("Assets", state.asset_browser_folder.empty(), 0, ImVec2(0.0f, 26.0f))) state.asset_browser_folder.clear();
            std::vector<std::filesystem::path> folders;
            for (const auto& record : state.asset_catalog.records()) {
                auto folder_path = record.relative_path.parent_path().lexically_normal();
                while (!folder_path.empty() && folder_path != ".") {
                    if (std::find(folders.begin(), folders.end(), folder_path) == folders.end()) folders.push_back(folder_path);
                    folder_path = folder_path.parent_path();
                }
            }
            std::sort(folders.begin(), folders.end(), [](const auto& a, const auto& b) {
                return a.generic_string() < b.generic_string();
            });
            for (const auto& folder_path : folders) {
                int depth = 0;
                for (const auto& part : folder_path) { (void)part; ++depth; }
                ImGui::Indent(static_cast<float>((std::max)(0, depth - 1)) * 10.0f);
                const auto label = folder_path.filename().string() + "##folder_" + folder_path.generic_string();
                if (ImGui::Selectable(label.c_str(), state.asset_browser_folder == folder_path)) state.asset_browser_folder = folder_path;
                ImGui::Unindent(static_cast<float>((std::max)(0, depth - 1)) * 10.0f);
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::Checkbox("Auto refresh", &state.asset_auto_refresh);
            if (ImGui::Button("Reimport All", ImVec2(-1.0f, 0.0f))) refresh_asset_catalog(state, true, true);
            if (state.last_asset_report.orphaned_metadata != 0) {
                ImGui::TextColored(palette.warning, "%zu orphaned metadata", state.last_asset_report.orphaned_metadata);
            }
            ImGui::EndChild();

            ImGui::SameLine();
            ImGui::BeginChild("AssetBrowserMain", ImVec2(0.0f, 0.0f), false);
            if (ImGui::SmallButton("Assets##breadcrumb_root")) state.asset_browser_folder.clear();
            std::filesystem::path breadcrumb_path;
            for (const auto& part : state.asset_browser_folder) {
                if (part == ".") continue;
                breadcrumb_path /= part;
                ImGui::SameLine();
                ImGui::TextDisabled(">");
                ImGui::SameLine();
                const std::string breadcrumb_label = part.string() + "##breadcrumb_" + breadcrumb_path.generic_string();
                if (ImGui::SmallButton(breadcrumb_label.c_str())) state.asset_browser_folder = breadcrumb_path;
            }
            ImGui::SameLine();
            if (state.last_asset_report.broken_dependencies != 0 || state.last_asset_report.stale_fallback_paths != 0) {
                ImGui::TextColored(palette.warning, "%zu broken reference(s) / %zu outdated path(s)",
                    state.last_asset_report.broken_dependencies, state.last_asset_report.stale_fallback_paths);
            } else {
                ImGui::TextDisabled("%zu assets", state.asset_catalog.records().size());
            }

            ImGui::SetNextItemWidth(std::min(340.0f, ImGui::GetContentRegionAvail().x * 0.42f));
            state.asset_filter.Draw("Search##assets");
            ImGui::SameLine();
            const char* kind_names[] = {"All", "Scenes", "Prefabs", "Sprite Clips", "Sprite Sheets", "Textures", "Audio", "Audio Clips", "Fonts", "Materials", "Vespera UI (.slui)", "RmlUi", "RCSS"};
            ImGui::SetNextItemWidth(126.0f);
            ImGui::Combo("##asset_kind_filter", &state.asset_kind_filter, kind_names, IM_ARRAYSIZE(kind_names));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Asset type filter");
            ImGui::SameLine();
            if (ImGui::SmallButton(state.asset_grid_view ? "Grid" : "List")) state.asset_grid_view = !state.asset_grid_view;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle asset grid/list view");
            ImGui::SameLine();
            if (state.asset_grid_view) {
                ImGui::SetNextItemWidth(84.0f);
                ImGui::SliderFloat("##asset_tile_size", &state.asset_tile_size, 72.0f, 132.0f, "%.0f px");
                ImGui::SameLine();
            }
            if (ImGui::Button("Refresh")) refresh_asset_catalog(state, true, false);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                ImGui::SetTooltip("Refresh asset catalog\n%zu fast-path / %zu hashed",
                    state.last_asset_report.fast_path_hits, state.last_asset_report.hashes_computed);
            }

            std::optional<std::filesystem::path> asset_open_scene;
            std::optional<std::filesystem::path> asset_instantiate_prefab;
            std::optional<std::string> asset_open_ui;
            auto asset_matches_filters = [&](const vespera::AssetRecord& record) {
                int filter_kind = 0;
                switch (record.kind) {
                    case vespera::AssetKind::Scene: filter_kind = 1; break;
                    case vespera::AssetKind::EntityPrefab: filter_kind = 2; break;
                    case vespera::AssetKind::SpriteClip: filter_kind = 3; break;
                    case vespera::AssetKind::SpriteSheet: filter_kind = 4; break;
                    case vespera::AssetKind::Texture: filter_kind = 5; break;
                    case vespera::AssetKind::Audio: filter_kind = 6; break;
                    case vespera::AssetKind::AudioClip: filter_kind = 7; break;
                    case vespera::AssetKind::Font: filter_kind = 8; break;
                    case vespera::AssetKind::Material: filter_kind = 9; break;
                    case vespera::AssetKind::UiDocument: filter_kind = 10; break;
                    case vespera::AssetKind::RmlDocument: filter_kind = 11; break;
                    case vespera::AssetKind::RmlStyleSheet: filter_kind = 12; break;
                }
                if (state.asset_kind_filter != 0 && state.asset_kind_filter != filter_kind) return false;
                if (!state.asset_browser_folder.empty()) {
                    const auto folder = record.relative_path.parent_path().lexically_normal();
                    if (folder != state.asset_browser_folder.lexically_normal()) return false;
                }
                const std::string search_text = record.relative_path.generic_string() + " " + record.importer;
                return state.asset_filter.PassFilter(search_text.c_str());
            };

            if (state.asset_grid_view) {
                const float tile = std::clamp(state.asset_tile_size, 72.0f, 132.0f);
                const float spacing = 10.0f;
                const float cell_width = tile + spacing;
                const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cell_width));
                if (ImGui::BeginTable("AssetGrid", columns, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings)) {
                    int column = 0;
                    for (const auto& record : state.asset_catalog.records()) {
                        if (!asset_matches_filters(record)) continue;
                        if (column == 0) ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(column);
                        ImGui::PushID(record.asset_id.c_str());
                        const bool selected = state.selection.kind == SelectionKind::Asset && state.selection.asset_id == record.asset_id;
                        const ImVec2 pos = ImGui::GetCursorScreenPos();
                        const ImVec2 card_size{tile, tile + 36.0f};
                        if (ImGui::Selectable("##asset_tile", selected, ImGuiSelectableFlags_AllowDoubleClick, card_size)) {
                            commit_active_edit(state);
                            state.selection = {SelectionKind::Asset};
                            state.selection.asset_id = record.asset_id;
                            state.selected_entity_ids.clear();
                            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                                if (record.kind == vespera::AssetKind::Scene) asset_open_scene = record.absolute_path;
                                else if (record.kind == vespera::AssetKind::EntityPrefab) asset_instantiate_prefab = record.absolute_path;
                                else if (record.kind == vespera::AssetKind::UiDocument) asset_open_ui = record.asset_id;
                            }
                        }
                        ImVec4 type_color(0.62f, 0.66f, 0.74f, 1.0f);
                        const char* type_short = "ASSET";
                        switch (record.kind) {
                            case vespera::AssetKind::Scene: type_short="SCENE"; type_color=ImVec4(0.48f,0.72f,1.0f,1.0f); break;
                            case vespera::AssetKind::EntityPrefab: type_short="PREFAB"; type_color=ImVec4(0.67f,0.57f,1.0f,1.0f); break;
                            case vespera::AssetKind::SpriteClip: type_short="CLIP"; type_color=ImVec4(0.78f,0.64f,1.0f,1.0f); break;
                            case vespera::AssetKind::SpriteSheet: type_short="SHEET"; type_color=ImVec4(0.55f,0.78f,1.0f,1.0f); break;
                            case vespera::AssetKind::Texture: type_short="TEX"; type_color=ImVec4(0.46f,0.86f,0.73f,1.0f); break;
                            case vespera::AssetKind::Audio: type_short="AUDIO"; type_color=ImVec4(1.0f,0.72f,0.43f,1.0f); break;
                            case vespera::AssetKind::AudioClip: type_short="A-CLIP"; type_color=ImVec4(1.0f,0.60f,0.35f,1.0f); break;
                            case vespera::AssetKind::Font: type_short="FONT"; type_color=ImVec4(0.94f,0.58f,0.77f,1.0f); break;
                            case vespera::AssetKind::Material: type_short="MAT"; type_color=ImVec4(0.78f,0.70f,0.42f,1.0f); break;
                            case vespera::AssetKind::UiDocument: type_short="SLUI"; type_color=ImVec4(0.55f,0.82f,0.95f,1.0f); break;
                            case vespera::AssetKind::RmlDocument: type_short="RML"; type_color=ImVec4(0.45f,0.78f,1.0f,1.0f); break;
                            case vespera::AssetKind::RmlStyleSheet: type_short="RCSS"; type_color=ImVec4(0.56f,0.72f,1.0f,1.0f); break;
                        }
                        auto* draw = ImGui::GetWindowDrawList();
                        const ImU32 bg = ImGui::GetColorU32(ImVec4(type_color.x*0.22f, type_color.y*0.22f, type_color.z*0.22f, 0.95f));
                        const ImU32 border = ImGui::GetColorU32(type_color);
                        const ImVec2 icon_min{pos.x + 8.0f, pos.y + 8.0f};
                        const ImVec2 icon_max{pos.x + tile - 8.0f, pos.y + tile - 22.0f};
                        draw->AddRectFilled(icon_min, icon_max, bg, 6.0f);
                        if (record.kind == vespera::AssetKind::Texture) {
                            if (const auto* thumbnail = texture_asset_thumbnail(state, record)) {
                                draw_cpu_thumbnail(draw, *thumbnail, {icon_min.x + 2.0f, icon_min.y + 2.0f}, {icon_max.x - 2.0f, icon_max.y - 2.0f});
                            } else {
                                const ImVec2 type_size = ImGui::CalcTextSize(type_short);
                                draw->AddText({(icon_min.x+icon_max.x-type_size.x)*0.5f, (icon_min.y+icon_max.y-type_size.y)*0.5f}, border, type_short);
                            }
                        } else {
                            const ImVec2 type_size = ImGui::CalcTextSize(type_short);
                            draw->AddText({(icon_min.x+icon_max.x-type_size.x)*0.5f, (icon_min.y+icon_max.y-type_size.y)*0.5f}, border, type_short);
                        }
                        draw->AddRect(icon_min, icon_max, border, 6.0f, 0, selected ? 2.0f : 1.0f);
                        const std::string display = record.display_name;
                        draw->AddText({pos.x + 6.0f, pos.y + tile - 14.0f}, ImGui::GetColorU32(ImGuiCol_Text), display.c_str());
                        const auto folder = record.relative_path.parent_path().filename().generic_string();
                        draw->AddText({pos.x + 6.0f, pos.y + tile + 4.0f}, ImGui::GetColorU32(ImGuiCol_TextDisabled), folder.c_str());
                        begin_project_asset_drag(record);
                        if (ImGui::BeginPopupContextItem("asset_grid_context")) {
                            if (record.kind == vespera::AssetKind::Scene && ImGui::MenuItem("Open Scene")) asset_open_scene = record.absolute_path;
                            if (record.kind == vespera::AssetKind::EntityPrefab && ImGui::MenuItem("Instantiate Prefab")) asset_instantiate_prefab = record.absolute_path;
                            if (record.kind == vespera::AssetKind::UiDocument && ImGui::MenuItem("Open UI Authoring")) asset_open_ui = record.asset_id;
                            ImGui::Separator();
                            if (ImGui::MenuItem("Copy Asset ID")) ImGui::SetClipboardText(record.asset_id.c_str());
                            if (ImGui::MenuItem("Copy Relative Path")) { const auto text = record.relative_path.generic_string(); ImGui::SetClipboardText(text.c_str()); }
                            ImGui::Separator();
                            if (ImGui::MenuItem("Move / Rename...")) request_asset_move(state, record);
                            if (ImGui::MenuItem("Check Delete Safety")) {
                                const auto preflight = vespera::preflight_delete_project_asset(state.asset_catalog, state.project, record.asset_id);
                                if (preflight) push_console(state, ConsoleEntry::Level::Info, "Delete preflight: safe to delete " + record.relative_path.generic_string() + " (no mutation performed)");
                                else {
                                    push_console(state, ConsoleEntry::Level::Warning, "Delete preflight blocked for " + record.relative_path.generic_string());
                                    for (const auto& blocker : preflight.blockers) push_console(state, ConsoleEntry::Level::Warning, "  - " + blocker);
                                }
                            }
                            ImGui::EndPopup();
                        }
                        ImGui::PopID();
                        column = (column + 1) % columns;
                    }
                    ImGui::EndTable();
                }
            } else {
                const float asset_table_height = std::max(110.0f, ImGui::GetContentRegionAvail().y - 38.0f);
                if (ImGui::BeginTable("AssetBrowser", 5,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH
                        | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY
                        | ImGuiTableFlags_SizingStretchProp,
                        ImVec2(0.0f, asset_table_height))) {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 82.0f);
                    ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthStretch, 0.40f);
                    ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch, 0.32f);
                    ImGui::TableSetupColumn("Import", ImGuiTableColumnFlags_WidthFixed, 92.0f);
                    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 72.0f);
                    ImGui::TableHeadersRow();

                    for (const auto& record : state.asset_catalog.records()) {
                        int filter_kind = 0;
                        switch (record.kind) {
                            case vespera::AssetKind::Scene: filter_kind = 1; break;
                            case vespera::AssetKind::EntityPrefab: filter_kind = 2; break;
                            case vespera::AssetKind::SpriteClip: filter_kind = 3; break;
                            case vespera::AssetKind::SpriteSheet: filter_kind = 4; break;
                            case vespera::AssetKind::Texture: filter_kind = 5; break;
                            case vespera::AssetKind::Audio: filter_kind = 6; break;
                            case vespera::AssetKind::AudioClip: filter_kind = 7; break;
                            case vespera::AssetKind::Font: filter_kind = 8; break;
                            case vespera::AssetKind::Material: filter_kind = 9; break;
                            case vespera::AssetKind::UiDocument: filter_kind = 10; break;
                            case vespera::AssetKind::RmlDocument: filter_kind = 11; break;
                            case vespera::AssetKind::RmlStyleSheet: filter_kind = 12; break;
                        }
                        if (state.asset_kind_filter != 0 && state.asset_kind_filter != filter_kind) continue;
                        if (!state.asset_browser_folder.empty()) {
                            const auto folder = record.relative_path.parent_path().lexically_normal();
                            const auto selected_folder = state.asset_browser_folder.lexically_normal();
                            const auto folder_text = folder.generic_string();
                            const auto selected_text = selected_folder.generic_string();
                            if (folder_text != selected_text && !(folder_text.size() > selected_text.size()
                                && folder_text.compare(0, selected_text.size(), selected_text) == 0
                                && folder_text[selected_text.size()] == '/')) continue;
                        }
                        const std::string search_text = record.relative_path.generic_string() + " " + record.importer;
                        if (!state.asset_filter.PassFilter(search_text.c_str())) continue;

                        ImGui::PushID(record.asset_id.c_str());
                        ImGui::TableNextRow(ImGuiTableRowFlags_None, 29.0f);
                        ImGui::TableSetColumnIndex(0);
                        const char* type_short = "ASSET";
                        ImVec4 type_color(0.62f, 0.66f, 0.74f, 1.0f);
                        switch (record.kind) {
                            case vespera::AssetKind::Scene: type_short = "SCENE"; type_color = ImVec4(0.48f,0.72f,1.0f,1.0f); break;
                            case vespera::AssetKind::EntityPrefab: type_short = "PREFAB"; type_color = ImVec4(0.67f,0.57f,1.0f,1.0f); break;
                            case vespera::AssetKind::SpriteClip: type_short = "CLIP"; type_color = ImVec4(0.78f,0.64f,1.0f,1.0f); break;
                            case vespera::AssetKind::SpriteSheet: type_short = "SHEET"; type_color = ImVec4(0.55f,0.78f,1.0f,1.0f); break;
                            case vespera::AssetKind::Texture: type_short = "TEXTURE"; type_color = ImVec4(0.46f,0.86f,0.73f,1.0f); break;
                            case vespera::AssetKind::Audio: type_short = "AUDIO"; type_color = ImVec4(1.0f,0.72f,0.43f,1.0f); break;
                            case vespera::AssetKind::AudioClip: type_short = "A-CLIP"; type_color = ImVec4(1.0f,0.60f,0.35f,1.0f); break;
                            case vespera::AssetKind::Font: type_short = "FONT"; type_color = ImVec4(0.94f,0.58f,0.77f,1.0f); break;
                            case vespera::AssetKind::Material: type_short = "MATERIAL"; type_color = ImVec4(0.78f,0.70f,0.42f,1.0f); break;
                            case vespera::AssetKind::UiDocument: type_short = "SLUI"; type_color = ImVec4(0.55f,0.82f,0.95f,1.0f); break;
                            case vespera::AssetKind::RmlDocument: type_short = "RML"; type_color = ImVec4(0.45f,0.78f,1.0f,1.0f); break;
                            case vespera::AssetKind::RmlStyleSheet: type_short = "RCSS"; type_color = ImVec4(0.56f,0.72f,1.0f,1.0f); break;
                        }
                        ImGui::TextColored(type_color, "%s", type_short);

                        ImGui::TableSetColumnIndex(1);
                        const bool selected = state.selection.kind == SelectionKind::Asset && state.selection.asset_id == record.asset_id;
                        const bool active_scene = record.kind == vespera::AssetKind::Scene
                            && !state.scene_path.empty()
                            && record.absolute_path.lexically_normal() == state.scene_path.lexically_normal();
                        std::string row_name = record.display_name;
                        if (active_scene) row_name += state.dirty ? "  *" : "  (open)";
                        const bool clicked = ImGui::Selectable(row_name.c_str(), selected,
                            ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick,
                            ImVec2(0.0f, 24.0f));
                        if (clicked) {
                            commit_active_edit(state);
                            state.selection = {SelectionKind::Asset};
                            state.selection.asset_id = record.asset_id;
                            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                                if (record.kind == vespera::AssetKind::Scene) asset_open_scene = record.absolute_path;
                                else if (record.kind == vespera::AssetKind::EntityPrefab) asset_instantiate_prefab = record.absolute_path;
                                else if (record.kind == vespera::AssetKind::UiDocument) asset_open_ui = record.asset_id;
                            }
                        }
                        begin_project_asset_drag(record);
                        if (ImGui::BeginPopupContextItem("asset_context")) {
                            if (record.kind == vespera::AssetKind::Scene && ImGui::MenuItem("Open Scene")) asset_open_scene = record.absolute_path;
                            if (record.kind == vespera::AssetKind::EntityPrefab && ImGui::MenuItem("Instantiate Prefab")) asset_instantiate_prefab = record.absolute_path;
                            if (record.kind == vespera::AssetKind::UiDocument && ImGui::MenuItem("Open UI Authoring")) asset_open_ui = record.asset_id;
                            ImGui::Separator();
                            if (ImGui::MenuItem("Copy Asset ID")) ImGui::SetClipboardText(record.asset_id.c_str());
                            if (ImGui::MenuItem("Copy Relative Path")) {
                                const auto text = record.relative_path.generic_string();
                                ImGui::SetClipboardText(text.c_str());
                            }
                            ImGui::Separator();
                            if (ImGui::MenuItem("Move / Rename...")) request_asset_move(state, record);
                            if (ImGui::MenuItem("Check Delete Safety")) {
                                const auto preflight = vespera::preflight_delete_project_asset(state.asset_catalog, state.project, record.asset_id);
                                if (preflight) {
                                    push_console(state, ConsoleEntry::Level::Info, "Delete preflight: safe to delete " + record.relative_path.generic_string() + " (no mutation performed)");
                                } else {
                                    push_console(state, ConsoleEntry::Level::Warning, "Delete preflight blocked for " + record.relative_path.generic_string());
                                    for (const auto& blocker : preflight.blockers) push_console(state, ConsoleEntry::Level::Warning, "  - " + blocker);
                                }
                            }
                            ImGui::EndPopup();
                        }

                        ImGui::TableSetColumnIndex(2);
                        const auto folder = record.relative_path.parent_path().generic_string();
                        ImGui::TextDisabled("%s", folder.empty() ? "." : folder.c_str());
                        ImGui::TableSetColumnIndex(3);
                        const bool import_ok = record.import_state == vespera::AssetImportState::Ready;
                        ImGui::TextColored(import_ok ? ImVec4(0.48f,0.82f,0.62f,1.0f) : ImVec4(1.0f,0.68f,0.25f,1.0f),
                            "%s", vespera::asset_import_state_name(record.import_state).data());
                        ImGui::TableSetColumnIndex(4);
                        if (record.source_size >= 1024u * 1024u) ImGui::Text("%.1f MB", static_cast<double>(record.source_size) / (1024.0 * 1024.0));
                        else if (record.source_size >= 1024u) ImGui::Text("%.1f KB", static_cast<double>(record.source_size) / 1024.0);
                        else ImGui::Text("%llu B", static_cast<unsigned long long>(record.source_size));
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }

            }

            if (asset_open_scene) {
                if (state.dirty) {
                    state.pending_action = PendingAction::OpenScene;
                    state.pending_open_path = *asset_open_scene;
                    state.request_unsaved_popup = true;
                } else {
                    open_scene(state, *asset_open_scene);
                }
            } else if (asset_instantiate_prefab) {
                command_instantiate_prefab(state, *asset_instantiate_prefab);
            }
            if (asset_open_ui) {
                if (const auto* record = state.asset_catalog.find_by_id(*asset_open_ui)) open_ui_authoring(state, *record);
            }

            const bool entity_selected_for_prefab = selected_entity_index(state).has_value();
            ImGui::BeginDisabled(!entity_selected_for_prefab);
            if (ImGui::Button("Create Prefab from Selection")) command_create_prefab_from_selected(state);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("Double-click scenes to open, prefabs to instantiate, and UI documents to author.");
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Scene Resources")) {
            ImGui::TextDisabled("Resources serialized inside the currently open scene.");
            if (ImGui::Button("+ Material")) command_create_material(state);
            ImGui::SameLine();
            if (ImGui::Button("+ Sprite Clip")) command_create_clip(state);
            ImGui::Separator();

            std::optional<std::size_t> duplicate_material;
            std::optional<std::size_t> delete_material;
            if (ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen)) {
                const auto& materials = state.scene.world.materials();
                if (materials.empty()) ImGui::TextDisabled("No materials in this scene.");
                for (std::size_t i = 0; i < materials.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i) + 200000);
                    const bool selected = state.selection.kind == SelectionKind::Material && state.selection.index == i;
                    if (ImGui::Selectable(materials[i].name.c_str(), selected)) {
                        commit_active_edit(state);
                        state.selection = {SelectionKind::Material, i};
                    }
                    if (ImGui::BeginPopupContextItem("material_context")) {
                        if (ImGui::MenuItem("Duplicate", "Ctrl+D")) duplicate_material = i;
                        if (ImGui::MenuItem("Delete", "Del")) delete_material = i;
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
            }
            if (duplicate_material && *duplicate_material < state.scene.world.materials().size()) {
                state.selection = {SelectionKind::Material, *duplicate_material}; command_duplicate_selection(state);
            } else if (delete_material && *delete_material < state.scene.world.materials().size()) {
                state.selection = {SelectionKind::Material, *delete_material}; command_delete_selection(state);
            }

            std::optional<std::size_t> duplicate_clip;
            std::optional<std::size_t> delete_clip;
            if (ImGui::CollapsingHeader("Sprite Clips", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (state.scene.sprite_clips.empty()) ImGui::TextDisabled("No sprite clips in this scene.");
                for (std::size_t i = 0; i < state.scene.sprite_clips.size(); ++i) {
                    const auto& clip = state.scene.sprite_clips[i];
                    ImGui::PushID(static_cast<int>(i) + 300000);
                    const bool selected = state.selection.kind == SelectionKind::SpriteClip && state.selection.index == i;
                    if (ImGui::Selectable(clip.name.c_str(), selected)) {
                        commit_active_edit(state);
                        state.selection = {SelectionKind::SpriteClip, i};
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%u directions x %u frames | %.2f fps | %s",
                        clip.direction_count, clip.frame_count, clip.frames_per_second, clip.loop ? "loop" : "once");
                    if (ImGui::BeginPopupContextItem("clip_context")) {
                        if (ImGui::MenuItem("Duplicate", "Ctrl+D")) duplicate_clip = i;
                        if (ImGui::MenuItem("Delete", "Del")) delete_clip = i;
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
            }
            if (duplicate_clip && *duplicate_clip < state.scene.sprite_clips.size()) {
                state.selection = {SelectionKind::SpriteClip, *duplicate_clip}; command_duplicate_selection(state);
            } else if (delete_clip && *delete_clip < state.scene.sprite_clips.size()) {
                state.selection = {SelectionKind::SpriteClip, *delete_clip}; command_delete_selection(state);
            }

            if (ImGui::CollapsingHeader("Registered Textures")) {
                for (const auto& texture : state.scene.world.textures()) ImGui::BulletText("%s", texture.name.c_str());
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("C# Scripts")) {
            if (ImGui::Button("Build C#")) build_managed_scripts(state);
            ImGui::SameLine();
            if (ImGui::Button("Refresh Metadata")) load_managed_metadata(state, true);
            ImGui::Separator();
            if (!state.managed_metadata.loaded) {
                ImGui::TextDisabled("No C# metadata loaded. Use Build C# to refresh it.");
            } else {
                ImGui::TextDisabled("Assembly: %s", state.managed_metadata.assembly.c_str());
                if (ImGui::BeginTable("ManagedScripts", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
                    ImGui::TableSetupColumn("Component");
                    ImGui::TableSetupColumn("Exposed", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                    ImGui::TableHeadersRow();
                    for (const auto& meta : state.managed_metadata.scripts) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(meta.class_name.c_str());
                        ImGui::TableSetColumnIndex(1); ImGui::Text("%zu", meta.fields.size());
                    }
                    ImGui::EndTable();
                }
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Project Settings")) {
            if (!state.project_loaded) {
                ImGui::TextDisabled("Open a .vesperaproject workspace to edit project settings.");
            } else {
                ImGui::SeparatorText("General");
                ImGui::InputText("Project Name", &state.project.name);
                ImGui::TextDisabled("Project file: %s", state.project_path.string().c_str());
                ImGui::TextDisabled("Assets root: %s", state.project.assets_root().string().c_str());

                ImGui::SeparatorText("Startup & Runtime");
                std::string startup_scene = state.project.startup_scene.generic_string();
                if (ImGui::InputText("Startup Scene", &startup_scene)) {
                    state.project.startup_scene = startup_scene;
                    state.project.startup_scene_asset_id.clear();
                }
                if (!state.project.startup_scene_asset_id.empty()) {
                    ImGui::TextDisabled("Asset ID: %s", state.project.startup_scene_asset_id.c_str());
                }
                if (ImGui::Button("Use Current Scene") && !state.scene_path.empty()) {
                    std::error_code ec;
                    const auto relative = std::filesystem::relative(state.scene_path, state.project.assets_root(), ec);
                    if (!ec && !relative.empty()) {
                        state.project.startup_scene = relative.lexically_normal();
                        if (const auto* startup_record = state.asset_catalog.find(state.project.startup_scene.generic_string())) {
                            state.project.startup_scene_asset_id = startup_record->asset_id;
                        } else {
                            state.project.startup_scene_asset_id.clear();
                        }
                    }
                }

                std::string startup_ui = state.project.startup_ui.generic_string();
                if (ImGui::InputText("Startup RML UI", &startup_ui)) {
                    state.project.startup_ui = startup_ui;
                    state.project.startup_ui_asset_id.clear();
                }
                if (!state.project.startup_ui_asset_id.empty()) {
                    ImGui::TextDisabled("Asset ID: %s", state.project.startup_ui_asset_id.c_str());
                }
                if (ImGui::Button("Use Selected RML")) {
                    const auto* selected_record = state.selection.kind == SelectionKind::Asset
                        ? state.asset_catalog.find_by_id(state.selection.asset_id) : nullptr;
                    if (!selected_record) {
                        push_console(state, ConsoleEntry::Level::Warning,
                            "Startup UI: select an RML document in Project/Assets first.");
                    } else if (selected_record->kind != vespera::AssetKind::RmlDocument) {
                        push_console(state, ConsoleEntry::Level::Warning,
                            "Startup UI: selected asset is not an RML document: " + selected_record->relative_path.generic_string());
                    } else {
                        state.project.startup_ui = selected_record->relative_path.lexically_normal();
                        state.project.startup_ui_asset_id = selected_record->asset_id;
                        push_console(state, ConsoleEntry::Level::Info,
                            "Startup UI set to " + selected_record->relative_path.generic_string());
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear Startup UI")) {
                    state.project.startup_ui.clear();
                    state.project.startup_ui_asset_id.clear();
                }
                ImGui::TextDisabled("Optional RML document shown when the standalone game starts. Leave empty for no startup UI.");

                std::string lua_entry = state.project.lua_entry.generic_string();
                if (ImGui::InputText("Lua Entry Script", &lua_entry)) {
                    state.project.lua_entry = lua_entry;
                    state.project.lua_entry_asset_id.clear();
                }
                if (!state.project.lua_entry_asset_id.empty()) {
                    ImGui::TextDisabled("Lua asset ID: %s", state.project.lua_entry_asset_id.c_str());
                }
                if (ImGui::Button("Use Selected Lua")) {
                    const auto* selected_record = state.selection.kind == SelectionKind::Asset
                        ? state.asset_catalog.find_by_id(state.selection.asset_id) : nullptr;
                    if (!selected_record) {
                        push_console(state, ConsoleEntry::Level::Warning,
                            "Lua entry: select a .lua asset in Project/Assets first.");
                    } else if (selected_record->kind != vespera::AssetKind::LuaScript) {
                        push_console(state, ConsoleEntry::Level::Warning,
                            "Lua entry: selected asset is not a Lua script: " + selected_record->relative_path.generic_string());
                    } else {
                        state.project.lua_entry = selected_record->relative_path.lexically_normal();
                        state.project.lua_entry_asset_id = selected_record->asset_id;
                        push_console(state, ConsoleEntry::Level::Info,
                            "Lua entry set to " + selected_record->relative_path.generic_string());
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear Lua Entry")) {
                    state.project.lua_entry.clear();
                    state.project.lua_entry_asset_id.clear();
                }
                ImGui::TextDisabled("Optional Lua entry script. C# and Lua can be used together.");

                ImGui::InputText("Window Title", &state.project.window_title);
                ImGui::InputInt("Window Width", &state.project.window_width, 16, 128);
                ImGui::InputInt("Window Height", &state.project.window_height, 16, 128);
                ImGui::Checkbox("Resizable", &state.project.window_resizable);
                ImGui::SameLine(); ImGui::Checkbox("Relative Mouse", &state.project.relative_mouse);
                ImGui::SameLine(); ImGui::Checkbox("Escape Quits", &state.project.escape_quits);
                ImGui::SameLine(); ImGui::Checkbox("VSync", &state.project.vsync);

                ImGui::SeparatorText("Input Actions");
                ImGui::TextDisabled("Input bindings are shared by the standalone game and in-editor Play Mode.");
                std::optional<std::size_t> remove_input_binding;
                if (state.project.input_bindings.empty()) {
                    ImGui::TextDisabled("No input bindings. Named C# input actions will remain unbound.");
                } else if (ImGui::BeginTable("ProjectInputBindings", 6,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 1.5f);
                    ImGui::TableSetupColumn("Device", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                    ImGui::TableSetupColumn("Code", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                    ImGui::TableSetupColumn("Scale", ImGuiTableColumnFlags_WidthFixed, 75.0f);
                    ImGui::TableSetupColumn("Deadzone", ImGuiTableColumnFlags_WidthFixed, 85.0f);
                    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 26.0f);
                    ImGui::TableHeadersRow();
                    for (std::size_t i = 0; i < state.project.input_bindings.size(); ++i) {
                        auto& binding = state.project.input_bindings[i];
                        ImGui::PushID(static_cast<int>(i) + 920000);
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0); ImGui::SetNextItemWidth(-1); ImGui::InputText("##action", &binding.action);
                        ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1); ImGui::InputText("##device", &binding.device);
                        ImGui::TableSetColumnIndex(2); ImGui::SetNextItemWidth(-1); ImGui::InputText("##code", &binding.code);
                        ImGui::TableSetColumnIndex(3); ImGui::SetNextItemWidth(-1); ImGui::DragFloat("##scale", &binding.scale, 0.05f, -100.0f, 100.0f, "%.2f");
                        ImGui::TableSetColumnIndex(4); ImGui::SetNextItemWidth(-1); ImGui::DragFloat("##deadzone", &binding.deadzone, 0.01f, 0.0f, 0.95f, "%.2f");
                        ImGui::TableSetColumnIndex(5);
                        if (ImGui::SmallButton("x")) remove_input_binding = i;
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                if (remove_input_binding && *remove_input_binding < state.project.input_bindings.size()) {
                    state.project.input_bindings.erase(
                        state.project.input_bindings.begin() + static_cast<std::ptrdiff_t>(*remove_input_binding));
                }
                if (ImGui::Button("+ Key Binding")) {
                    state.project.input_bindings.push_back({"new_action", "key", "W", 1.0f, 0.0f});
                }
                ImGui::SameLine();
                if (ImGui::Button("+ Gamepad Button")) {
                    state.project.input_bindings.push_back({"new_action", "gamepad_button", "South", 1.0f, 0.0f});
                }
                ImGui::SameLine();
                if (ImGui::Button("+ Gamepad Axis")) {
                    state.project.input_bindings.push_back({"new_action", "gamepad_axis", "LeftX", 1.0f, 0.18f});
                }

                ImGui::SeparatorText("Build & Package");
                ImGui::InputText("Company", &state.project.company_name);
                ImGui::InputText("Product Version", &state.project.product_version);
                ImGui::InputText("Package Name", &state.project.package_name);
                int deployment_index = state.project.managed_deployment == vespera::ManagedDeploymentMode::Portable ? 1 : 0;
                const char* deployment_items[] = {"Framework-dependent", "Portable (.NET bundled)"};
                if (ImGui::Combo("Managed Deployment", &deployment_index, deployment_items, IM_ARRAYSIZE(deployment_items))) {
                    state.project.managed_deployment = deployment_index == 1
                        ? vespera::ManagedDeploymentMode::Portable
                        : vespera::ManagedDeploymentMode::FrameworkDependent;
                }
                ImGui::TextDisabled("Portable builds include a compatible .NET runtime beside the game executable.");
                ImGui::InputText("Executable Name", &state.project.executable_name);
                std::string build_output = state.project.build_output_directory.generic_string();
                if (ImGui::InputText("Build Output Directory", &build_output)) state.project.build_output_directory = build_output;
                std::string game_icon = state.project.game_icon.generic_string();
                if (ImGui::InputText("Game Icon Asset", &game_icon)) {
                    state.project.game_icon = game_icon;
                    state.project.game_icon_asset_id.clear();
                }
                if (!state.project.game_icon_asset_id.empty()) {
                    ImGui::TextDisabled("Game icon asset ID: %s", state.project.game_icon_asset_id.c_str());
                }
                ImGui::Checkbox("Development Symbols / Diagnostics", &state.project.development_diagnostics);
                ImGui::TextDisabled("Debug favors diagnostics, Development balances diagnostics and speed, and Release is optimized.");
                ImGui::TextDisabled("The selected game icon is included with exported packages. Executable icon stamping is not yet supported.");
                ImGui::TextDisabled("Resolved output: %s", state.project.build_output_root().string().c_str());
                ImGui::BeginDisabled(state.build_job.running || editor_is_playing(state));
                if (ImGui::Button("Build Game...", ImVec2(130.0f, 0.0f))) {
                    state.build_job.window_open = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Build & Run", ImVec2(120.0f, 0.0f))) {
                    state.build_job.window_open = true;
                    (void)start_editor_build_job(state, "Development", true);
                }
                ImGui::EndDisabled();
                ImGui::TextDisabled("Builds run in the background; progress and output appear in Build Game.");

                ImGui::SeparatorText("Managed C#");
                std::string managed_project = state.project.managed_project.generic_string();
                if (ImGui::InputText("Managed Project", &managed_project)) state.project.managed_project = managed_project;
                ImGui::InputText("Managed Assembly", &state.project.managed_assembly);
                ImGui::InputText("Game Target", &state.project.game_target);

                ImGui::SeparatorText("Additional Build Assets");
                ImGui::TextDisabled("Startup scene dependencies are included automatically. Add assets here only when they are loaded dynamically.");
                std::optional<std::size_t> remove_build_include;
                if (state.project.build_includes.empty()) ImGui::TextDisabled("No explicit build includes.");
                for (std::size_t i = 0; i < state.project.build_includes.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i) + 810000);
                    const bool stable = i < state.project.build_include_asset_ids.size() && !state.project.build_include_asset_ids[i].empty();
                    ImGui::BulletText("%s%s", state.project.build_includes[i].generic_string().c_str(), stable ? "  [tracked]" : "");
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Remove")) remove_build_include = i;
                    ImGui::PopID();
                }
                if (remove_build_include && *remove_build_include < state.project.build_includes.size()) {
                    const auto index = *remove_build_include;
                    state.project.build_includes.erase(state.project.build_includes.begin() + static_cast<std::ptrdiff_t>(index));
                    if (index < state.project.build_include_asset_ids.size()) {
                        state.project.build_include_asset_ids.erase(state.project.build_include_asset_ids.begin() + static_cast<std::ptrdiff_t>(index));
                    }
                }
                const auto manifest = vespera::build_project_asset_manifest(state.project, state.asset_catalog);
                const ImVec4 manifest_color = manifest.valid()
                    ? ImVec4(0.48f, 0.82f, 0.62f, 1.0f) : ImVec4(1.0f, 0.55f, 0.42f, 1.0f);
                ImGui::TextColored(manifest_color, "Build assets: %zu included | %zu missing input(s) | %zu broken reference(s) | %zu outdated path(s)",
                    manifest.assets.size(), manifest.missing_roots.size(), manifest.broken_dependencies.size(), manifest.stale_root_paths.size());
                if (state.last_asset_report.stale_fallback_paths != 0 || !manifest.stale_root_paths.empty()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.68f, 0.25f, 1.0f),
                        "Some saved asset paths are outdated, but the asset links still resolve.");
                }
                if (ImGui::Button("Repair Asset References", ImVec2(220.0f, 0.0f))) {
                    repair_asset_fallback_paths(state, true);
                }

                ImGui::SeparatorText("Validation");
                const auto issues = vespera::validate_vespera_project(state.project);
                std::size_t errors = 0, warnings = 0;
                for (const auto& issue : issues) {
                    if (issue.severity == vespera::ProjectValidationSeverity::Error) ++errors; else ++warnings;
                }
                if (errors == 0 && warnings == 0) ImGui::TextDisabled("Project validation passed.");
                else ImGui::Text("Project validation: %zu error(s), %zu warning(s)", errors, warnings);
                for (const auto& issue : issues) {
                    const ImVec4 color = issue.severity == vespera::ProjectValidationSeverity::Error
                        ? ImVec4(0.95f, 0.38f, 0.34f, 1.0f) : ImVec4(0.95f, 0.72f, 0.28f, 1.0f);
                    ImGui::TextColored(color, "%s", issue.message.c_str());
                }

                if (ImGui::Button("Save Project", ImVec2(130.0f, 0.0f))) {
                    const auto result = vespera::save_vespera_project(state.project, state.project_path);
                    push_console(state, result ? ConsoleEntry::Level::Info : ConsoleEntry::Level::Error, result.message);
                }
                ImGui::SameLine();
                if (ImGui::Button("Validate to Console", ImVec2(150.0f, 0.0f))) {
                    if (issues.empty()) push_console(state, ConsoleEntry::Level::Info, "Project validation passed.");
                    for (const auto& issue : issues) push_console(state,
                        issue.severity == vespera::ProjectValidationSeverity::Error
                            ? ConsoleEntry::Level::Error : ConsoleEntry::Level::Warning,
                        "Project: " + issue.message);
                }
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}


} // namespace vespera::editor
